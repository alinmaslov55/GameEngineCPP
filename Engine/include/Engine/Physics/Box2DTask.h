#ifndef PHYSICS_BOX2DTASK_H
#define PHYSICS_BOX2DTASK_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Engine {

inline constexpr size_t kMaxChunksPerBox2DTask = 64;
inline constexpr size_t kMaxBox2DTasks = 16;

/// @brief Cache-line-isolated Box2D worker task slot shared cleanly by PhysicsWorld2D and Scene diagnostics.
struct alignas(64) Box2DTask {
    using TaskCallbackFn = void(int startIndex, int endIndex, uint32_t workerIndex, void* taskContext);

    struct ChunkPacket {
        TaskCallbackFn* task = nullptr;
        int startIndex = 0;
        int endIndex = 0;
        void* taskContext = nullptr;
        Box2DTask* parentTask = nullptr;
        uint32_t workerLimit = 0;
    };

    std::atomic<int> remaining{ 0 };
    std::atomic<bool> done{ false };
    std::atomic<bool> inUse{ false };
    uint32_t taskIndex{ 0 };
    ChunkPacket* chunks{ nullptr };

    Box2DTask() = default;
    Box2DTask(const Box2DTask&) = delete;
    Box2DTask& operator=(const Box2DTask&) = delete;
    Box2DTask(Box2DTask&&) = delete;
    Box2DTask& operator=(Box2DTask&&) = delete;
};

static_assert(sizeof(Box2DTask) == 64, "Box2DTask size must equal 64 bytes for cache isolation");

} // namespace Engine

#endif // PHYSICS_BOX2DTASK_H
