#include "Engine/Core/JobSystem.h"
#include "Engine/Utils/Logger.h"

namespace Engine {

static thread_local int32_t t_WorkerIndex = -1;
std::unique_ptr<JobSystem> JobSystem::s_Instance = nullptr;

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

    m_Initialized = true;
    ENGINE_CORE_INFO("JobSystem initialized with {} worker threads", m_WorkerCount);
}

JobSystem::~JobSystem() {
    if (m_Initialized) {
        for (auto& worker : m_Workers) {
            worker.request_stop();
        }
        {
            std::lock_guard lock(m_WakeMutex);
            m_WakeCV.notify_all();
        }
        m_Workers.clear();
        m_WorkerQueues.clear();
        m_PendingJobs.store(0, std::memory_order_seq_cst);
        m_WaitingWorkers.store(0, std::memory_order_seq_cst);
        m_Initialized = false;
    }
}

void JobSystem::Init(uint32_t workerCount) {
    if (s_Instance && s_Instance->m_Initialized) {
        ENGINE_CORE_WARN("JobSystem::Init called while already initialized");
        return;
    }

    if (workerCount == 0) {
        uint32_t numCores = std::thread::hardware_concurrency();
        workerCount = (numCores > 1) ? (numCores - 1) : 1;
    }

    s_Instance = std::unique_ptr<JobSystem>(new JobSystem(workerCount));
}

void JobSystem::Shutdown() {
    if (!s_Instance || !s_Instance->m_Initialized) {
        return;
    }

    ENGINE_CORE_INFO("Shutting down JobSystem...");

    // 1. Signal stop to all workers
    for (auto& worker : s_Instance->m_Workers) {
        worker.request_stop();
    }

    // 2. Wake all sleeping workers
    {
        std::lock_guard lock(s_Instance->m_WakeMutex);
        s_Instance->m_WakeCV.notify_all();
    }

    // 3. Clear workers (std::jthread destructor will wait/join each thread)
    s_Instance->m_Workers.clear();
    s_Instance->m_WorkerQueues.clear();
    s_Instance->m_PendingJobs.store(0, std::memory_order_seq_cst);
    s_Instance->m_WaitingWorkers.store(0, std::memory_order_seq_cst);
    s_Instance->m_Initialized = false;
    s_Instance.reset();

    ENGINE_CORE_INFO("JobSystem shutdown complete");
}

bool JobSystem::IsInitialized() noexcept {
    return s_Instance != nullptr && s_Instance->m_Initialized;
}

uint32_t JobSystem::GetWorkerCount() noexcept {
    return (s_Instance && s_Instance->m_Initialized) ? s_Instance->m_WorkerCount : 0;
}

size_t JobSystem::GetPendingJobCount() noexcept {
    return (s_Instance && s_Instance->m_Initialized) ? s_Instance->m_PendingJobs.load(std::memory_order_relaxed) : 0;
}

uint32_t JobSystem::GetWaitingWorkerCount() noexcept {
    return (s_Instance && s_Instance->m_Initialized) ? s_Instance->m_WaitingWorkers.load(std::memory_order_relaxed) : 0;
}

int32_t JobSystem::GetCurrentWorkerIndex() noexcept {
    return t_WorkerIndex;
}

JobHandle JobSystem::Dispatch(std::function<void()> job) {
    if (!job) {
        return JobHandle{};
    }

    auto jobState = std::make_shared<JobState>();
    jobState->jobFunction = std::move(job);

    if (!IsInitialized()) {
        jobState->jobFunction();
        jobState->isFinished.store(true, std::memory_order_release);
        return JobHandle{ jobState };
    }

    s_Instance->SubmitJob(jobState);
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
        return JobHandle{};
    }

    auto jobState = std::make_shared<JobState>();
    jobState->jobFunction = std::move(job);

    if (!IsInitialized()) {
        jobState->jobFunction();
        jobState->isFinished.store(true, std::memory_order_release);
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
        s_Instance->SubmitJob(jobState);
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
                s_Instance->SubmitJob(jobState);
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

    while (!stopToken.stop_requested()) {
        QueuedJob job = FindWork(workerIndex);
        if (job.IsValid()) {
            job.Execute();
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

    m_PendingJobs.fetch_add(1, std::memory_order_seq_cst);

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

void JobSystem::SubmitRawJob(QueuedJob::RawCallback callback, void* userData) {
    if (!callback) return;
    if (!IsInitialized()) {
        callback(userData);
        return;
    }
    QueuedJob entry;
    entry.callback = callback;
    entry.userData = userData;
    s_Instance->SubmitQueuedJob(std::move(entry));
}

void JobSystem::ExecuteJobState(const std::shared_ptr<JobState>& jobState) {
    if (!jobState) return;

    if (jobState->jobFunction) {
        jobState->jobFunction();
    }

    jobState->isFinished.store(true, std::memory_order_release);
    jobState->isFinished.notify_all();

    std::vector<std::shared_ptr<JobState>> readyContinuations;
    {
        std::lock_guard lock(jobState->continuationMutex);
        for (auto& cont : jobState->continuations) {
            if (cont->pendingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                readyContinuations.push_back(std::move(cont));
            }
        }
        jobState->continuations.clear();
    }

    for (auto& cont : readyContinuations) {
        if (s_Instance) {
            s_Instance->SubmitJob(std::move(cont));
        } else {
            ExecuteJobState(cont);
        }
    }
}

bool JobSystem::HasPendingWork() const {
    return m_PendingJobs.load(std::memory_order_seq_cst) > 0;
}

bool JobSystem::ExecuteOneJob() {
    if (!s_Instance || !s_Instance->m_Initialized) {
        return false;
    }

    // Fast-path early exit: if no pending jobs in any queue, don't lock anything!
    if (s_Instance->m_PendingJobs.load(std::memory_order_acquire) == 0) {
        return false;
    }

    const int32_t wIdx = t_WorkerIndex;
    QueuedJob job;

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

    if (job.IsValid()) {
        job.Execute();
        return true;
    }

    return false;
}

} // namespace Engine

