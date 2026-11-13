#include "Engine/Core/JobSystem.h"
#include "Engine/Utils/Logger.h"
#include <exception>
#include <shared_mutex>

namespace Engine {

static thread_local int32_t t_WorkerIndex = -1;
std::unique_ptr<JobSystem> JobSystem::s_Instance = nullptr;
static std::shared_mutex s_InstanceMutex;

void JobHandle::Wait() const {
    if (!m_State) return;

    while (!m_State->isFinished.load(std::memory_order_acquire)) {
        if (!JobSystem::ExecuteOneJob()) {
            m_State->isFinished.wait(false, std::memory_order_acquire);
        }
    }
}

JobHandle JobHandle::Then(std::function<void()> nextJob) const {
    return JobSystem::DispatchAfter(*this, std::move(nextJob));
}

JobSystem::JobSystem(uint32_t workerCount)
    : m_WorkerCount(workerCount) {
    m_PendingJobs.store(0, std::memory_order_seq_cst);
    m_WaitingWorkers.store(0, std::memory_order_seq_cst);
    m_QueuesAlive.store(true, std::memory_order_release);
    m_WorkerQueues.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        m_WorkerQueues.push_back(std::make_unique<WorkerQueue>());
    }

    m_Workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        m_Workers.emplace_back([this, i](std::stop_token stopToken) {
            WorkerLoop(stopToken, i);
        });
    }

    m_Initialized.store(true, std::memory_order_release);
    ENGINE_CORE_INFO("JobSystem initialized with {} worker threads", m_WorkerCount);
}

JobSystem::~JobSystem() {
    m_Initialized.store(false, std::memory_order_seq_cst);
    if (!m_Workers.empty()) {
        for (auto& worker : m_Workers) {
            worker.request_stop();
        }
        {
            std::lock_guard lock(m_WakeMutex);
            m_WakeCV.notify_all();
        }
        m_Workers.clear();
    }
    m_QueuesAlive.store(false, std::memory_order_seq_cst);
    DrainQueuedJobsOnShutdown();
    m_WorkerQueues.clear();
    m_PendingJobs.store(0, std::memory_order_seq_cst);
    m_WaitingWorkers.store(0, std::memory_order_seq_cst);
}

void JobSystem::Init(uint32_t workerCount) {
    std::unique_lock lock(s_InstanceMutex);
    if (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire)) {
        ENGINE_CORE_WARN("JobSystem::Init called while already initialized");
        return;
    }

    if (workerCount == 0) {
        uint32_t numCores = std::thread::hardware_concurrency();
        workerCount = (numCores > 1) ? (numCores - 1) : 1;
        ENGINE_CORE_DEBUG("JobSystem: Auto-detected {} worker threads (hardware_concurrency={})", workerCount, numCores);
    }

    s_Instance = std::make_unique<JobSystem>(workerCount);
}

void JobSystem::Shutdown() {
    JobSystem* instance = nullptr;
    {
        std::unique_lock lock(s_InstanceMutex);
        if (!s_Instance || !s_Instance->m_Initialized.load(std::memory_order_acquire)) {
            return;
        }
        // Stop accepting new queued submissions immediately so Dispatch/ParallelFor fall back inline cleanly
        s_Instance->m_Initialized.store(false, std::memory_order_seq_cst);
        instance = s_Instance.get();
    }

    ENGINE_CORE_INFO("Shutting down JobSystem...");

    // 1. Signal stop to all workers; WorkerLoop will finish draining pending queued jobs before exiting
    for (auto& worker : instance->m_Workers) {
        worker.request_stop();
    }

    // 2. Wake all sleeping workers
    {
        std::lock_guard lock(instance->m_WakeMutex);
        instance->m_WakeCV.notify_all();
    }

    // 3. Join workers outside s_InstanceMutex so worker continuations / inline submissions cannot deadlock
    instance->m_Workers.clear();

    // 4. Detach global singleton atomically, then flush any remaining queued items defensively
    std::unique_ptr<JobSystem> doomed;
    {
        std::unique_lock lock(s_InstanceMutex);
        instance->m_QueuesAlive.store(false, std::memory_order_seq_cst);
        doomed = std::move(s_Instance);
    }

    if (doomed) {
        doomed->DrainQueuedJobsOnShutdown();
        doomed->m_WorkerQueues.clear();
        doomed->m_PendingJobs.store(0, std::memory_order_seq_cst);
        doomed->m_WaitingWorkers.store(0, std::memory_order_seq_cst);
        doomed.reset();
    }

    ENGINE_CORE_INFO("JobSystem shutdown complete");
}

