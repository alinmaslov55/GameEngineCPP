#ifndef CORE_JOBSYSTEM_H
#define CORE_JOBSYSTEM_H

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

class JobSystem;

struct JobState {
    std::atomic<bool> isFinished{ false };
    std::atomic<uint32_t> pendingDependencies{ 0 };
    std::function<void()> jobFunction;
    std::vector<std::shared_ptr<JobState>> continuations;
    std::mutex continuationMutex;
};

struct QueuedJob {
    using RawCallback = void (*)(void*);

    RawCallback callback = nullptr;
    void* userData = nullptr;
    std::shared_ptr<JobState> retainedState;

    [[nodiscard]] bool IsValid() const noexcept {
        return callback != nullptr || retainedState != nullptr;
    }

    void Execute();
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

    // ParallelFor: Distributes [start, end) index iterations across worker threads without per-chunk heap allocation
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType>
    static void ParallelFor(IndexType start, IndexType end, Func&& func, size_t chunkSize = 0) {
        ParallelForImpl<IndexType, Func, false>(start, end, std::forward<Func>(func), chunkSize);
    }

    // ParallelFor overload for count [0, count)
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType>
    static void ParallelFor(IndexType count, Func&& func, size_t chunkSize = 0) {
        ParallelFor(static_cast<IndexType>(0), count, std::forward<Func>(func), chunkSize);
    }

    // ParallelForBatched: Distributes [start, end) ranges as batches [batchStart, batchEnd) without per-chunk heap allocation
    template <typename IndexType, typename Func>
    requires std::integral<IndexType> && std::invocable<Func, IndexType, IndexType>
    static void ParallelForBatched(IndexType start, IndexType end, Func&& func, size_t chunkSize = 0) {
        ParallelForImpl<IndexType, Func, true>(start, end, std::forward<Func>(func), chunkSize);
    }

    JobSystem(uint32_t workerCount);
    ~JobSystem();

    class JobRingBuffer {
    public:
        JobRingBuffer() : m_Buffer(128) {}

        void PushBack(QueuedJob job) {
            if (m_Size == m_Buffer.size()) {
                Grow();
            }
            const size_t mask = m_Buffer.size() - 1;
            m_Buffer[(m_Head + m_Size) & mask] = std::move(job);
            ++m_Size;
        }

        bool PopBack(QueuedJob& outJob) noexcept {
            if (m_Size == 0) return false;
            --m_Size;
            const size_t mask = m_Buffer.size() - 1;
            const size_t tail = (m_Head + m_Size) & mask;
            outJob = std::move(m_Buffer[tail]);
            m_Buffer[tail] = QueuedJob{};
            return true;
        }

        bool PopFront(QueuedJob& outJob) noexcept {
            if (m_Size == 0) return false;
            const size_t mask = m_Buffer.size() - 1;
            outJob = std::move(m_Buffer[m_Head]);
            m_Buffer[m_Head] = QueuedJob{};
            m_Head = (m_Head + 1) & mask;
            --m_Size;
            return true;
        }

        [[nodiscard]] bool Empty() const noexcept { return m_Size == 0; }

    private:
        void Grow() {
            const size_t oldCap = m_Buffer.size();
            const size_t newCap = oldCap * 2;
            std::vector<QueuedJob> next(newCap);
            const size_t oldMask = oldCap - 1;
            for (size_t i = 0; i < m_Size; ++i) {
                next[i] = std::move(m_Buffer[(m_Head + i) & oldMask]);
            }
            m_Buffer = std::move(next);
            m_Head = 0;
        }

        std::vector<QueuedJob> m_Buffer;
        size_t m_Head = 0;
        size_t m_Size = 0;
    };

    class alignas(64) WorkerQueue {
    public:
        void Push(QueuedJob job) {
            std::lock_guard lock(m_Mutex);
            m_Ring.PushBack(std::move(job));
        }

        bool PopLocal(QueuedJob& outJob) {
            std::lock_guard lock(m_Mutex);
            return m_Ring.PopBack(outJob);
        }

        bool Steal(QueuedJob& outJob) {
            std::lock_guard lock(m_Mutex);
            return m_Ring.PopFront(outJob);
        }

        bool Empty() const {
            std::lock_guard lock(m_Mutex);
            return m_Ring.Empty();
        }

    private:
        mutable std::mutex m_Mutex;
        JobRingBuffer m_Ring;
    };

    class alignas(64) GlobalQueue {
    public:
        void Push(QueuedJob job) {
            std::lock_guard lock(m_Mutex);
            m_Ring.PushBack(std::move(job));
        }

        bool Pop(QueuedJob& outJob) {
            std::lock_guard lock(m_Mutex);
            return m_Ring.PopFront(outJob);
        }

        bool Empty() const {
            std::lock_guard lock(m_Mutex);
            return m_Ring.Empty();
        }

    private:
        mutable std::mutex m_Mutex;
        JobRingBuffer m_Ring;
    };

private:
    friend struct QueuedJob;

