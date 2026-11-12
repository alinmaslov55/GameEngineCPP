#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/SurfaceSample.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Core/ThreadAffinity.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <utility>
#include <unordered_map>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Engine {

class PhysicsWorld2D;
class EntityPoolManager;
class SceneRenderer;
class Prefab;
struct RaycastHitXZ;

/// @brief CPU illumination query result evaluated on the XZ gameplay plane (LGT-03).
struct IlluminationSample {
    float SafeIntensity = 0.0f;               // Sum of SafeLifeline smoothstep attenuations at worldXZ
    float HazardIntensity = 0.0f;             // Sum of DeceptiveHazard attenuations
    float PrimaryTorchNormalizedDist = 999.0f; // distXZ / TorchRadius (< 1.0 = inside torch circle)
    GameObject* DominantSafeSource = nullptr; // Highest-contributing SafeLifeline entity at worldXZ
    float NeutralIntensity = 0.0f;            // Sum of NeutralShrine attenuations

    [[nodiscard]] bool IsInSafeLight(float threshold = 0.05f) const noexcept {
        return SafeIntensity >= threshold;
    }

    [[nodiscard]] bool IsInSearingRim(float innerRatio = 0.82f) const noexcept {
        return PrimaryTorchNormalizedDist >= innerRatio && PrimaryTorchNormalizedDist <= 1.02f;
    }
};

/// @brief Optional modifiers for detailed CPU illumination queries (flicker & line-of-light occlusion).
struct IlluminationQueryOptions {
    bool CheckLineOfLight = false;           // Casts XZ ray against OccluderMaskBits via PhysicsQueries
    uint64_t OccluderMaskBits = (1ULL << 1); // Matches PhysicsLayer::Environment
    bool IncludeFlicker = false;             // Scales by EvaluateEffectiveIntensity(Time::GetTotalTime(), uuid)
};

/// @brief Core Scene orchestrator coordinating entity lifecycle (ECS), UUID indexing,
/// and delegating physics, object pooling, and rendering pipelines to cohesive subsystems.
class Scene {
public:
    explicit Scene(const std::string& name = "Untitled Scene");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept;
    Scene& operator=(Scene&&) noexcept;

    // Subsystem accessors (SRP & DIP)
    [[nodiscard]] PhysicsWorld2D& GetPhysicsWorld() noexcept;
    [[nodiscard]] const PhysicsWorld2D& GetPhysicsWorld() const noexcept;
    [[nodiscard]] EntityPoolManager& GetPoolManager() noexcept;
    [[nodiscard]] const EntityPoolManager& GetPoolManager() const noexcept;
    [[nodiscard]] SceneRenderer& GetRenderer() noexcept;
    [[nodiscard]] const SceneRenderer& GetRenderer() const noexcept;

    // Entity lifecycle and UUID lookup
    GameObject* CreateGameObject(const std::string& name);
    GameObject* CreateGameObjectWithUUID(uint64_t uuid, const std::string& name);
    void DestroyGameObject(GameObject* gameObject);
    void FlushEntityLifecycleQueues();
    [[nodiscard]] GameObject* FindGameObjectByUUID(uint64_t uuid) const noexcept;
    void FindGameObjectsWithTag(EntityTag tag, std::vector<GameObject*>& out) const;
    [[nodiscard]] std::vector<GameObject*> FindGameObjectsWithTag(EntityTag tag) const;
    [[nodiscard]] GameObject* FindFirstGameObjectWithTag(EntityTag tag) const noexcept;
    void FindGameObjectsInLayer(uint32_t layerMask, std::vector<GameObject*>& out) const;
    [[nodiscard]] std::vector<GameObject*> FindGameObjectsInLayer(uint32_t layerMask) const;
    [[nodiscard]] size_t GetEntityUUIDMapCount() const noexcept { return m_EntityUUIDMap.size(); }
    [[nodiscard]] size_t GetTempChildrenScratchCapacity() const noexcept { return m_TempChildrenScratch.capacity(); }
    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const noexcept { return m_GameObjects; }

    // Iteration safety guard
    [[nodiscard]] bool IsIteratingEntities() const noexcept { return m_IterationDepth > 0; }
    void SetIsIteratingEntities(bool iterating) noexcept { m_IterationDepth = iterating ? 1 : 0; }
    [[nodiscard]] uint32_t GetIterationDepth() const noexcept { return m_IterationDepth; }
    void BeginIteration() noexcept { ++m_IterationDepth; }
    void EndIteration();

    class ScopedIterationGuard {
    public:
        explicit ScopedIterationGuard(Scene& scene) noexcept : m_Scene(scene) {
            m_Scene.BeginIteration();
        }
        ~ScopedIterationGuard() {
            m_Scene.EndIteration();
        }
        ScopedIterationGuard(const ScopedIterationGuard&) = delete;
        ScopedIterationGuard& operator=(const ScopedIterationGuard&) = delete;
        ScopedIterationGuard(ScopedIterationGuard&&) = delete;
        ScopedIterationGuard& operator=(ScopedIterationGuard&&) = delete;

