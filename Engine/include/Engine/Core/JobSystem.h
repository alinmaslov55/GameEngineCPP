#ifndef CORE_JOBSYSTEM_H
#define CORE_JOBSYSTEM_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <concepts>
#include <latch>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <algorithm>

namespace Engine {

class JobSystem;

struct JobState {
    std::atomic<bool> isFinished{ false };
    std::atomic<uint32_t> pendingDependencies{ 0 };
    std::function<void()> jobFunction;
    std::vector<std::shared_ptr<JobState>> continuations;
    std::mutex continuationMutex;
};

class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(std::shared_ptr<JobState> state) : m_State(std::move(state)) {}

    [[nodiscard]] bool IsValid() const noexcept { return m_State != nullptr; }
    [[nodiscard]] bool IsComplete() const noexcept {
        return !m_State || m_State->isFinished.load(std::memory_order_acquire);
    }

    void Wait() const;

    [[nodiscard]] JobHandle Then(std::function<void()> nextJob) const;

private:
    std::shared_ptr<JobState> m_State;
    friend class JobSystem;
};

class JobSystem {
public:
    static void Init(uint32_t workerCount = 0);
    static void Shutdown();

    [[nodiscard]] static bool IsInitialized() noexcept;
    [[nodiscard]] static uint32_t GetWorkerCount() noexcept;
    [[nodiscard]] static size_t GetPendingJobCount() noexcept;
    [[nodiscard]] static uint32_t GetWaitingWorkerCount() noexcept;

    // Returns the worker index [0, GetWorkerCount() - 1] if called from a JobSystem worker thread,
    // or -1 if called from the main thread or an external calling thread.
    [[nodiscard]] static int32_t GetCurrentWorkerIndex() noexcept;

    // Dispatches a job to the worker pool and returns a JobHandle
    static JobHandle Dispatch(std::function<void()> job);

    // Dispatches a job that begins only after the specified dependency has finished
    static JobHandle DispatchAfter(const JobHandle& dependency, std::function<void()> job);

    // Dispatches a job that begins only after all specified dependencies have finished
    static JobHandle DispatchAfter(const std::vector<JobHandle>& dependencies, std::function<void()> job);

    // Waits for a JobHandle to complete while executing pending jobs on the calling thread
    static void Wait(const JobHandle& handle);

    // Synchronously executes pending jobs on the calling thread until all handles complete
    static void WaitAll(const std::vector<JobHandle>& handles);

    // Executes a single pending job from local/global/stolen queues on the current thread
    // Returns true if a job was found and executed; false if no work is currently available
    static bool ExecuteOneJob();

    // ParallelFor: Distributes [start, end) index iterations across worker threads using std::latch
    // The calling thread participates in work stealing and returns only when all iterations complete
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType>
    static void ParallelFor(IndexType start, IndexType end, Func&& func, size_t chunkSize = 0) {
        if (start >= end) {
            return;
        }

        const size_t totalItems = static_cast<size_t>(end - start);
        const uint32_t workers = GetWorkerCount();

        // If not initialized, only 1 item, or 0 workers, run sequentially on current thread
        if (!IsInitialized() || workers == 0 || totalItems <= 1) {
            for (IndexType i = start; i < end; ++i) {
                func(i);
            }
            return;
        }

        const size_t threadsToUse = std::max<size_t>(1, workers + 1);
        if (chunkSize == 0) {
            chunkSize = std::max<size_t>(1, totalItems / (threadsToUse * 4));
        }

        const size_t chunkCount = (totalItems + chunkSize - 1) / chunkSize;
        if (chunkCount <= 1) {
            for (IndexType i = start; i < end; ++i) {
                func(i);
            }
            return;
        }

        std::latch workDone(static_cast<ptrdiff_t>(chunkCount));

        for (size_t c = 0; c < chunkCount; ++c) {
            const IndexType chunkStart = start + static_cast<IndexType>(c * chunkSize);
            const IndexType chunkEnd = std::min(end, chunkStart + static_cast<IndexType>(chunkSize));

            Dispatch([&workDone, &func, chunkStart, chunkEnd]() {
                for (IndexType i = chunkStart; i < chunkEnd; ++i) {
                    func(i);
                }
                workDone.count_down();
            });
        }

        while (!workDone.try_wait()) {
            if (!ExecuteOneJob()) {
                workDone.wait();
                break;
            }
        }
    }

    // ParallelFor overload for count [0, count)
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType>
    static void ParallelFor(IndexType count, Func&& func, size_t chunkSize = 0) {
        ParallelFor(static_cast<IndexType>(0), count, std::forward<Func>(func), chunkSize);
    }

