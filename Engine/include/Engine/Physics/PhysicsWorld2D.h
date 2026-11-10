#ifndef PHYSICS_PHYSICSWORLD2D_H
#define PHYSICS_PHYSICSWORLD2D_H

#include "Engine/Physics/PhysicsHandles.h"
#include "Engine/Core/ThreadAffinity.h"
#include <box2d/types.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <unordered_map>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Engine {

class Scene;
class GameObject;
class Transform;
class Rigidbody2D;
class BoxCollider2D;
class Collider2D;
class Tilemap;
struct RaycastHitXZ;

/// @brief Dedicated 2.5D physics subsystem managing Box2D v3 simulation,
/// multithreaded worker task pool, 2D spatial hash grid, elevation arc integration,
/// body registration, and spatial queries.
class PhysicsWorld2D {
public:
    struct alignas(64) Box2DTask {
        std::atomic<int> remaining{ 0 };
        std::atomic<bool> done{ false };
        std::atomic<bool> inUse{ false };

        Box2DTask() = default;
        Box2DTask(const Box2DTask&) = delete;
        Box2DTask& operator=(const Box2DTask&) = delete;
        Box2DTask(Box2DTask&&) = delete;
        Box2DTask& operator=(Box2DTask&&) = delete;
    };

    static constexpr size_t kMaxBox2DTasks = 16;
    static constexpr float kSpatialCellSize = 2.0f;