    template <typename IndexType, typename Func, bool IsBatched>
    static void ParallelForImpl(IndexType start, IndexType end, Func&& func, size_t chunkSize) {
        if (start >= end) {
            return;
        }

        const size_t totalItems = static_cast<size_t>(end - start);
        const uint32_t workers = GetWorkerCount();

        if (!IsInitialized() || workers == 0 || (!IsBatched && totalItems <= 1)) {
            if constexpr (IsBatched) {
                func(start, end);
            } else {
                for (IndexType i = start; i < end; ++i) {
                    func(i);
                }
            }
            return;
        }

        const size_t threadsToUse = std::max<size_t>(1, workers + 1);
        if (chunkSize == 0) {
            chunkSize = std::max<size_t>(1, totalItems / (threadsToUse * 4));
        }

        const size_t chunkCount = (totalItems + chunkSize - 1) / chunkSize;
        if (chunkCount <= 1) {
            if constexpr (IsBatched) {
                func(start, end);
            } else {
                for (IndexType i = start; i < end; ++i) {
                    func(i);
                }
            }
            return;
        }

        using CleanFunc = std::remove_reference_t<Func>;
        struct ParallelContext {
            explicit ParallelContext(ptrdiff_t count) : workDone(count) {}
            std::latch workDone;
            std::mutex exceptionMutex;
            std::exception_ptr firstException;

            void CaptureException() noexcept {
                std::lock_guard lock(exceptionMutex);
                if (!firstException) {
                    firstException = std::current_exception();
                }
            }
        };

        struct ParallelChunkPacket {
            CleanFunc* func = nullptr;
            ParallelContext* context = nullptr;
            IndexType start{};
            IndexType end{};
        };

        auto runner = [](void* raw) {
            auto* packet = static_cast<ParallelChunkPacket*>(raw);
            ParallelContext* ctx = packet->context;
            struct CountDownGuard {
                std::latch* l;
                ~CountDownGuard() noexcept {
                    if (l) {
                        l->count_down();
                    }
                }
            } countDownGuard{ &ctx->workDone };

            CleanFunc& fn = *(packet->func);
            const IndexType s = packet->start;
            const IndexType e = packet->end;
            try {
                if constexpr (IsBatched) {
                    fn(s, e);
                } else {
                    for (IndexType i = s; i < e; ++i) {
                        fn(i);
                    }
                }
            } catch (...) {
                ctx->CaptureException();
            }
        };

        ParallelContext ctx{ static_cast<ptrdiff_t>(chunkCount) };

        constexpr size_t kInlineChunkLimit = 64;
        std::array<ParallelChunkPacket, kInlineChunkLimit> inlinePackets{};
        std::vector<ParallelChunkPacket> overflowPackets;
        ParallelChunkPacket* packets = nullptr;
        if (chunkCount <= kInlineChunkLimit) {
            packets = inlinePackets.data();
        } else {
            overflowPackets.resize(chunkCount);
            packets = overflowPackets.data();
        }

        for (size_t c = 0; c < chunkCount; ++c) {
            const IndexType chunkStart = start + static_cast<IndexType>(c * chunkSize);
            const IndexType chunkEnd = std::min(end, chunkStart + static_cast<IndexType>(chunkSize));
            packets[c].func = &func;
            packets[c].context = &ctx;
            packets[c].start = chunkStart;
            packets[c].end = chunkEnd;
        }

        // Synchronize with all workers before stack packets and ParallelContext leave scope
        {
            struct LatchWaitGuard {
                std::latch& latch;
                ~LatchWaitGuard() {
                    while (!latch.try_wait()) {
                        if (!ExecuteOneJob()) {
                            latch.wait();
                            break;
                        }
                    }
                }
            } waitGuard{ ctx.workDone };

            size_t submitted = 0;
            try {
                for (; submitted < chunkCount; ++submitted) {
                    SubmitRawJob(+runner, &packets[submitted]);
                }
            } catch (...) {
                if (submitted < chunkCount) {
                    ctx.workDone.count_down(static_cast<ptrdiff_t>(chunkCount - submitted));
                }
                throw;
            }
        }

        if (ctx.firstException) {
            std::rethrow_exception(ctx.firstException);
        }
    }

    static void SubmitRawJob(QueuedJob::RawCallback callback, void* userData);
    void WorkerLoop(std::stop_token stopToken, uint32_t workerIndex);
    QueuedJob FindWork(uint32_t workerIndex);
    void SubmitJob(std::shared_ptr<JobState> job);
    void SubmitQueuedJob(QueuedJob&& job);
    static void SubmitOrExecuteJobState(std::shared_ptr<JobState> jobState);
    static void ExecuteJobState(const std::shared_ptr<JobState>& jobState);
    void DrainQueuedJobsOnShutdown() noexcept;
    bool HasPendingWork() const;

    bool PopWorkerJob(uint32_t index, QueuedJob& outJob);
    bool StealWorkerJob(uint32_t index, QueuedJob& outJob);
    bool PopGlobalJob(QueuedJob& outJob);

private:
    static std::unique_ptr<JobSystem> s_Instance;

    std::atomic<bool> m_Initialized{ false };
    std::atomic<bool> m_QueuesAlive{ false };
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