    private:
        Scene& m_Scene;
    };

    // Prefab instantiation with recursive UUID remapping and world positioning
    GameObject* InstantiatePrefab(const Prefab& prefab, const glm::vec3& worldPos = glm::vec3(0.0f));

    // Facade: Object Pooling (delegates to EntityPoolManager)
    void PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count);
    GameObject* AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos = glm::vec3(0.0f));
    void ReleaseToPool(const std::string& poolKey, GameObject* instance);
    void ReleaseToPool(GameObject* instance);
    void PropagatePoolAcquire(GameObject* root);
    void PropagatePoolRelease(GameObject* root);
    [[nodiscard]] size_t GetPoolFreeCount(const std::string& poolKey) const noexcept;
    [[nodiscard]] size_t GetPoolTotalCount(const std::string& poolKey) const noexcept;
    void ClearPool(const std::string& poolKey);
    void ClearAllPools();

    // Lifecycle loop
    void Update(float deltaTime);
    void Render(const PerspectiveCamera* overrideCamera = nullptr);
    void OnViewportResize(uint32_t width, uint32_t height);

    // Facade: Physics simulation lifecycle & queries (delegates to PhysicsWorld2D)
    void OnRuntimeStart();
    void OnRuntimeStop();
    void UpdateRuntime(float deltaTime);
    [[nodiscard]] bool IsRuntimeRunning() const noexcept;
    [[nodiscard]] uint32_t GetPhysicsWorldId() const noexcept;
    [[nodiscard]] float GetPhysicsAlpha() const noexcept;
    void ResetPhysicsAccumulator() noexcept;

    void RegisterRuntimeBody(GameObject* go);
    void UnregisterRuntimeBody(GameObject* go);
    void MarkPhysicsDirty(GameObject* go);
    [[nodiscard]] size_t GetPendingPhysicsSyncCount() const noexcept;

    void OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    void OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    bool RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const;

    // CPU Illumination & Umbral Visibility Queries (LGT-03)
    [[nodiscard]] IlluminationSample EvaluateIlluminationAtXZ(glm::vec2 worldXZ) const noexcept;
    [[nodiscard]] IlluminationSample EvaluateIlluminationDetailedXZ(glm::vec2 worldXZ,
                                                                    const IlluminationQueryOptions& options) const noexcept;

    // CPU Surface & Fire Simulation Queries
    [[nodiscard]] SurfaceSample SampleSurfaceAtXZ(float worldX, float worldZ) const noexcept;
    void PaintMaterialWorld(const glm::vec2& centerXZ, uint32_t layerIdx, float radius, float strength = 1.0f);
    void IgniteAtXZ(const glm::vec2& centerXZ, float radius);
    void ExtinguishAtXZ(const glm::vec2& centerXZ, float radius);

    // Facade: Camera & Viewport (delegates to SceneRenderer)
    [[nodiscard]] PerspectiveCamera& GetCamera() noexcept;
    [[nodiscard]] const PerspectiveCamera& GetCamera() const noexcept;
    [[nodiscard]] const PerspectiveCamera& GetActiveCamera() const;
    [[nodiscard]] float GetOrthographicSize() const noexcept;
    void SetOrthographicSize(float size);

    // Spatial hash grid facade
    static constexpr float kSpatialCellSize = 2.0f;
    [[nodiscard]] static constexpr uint64_t GetCellKey(int32_t cx, int32_t cz) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(cz));
    }
    void InvalidateSpatialGrid() noexcept;
    [[nodiscard]] bool IsSpatialGridDirty() const noexcept;
    [[nodiscard]] size_t GetStaticGridCellCount() const noexcept;
    [[nodiscard]] size_t GetStaticColliderCount() const noexcept;

    // Box2D task pool diagnostics facade
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
    [[nodiscard]] uint64_t GetBox2DParallelTasksDispatched() const noexcept;
    [[nodiscard]] uint64_t GetBox2DSerialTasksExecuted() const noexcept;
    [[nodiscard]] uint64_t GetBox2DTotalTasks() const noexcept;
    [[nodiscard]] const Box2DTask* GetBox2DTaskSlots() const noexcept;
    [[nodiscard]] size_t GetBox2DTaskSlotCount() const noexcept;

    // Metadata
    [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

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
#ifndef NDEBUG
    std::thread::id m_OwnerThreadId{ std::this_thread::get_id() };
#endif
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    std::vector<std::unique_ptr<GameObject>> m_PendingSpawnQueue;
    std::vector<GameObject*> m_PendingDestroyQueue;
    std::vector<Transform*> m_TempChildrenScratch;
    std::unordered_map<uint64_t, GameObject*> m_EntityUUIDMap;
    uint32_t m_IterationDepth = 0;

    // Subsystems
    std::unique_ptr<PhysicsWorld2D> m_PhysicsWorld;
    std::unique_ptr<EntityPoolManager> m_PoolManager;
    std::unique_ptr<SceneRenderer> m_Renderer;
};

} // namespace Engine

#endif // SCENE_SCENE_H