    [[nodiscard]] static constexpr uint64_t GetCellKey(int32_t cx, int32_t cz) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(cz));
    }

    explicit PhysicsWorld2D(Scene* scene) noexcept;
    ~PhysicsWorld2D();

    PhysicsWorld2D(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D(PhysicsWorld2D&&) noexcept = default;
    PhysicsWorld2D& operator=(PhysicsWorld2D&&) noexcept = default;

    // Simulation lifecycle
    void OnRuntimeStart();
    void OnRuntimeStop();
    void UpdateRuntime(float deltaTime);
    void UpdateNonRuntime(float deltaTime);

    [[nodiscard]] bool IsRuntimeRunning() const noexcept { return m_IsRuntimeRunning; }
    [[nodiscard]] uint32_t GetPhysicsWorldId() const noexcept { return m_PhysicsWorldId; }
    [[nodiscard]] float GetPhysicsAlpha() const noexcept { return m_PhysicsAlpha; }
    void ResetPhysicsAccumulator() noexcept { m_PhysicsAccumulator = 0.0f; m_PhysicsAlpha = 0.0f; }

    // Body registration & synchronization
    void RegisterRuntimeBody(GameObject* go);
    void UnregisterRuntimeBody(GameObject* go);
    void DisableRuntimeBody(GameObject* go);
    void MarkPhysicsDirty(GameObject* go);
    [[nodiscard]] size_t GetPendingPhysicsSyncCount() const noexcept { return m_PendingPhysicsSync.size(); }
    [[nodiscard]] size_t GetRegisteredPhysicsBodyCount() const noexcept { return m_PhysicsBodies.size(); }

    // Spatial queries (PHY-05)
    void OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    void OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    bool RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const;

    // Task and spatial grid diagnostics
    [[nodiscard]] uint64_t GetBox2DParallelTasksDispatched() const noexcept { return m_Box2DParallelTasksDispatched; }
    [[nodiscard]] uint64_t GetBox2DSerialTasksExecuted() const noexcept { return m_Box2DSerialTasksExecuted; }
    [[nodiscard]] uint64_t GetBox2DTotalTasks() const noexcept { return m_Box2DParallelTasksDispatched + m_Box2DSerialTasksExecuted; }
    [[nodiscard]] const Box2DTask* GetBox2DTaskSlots() const noexcept { return m_Box2DTasks.data(); }
    [[nodiscard]] size_t GetBox2DTaskSlotCount() const noexcept { return kMaxBox2DTasks; }

    void InvalidateSpatialGrid() noexcept { m_StaticCollidersDirty = true; }
    [[nodiscard]] bool IsSpatialGridDirty() const noexcept { return m_StaticCollidersDirty; }
    [[nodiscard]] size_t GetStaticGridCellCount() const noexcept { return m_StaticGrid.size(); }
    [[nodiscard]] size_t GetStaticColliderCount() const noexcept { return m_StaticColliders.size(); }
    [[nodiscard]] size_t GetContactedPairsScratchCapacity() const noexcept { return m_ContactedPairsScratch.capacity(); }

    // Debug owner-thread affinity enforcement
    void AssertMainThreadAffinity([[maybe_unused]] const char* operation) const noexcept {
#ifndef NDEBUG
        Detail::AssertThreadAffinity(m_OwnerThreadId, operation);
#endif
    }
    [[nodiscard]] bool IsOnOwnerThread() const noexcept {
#ifndef NDEBUG
        return std::this_thread::get_id() == m_OwnerThreadId;
#else
        return true;
#endif
    }

private:
    static void* Box2DEnqueueTask(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext);
    static void Box2DFinishTask(void* userTask, void* userContext);

    void CreateRuntimePhysicsBody(GameObject* go);
    void CreateRuntimeTilemapBody(GameObject* go);
    void UpsertPhysicsBodyEntry(GameObject* go, Transform* transform, Rigidbody2D* rb2d, BodyHandle handle);
    void RemovePhysicsBodyEntry(GameObject* go);
    void SyncMovedBodyTransform(const b2BodyMoveEvent& ev);
    void IntegrateVerticalElevation(float fixedDt);
    void ProcessPhysicsEvents();
    void ApplyAirbornePassThroughFilter(GameObject* go, bool airborne);

    void UpdateCollidersAndGravity(float deltaTime);
    void ResolveEntityCollisions();
    void ResolveTilemapCollisions();

private:
    struct PhysicsBodyEntry {
        GameObject* gameObject = nullptr;
        Transform* transform = nullptr;
        Rigidbody2D* rigidbody = nullptr;
        BodyHandle bodyHandle{};
        int8_t pendingAirborneTransition = 0; // 0 = none, +1 = entered airborne, -1 = touched down
        bool pendingTransformDirty = false;
    };

    static void EvaluateVerticalEntry(PhysicsBodyEntry& entry, float fixedDt, bool evaluateWorldY) noexcept;

    struct ColliderCacheEntry {
        GameObject* gameObject = nullptr;
        Transform* transform = nullptr;
        BoxCollider2D* collider = nullptr;
        Transform* cachedParent = nullptr;
        glm::vec3 cachedPosition{ 0.0f };
        glm::vec3 cachedScale{ 1.0f };
        glm::vec2 cachedSize{ 0.0f };
        glm::vec2 cachedOffset{ 0.0f };
        glm::vec3 cachedWorldPosition{ 0.0f };
    };

    struct ShapePairKey {
        uint64_t a = 0;
        uint64_t b = 0;
        bool operator==(const ShapePairKey&) const noexcept = default;
    };

#ifndef NDEBUG
    std::thread::id m_OwnerThreadId{ std::this_thread::get_id() };
#endif
    Scene* m_Scene{ nullptr };
    bool m_IsRuntimeRunning{ false };
    uint32_t m_PhysicsWorldId{ 0 };
    float m_PhysicsAccumulator{ 0.0f };
    float m_PhysicsAlpha{ 0.0f };
    std::vector<GameObject*> m_PendingPhysicsSync;
    std::vector<PhysicsBodyEntry> m_PhysicsBodies;
    std::unordered_map<GameObject*, size_t> m_PhysicsBodyIndices;
    std::vector<ShapePairKey> m_ContactedPairsScratch;

    // Box2D v3 multithreaded task management pool (zero heap allocation)
    alignas(64) std::array<Box2DTask, kMaxBox2DTasks> m_Box2DTasks;
    uint64_t m_Box2DParallelTasksDispatched{ 0 };
    uint64_t m_Box2DSerialTasksExecuted{ 0 };

    // 2D Spatial Hash Grid for static colliders and foliage interactors
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_StaticGrid;
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_FoliageInteractorGrid;
    std::vector<uint32_t> m_StaticTestedStamp;
    uint32_t m_SpatialQueryStamp{ 0 };
    bool m_StaticCollidersDirty{ true };

    // Fallback collision scratch buffers
    std::vector<ColliderCacheEntry> m_DynamicColliders;
    std::vector<ColliderCacheEntry> m_StaticColliders;
    std::vector<GameObject*> m_TilemapScratch;
};

} // namespace Engine

#endif // PHYSICS_PHYSICSWORLD2D_H