bool JobSystem::IsInitialized() noexcept {
    std::shared_lock lock(s_InstanceMutex);
    return s_Instance != nullptr && s_Instance->m_Initialized.load(std::memory_order_acquire);
}

uint32_t JobSystem::GetWorkerCount() noexcept {
    std::shared_lock lock(s_InstanceMutex);
    return (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire)) ? s_Instance->m_WorkerCount : 0;
}

size_t JobSystem::GetPendingJobCount() noexcept {
    std::shared_lock lock(s_InstanceMutex);
    return (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire))
        ? s_Instance->m_PendingJobs.load(std::memory_order_relaxed)
        : 0;
}

uint32_t JobSystem::GetWaitingWorkerCount() noexcept {
    std::shared_lock lock(s_InstanceMutex);
    return (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire))
        ? s_Instance->m_WaitingWorkers.load(std::memory_order_relaxed)
        : 0;
}

int32_t JobSystem::GetCurrentWorkerIndex() noexcept {
    return t_WorkerIndex;
}

JobHandle JobSystem::Dispatch(std::function<void()> job) {
    if (!job) {
        ENGINE_CORE_WARN("JobSystem::Dispatch ignored empty job callback");
        return JobHandle{};
    }

    auto jobState = std::make_shared<JobState>();
    jobState->jobFunction = std::move(job);

    {
        std::shared_lock lock(s_InstanceMutex);
        if (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire)) {
            s_Instance->SubmitJob(jobState);
            return JobHandle{ jobState };
        }
    }

    ENGINE_CORE_DEBUG("JobSystem::Dispatch executing job inline (JobSystem uninitialized)");
    ExecuteJobState(jobState);
    return JobHandle{ jobState };
}

JobHandle JobSystem::DispatchAfter(const JobHandle& dependency, std::function<void()> job) {
    if (!dependency.IsValid()) {
        return Dispatch(std::move(job));
    }
    return DispatchAfter(std::vector<JobHandle>{ dependency }, std::move(job));
}

JobHandle JobSystem::DispatchAfter(const std::vector<JobHandle>& dependencies, std::function<void()> job) {
    if (!job) {
        ENGINE_CORE_WARN("JobSystem::DispatchAfter ignored empty job callback");
        return JobHandle{};
    }

    auto jobState = std::make_shared<JobState>();
    jobState->jobFunction = std::move(job);

    if (!IsInitialized()) {
        ENGINE_CORE_DEBUG("JobSystem::DispatchAfter executing job inline (JobSystem uninitialized)");
        ExecuteJobState(jobState);
        return JobHandle{ jobState };
    }

    std::vector<std::shared_ptr<JobState>> pendingDeps;
    pendingDeps.reserve(dependencies.size());

    for (const auto& dep : dependencies) {
        if (dep.IsValid() && !dep.IsComplete()) {
            pendingDeps.push_back(dep.m_State);
        }
    }

    if (pendingDeps.empty()) {
        SubmitOrExecuteJobState(jobState);
        return JobHandle{ jobState };
    }

    jobState->pendingDependencies.store(static_cast<uint32_t>(pendingDeps.size()), std::memory_order_release);

    for (const auto& depState : pendingDeps) {
        bool alreadyFinished = false;
        {
            std::lock_guard lock(depState->continuationMutex);
            if (depState->isFinished.load(std::memory_order_acquire)) {
                alreadyFinished = true;
            } else {
                depState->continuations.push_back(jobState);
            }
        }

        if (alreadyFinished) {
            if (jobState->pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                SubmitOrExecuteJobState(jobState);
                break;
            }
        }
    }

    return JobHandle{ jobState };
}

void JobSystem::Wait(const JobHandle& handle) {
    handle.Wait();
}