    // ParallelForBatched: Distributes [start, end) ranges as batches [batchStart, batchEnd)
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType, IndexType>
    static void ParallelForBatched(IndexType start, IndexType end, Func&& func, size_t chunkSize = 0) {
        if (start >= end) {
            return;
        }

        const size_t totalItems = static_cast<size_t>(end - start);
        const uint32_t workers = GetWorkerCount();

        if (!IsInitialized() || workers == 0 || totalItems <= 1) {
            func(start, end);
            return;
        }

        const size_t threadsToUse = std::max<size_t>(1, workers + 1);
        if (chunkSize == 0) {
            chunkSize = std::max<size_t>(1, totalItems / (threadsToUse * 4));
        }

        const size_t chunkCount = (totalItems + chunkSize - 1) / chunkSize;
        if (chunkCount <= 1) {
            func(start, end);
            return;
        }

        std::latch workDone(static_cast<ptrdiff_t>(chunkCount));

        for (size_t c = 0; c < chunkCount; ++c) {
            const IndexType chunkStart = start + static_cast<IndexType>(c * chunkSize);
            const IndexType chunkEnd = std::min(end, chunkStart + static_cast<IndexType>(chunkSize));

            Dispatch([&workDone, &func, chunkStart, chunkEnd]() {
                func(chunkStart, chunkEnd);
                workDone.count_down();
            });
        }

        while (!workDone.try_wait()) {
            if (!ExecuteOneJob()) {
                workDone.wait();
                break;
            }
        }
    }

    JobSystem(uint32_t workerCount);
    ~JobSystem();

    class alignas(64) WorkerQueue {
    public:
        void Push(std::shared_ptr<JobState> job) {
            std::lock_guard lock(m_Mutex);
            m_Jobs.push_back(std::move(job));
        }

        bool PopLocal(std::shared_ptr<JobState>& outJob) {
            std::lock_guard lock(m_Mutex);
            if (m_Jobs.empty()) return false;
            outJob = std::move(m_Jobs.back());
            m_Jobs.pop_back();
            return true;
        }

        bool Steal(std::shared_ptr<JobState>& outJob) {
            std::unique_lock lock(m_Mutex, std::try_to_lock);
            if (!lock.owns_lock() || m_Jobs.empty()) return false;
            outJob = std::move(m_Jobs.front());
            m_Jobs.pop_front();
            return true;
        }

        bool Empty() const {
            std::lock_guard lock(m_Mutex);
            return m_Jobs.empty();
        }

    private:
        mutable std::mutex m_Mutex;
        std::deque<std::shared_ptr<JobState>> m_Jobs;
    };

    class alignas(64) GlobalQueue {
    public:
        void Push(std::shared_ptr<JobState> job) {
            std::lock_guard lock(m_Mutex);
            m_Jobs.push_back(std::move(job));
        }

        bool Pop(std::shared_ptr<JobState>& outJob) {
            std::lock_guard lock(m_Mutex);
            if (m_Jobs.empty()) return false;
            outJob = std::move(m_Jobs.front());
            m_Jobs.pop_front();
            return true;
        }

        bool Empty() const {
            std::lock_guard lock(m_Mutex);
            return m_Jobs.empty();
        }

    private:
        mutable std::mutex m_Mutex;
        std::deque<std::shared_ptr<JobState>> m_Jobs;
    };

private:
    void WorkerLoop(std::stop_token stopToken, uint32_t workerIndex);
    std::shared_ptr<JobState> FindWork(uint32_t workerIndex);
    void SubmitJob(std::shared_ptr<JobState> job);
    static void ExecuteJobState(const std::shared_ptr<JobState>& jobState);
    bool HasPendingWork() const;

    bool PopWorkerJob(uint32_t index, std::shared_ptr<JobState>& outJob);
    bool StealWorkerJob(uint32_t index, std::shared_ptr<JobState>& outJob);
    bool PopGlobalJob(std::shared_ptr<JobState>& outJob);

private:
    static std::unique_ptr<JobSystem> s_Instance;

    bool m_Initialized = false;
    uint32_t m_WorkerCount = 0;
    std::vector<std::jthread> m_Workers;
    std::vector<std::unique_ptr<WorkerQueue>> m_WorkerQueues;
    alignas(64) GlobalQueue m_GlobalQueue;

    alignas(64) std::atomic<size_t> m_PendingJobs{ 0 };
    alignas(64) std::atomic<uint32_t> m_WaitingWorkers{ 0 };

    alignas(64) mutable std::mutex m_WakeMutex;
    std::condition_variable m_WakeCV;
};

} // namespace Engine

#endif // CORE_JOBSYSTEM_H