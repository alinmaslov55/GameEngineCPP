#ifndef CORE_JOBHANDLE_H
#define CORE_JOBHANDLE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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

} // namespace Engine

#endif // CORE_JOBHANDLE_H