void JobSystem::WaitAll(const std::vector<JobHandle>& handles) {
    for (const auto& handle : handles) {
        handle.Wait();
    }
}

void QueuedJob::Execute() {
    if (callback) {
        callback(userData);
    } else if (retainedState) {
        JobSystem::ExecuteJobState(retainedState);
    }
}

bool JobSystem::PopWorkerJob(uint32_t index, QueuedJob& outJob) {
    if (m_WorkerQueues[index]->PopLocal(outJob)) {
        m_PendingJobs.fetch_sub(1, std::memory_order_seq_cst);
        return true;
    }
    return false;
}

bool JobSystem::StealWorkerJob(uint32_t index, QueuedJob& outJob) {
    if (m_WorkerQueues[index]->Steal(outJob)) {
        m_PendingJobs.fetch_sub(1, std::memory_order_seq_cst);
        return true;
    }
    return false;
}

bool JobSystem::PopGlobalJob(QueuedJob& outJob) {
    if (m_GlobalQueue.Pop(outJob)) {
        m_PendingJobs.fetch_sub(1, std::memory_order_seq_cst);
        return true;
    }
    return false;
}

void JobSystem::WorkerLoop(std::stop_token stopToken, uint32_t workerIndex) {
    t_WorkerIndex = static_cast<int32_t>(workerIndex);

    while (!stopToken.stop_requested() || HasPendingWork()) {
        QueuedJob job = FindWork(workerIndex);
        if (job.IsValid()) {
            job.Execute();
        } else if (stopToken.stop_requested()) {
            if (!HasPendingWork()) {
                break;
            }
            std::this_thread::yield();
        } else {
            std::unique_lock lock(m_WakeMutex);
            if (stopToken.stop_requested() || m_PendingJobs.load(std::memory_order_seq_cst) > 0) {
                continue;
            }

            m_WaitingWorkers.fetch_add(1, std::memory_order_seq_cst);
            m_WakeCV.wait(lock, [&]() {
                return stopToken.stop_requested() || m_PendingJobs.load(std::memory_order_seq_cst) > 0;
            });
            m_WaitingWorkers.fetch_sub(1, std::memory_order_seq_cst);
        }
    }

    t_WorkerIndex = -1;
}

QueuedJob JobSystem::FindWork(uint32_t workerIndex) {
    QueuedJob job;

    // 1. Try local worker queue (LIFO for hot cache)
    if (PopWorkerJob(workerIndex, job)) {
        return job;
    }

    // 2. Try global queue (FIFO)
    if (PopGlobalJob(job)) {
        return job;
    }

    // 3. Work stealing from neighboring workers (FIFO / front for oldest tasks)
    const uint32_t count = static_cast<uint32_t>(m_WorkerQueues.size());
    if (count > 1) {
        for (uint32_t i = 1; i < count; ++i) {
            uint32_t victim = (workerIndex + i) % count;
            if (StealWorkerJob(victim, job)) {
                return job;
            }
        }
    }

    return {};
}

void JobSystem::SubmitQueuedJob(QueuedJob&& job) {
    // Increment pending count strictly before publishing into any queue so consumers/stealers
    // can never decrement m_PendingJobs below 0 (uint64/size_t underflow to SIZE_MAX).
    m_PendingJobs.fetch_add(1, std::memory_order_seq_cst);

    try {
        const int32_t wIdx = t_WorkerIndex;
        if (wIdx >= 0 && static_cast<size_t>(wIdx) < m_WorkerQueues.size()) {
            m_WorkerQueues[wIdx]->Push(std::move(job));
        } else if (!m_WorkerQueues.empty()) {
            static std::atomic<uint32_t> s_WorkerDispatchIndex{ 0 };
            const uint32_t idx = s_WorkerDispatchIndex.fetch_add(1, std::memory_order_relaxed) % m_WorkerQueues.size();
            m_WorkerQueues[idx]->Push(std::move(job));
        } else {
            m_GlobalQueue.Push(std::move(job));
        }
    } catch (...) {
        m_PendingJobs.fetch_sub(1, std::memory_order_seq_cst);
        throw;
    }

    if (m_WaitingWorkers.load(std::memory_order_seq_cst) > 0) {
        std::lock_guard lock(m_WakeMutex);
        m_WakeCV.notify_one();
    }
}

void JobSystem::SubmitJob(std::shared_ptr<JobState> job) {
    if (!job) return;
    QueuedJob entry;
    entry.retainedState = std::move(job);
    SubmitQueuedJob(std::move(entry));
}

void JobSystem::SubmitOrExecuteJobState(std::shared_ptr<JobState> jobState) {
    if (!jobState) return;
    {
        std::shared_lock lock(s_InstanceMutex);
        if (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire)) {
            s_Instance->SubmitJob(std::move(jobState));
            return;
        }
    }
    ExecuteJobState(jobState);
}

void JobSystem::SubmitRawJob(QueuedJob::RawCallback callback, void* userData) {
    if (!callback) return;
    {
        std::shared_lock lock(s_InstanceMutex);
        if (s_Instance && s_Instance->m_Initialized.load(std::memory_order_acquire)) {
            QueuedJob entry;
            entry.callback = callback;
            entry.userData = userData;
            s_Instance->SubmitQueuedJob(std::move(entry));
            return;
        }
    }
    ENGINE_CORE_TRACE("JobSystem::SubmitRawJob executing callback inline (JobSystem uninitialized)");
    callback(userData);
}

void JobSystem::ExecuteJobState(const std::shared_ptr<JobState>& jobState) {
    if (!jobState) return;

    struct CompletionGuard {
        const std::shared_ptr<JobState>& state;
        ~CompletionGuard() {
            state->isFinished.store(true, std::memory_order_release);
            state->isFinished.notify_all();

            std::vector<std::shared_ptr<JobState>> readyContinuations;
            {
                std::lock_guard lock(state->continuationMutex);
                for (auto& cont : state->continuations) {
                    if (cont->pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        readyContinuations.push_back(std::move(cont));
                    }
                }
                state->continuations.clear();
            }

            for (auto& cont : readyContinuations) {
                SubmitOrExecuteJobState(std::move(cont));
            }
        }
    } completionGuard{ jobState };

    if (jobState->jobFunction) {
        try {
            jobState->jobFunction();
        } catch (const std::exception& e) {
            ENGINE_CORE_ERROR("JobSystem: Unhandled exception in dispatched job: {}", e.what());
        } catch (...) {
            ENGINE_CORE_ERROR("JobSystem: Unhandled unknown exception in dispatched job");
        }
    }
}

void JobSystem::DrainQueuedJobsOnShutdown() noexcept {
    bool drainedAny = true;
    while (drainedAny) {
        drainedAny = false;
        QueuedJob job;
        while (PopGlobalJob(job)) {
            drainedAny = true;
            job.Execute();
        }
        for (size_t i = 0; i < m_WorkerQueues.size(); ++i) {
            while (PopWorkerJob(static_cast<uint32_t>(i), job)) {
                drainedAny = true;
                job.Execute();
            }
        }
    }
}

bool JobSystem::HasPendingWork() const {
    return m_PendingJobs.load(std::memory_order_seq_cst) > 0;
}

bool JobSystem::ExecuteOneJob() {
    QueuedJob job;
    {
        std::shared_lock lock(s_InstanceMutex);
        if (!s_Instance || !s_Instance->m_QueuesAlive.load(std::memory_order_acquire)) {
            return false;
        }

        // Fast-path early exit: if no pending jobs in any queue, don't lock anything!
        if (s_Instance->m_PendingJobs.load(std::memory_order_acquire) == 0) {
            return false;
        }

        const int32_t wIdx = t_WorkerIndex;
        if (wIdx >= 0 && static_cast<size_t>(wIdx) < s_Instance->m_WorkerQueues.size()) {
            job = s_Instance->FindWork(static_cast<uint32_t>(wIdx));
        } else {
            if (!s_Instance->PopGlobalJob(job)) {
                const size_t count = s_Instance->m_WorkerQueues.size();
                for (size_t i = 0; i < count; ++i) {
                    if (s_Instance->StealWorkerJob(static_cast<uint32_t>(i), job)) {
                        break;
                    }
                }
            }
        }
    }

    if (job.IsValid()) {
        job.Execute();
        return true;
    }

    return false;
}

} // namespace Engine

