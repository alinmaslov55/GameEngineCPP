#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Scene/StandeeCommand.h"

#include <box2d/types.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <unordered_map>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Engine {

class BoxCollider2D;
class DirectionalLightComponent;
class PostProcessComponent;
class WindComponent;
class SpriteRenderer;
class TerrainComponent;
class Tilemap;
class ParticleSystem;
class UIPanel;
class UILabel;
class Prefab;
struct RaycastHitXZ;

struct SceneRenderContext {
    const PerspectiveCamera* activeCam = nullptr;
    SceneLightingData lighting;
    const DirectionalLightComponent* dirLight = nullptr;
    const PostProcessComponent* postProcess = nullptr;
    const WindComponent* windComp = nullptr;
    float totalTime = 0.0f;
    float physicsAlpha = 0.0f;
    bool useHDRPostProcess = false;

    SceneWindData BuildWindUniforms(const glm::vec3& swayRightAxis) const;
};

class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();

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

    // High-performance object pooling
    void PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count);
    GameObject* AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos = glm::vec3(0.0f));
    void ReleaseToPool(const std::string& poolKey, GameObject* instance);
    void ReleaseToPool(GameObject* instance); // Uses instance->GetPoolKey()

    // Runtime Box2D Body Registration & Lifecycle Synchronization
    void RegisterRuntimeBody(GameObject* go);
    void UnregisterRuntimeBody(GameObject* go);
    void MarkPhysicsDirty(GameObject* go);
    [[nodiscard]] size_t GetPendingPhysicsSyncCount() const noexcept { return m_PendingPhysicsSync.size(); }

    [[nodiscard]] size_t GetPoolFreeCount(const std::string& poolKey) const noexcept;
    [[nodiscard]] size_t GetPoolTotalCount(const std::string& poolKey) const noexcept;
    void ClearPool(const std::string& poolKey);
    void ClearAllPools();

    void Update(float deltaTime);
    void Render(const PerspectiveCamera* overrideCamera = nullptr);
    void OnViewportResize(uint32_t width, uint32_t height);

    // Box2D physics simulation lifecycle
    void OnRuntimeStart();
    void OnRuntimeStop();
    void UpdateRuntime(float deltaTime);
    [[nodiscard]] bool IsRuntimeRunning() const noexcept { return m_IsRuntimeRunning; }
    [[nodiscard]] uint32_t GetPhysicsWorldId() const noexcept { return m_PhysicsWorldId; }

    // Spatial Physics Queries (PHY-05)
    void OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    void OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const;
    bool RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const;

    [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    [[nodiscard]] PerspectiveCamera& GetCamera() noexcept { return m_Camera; }
    [[nodiscard]] const PerspectiveCamera& GetCamera() const noexcept { return m_Camera; }
    [[nodiscard]] const PerspectiveCamera& GetActiveCamera() const;

    [[nodiscard]] float GetOrthographicSize() const noexcept { return m_OrthographicSize; }
    void SetOrthographicSize(float size);

    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const noexcept { return m_GameObjects; }

    static constexpr float kSpatialCellSize = 2.0f;

    [[nodiscard]] static constexpr uint64_t GetCellKey(int32_t cx, int32_t cz) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(cz));
    }

    void InvalidateSpatialGrid() noexcept { m_StaticCollidersDirty = true; }
    [[nodiscard]] bool IsSpatialGridDirty() const noexcept { return m_StaticCollidersDirty; }
    [[nodiscard]] size_t GetStaticGridCellCount() const noexcept { return m_StaticGrid.size(); }
    [[nodiscard]] size_t GetStaticColliderCount() const noexcept { return m_StaticColliders.size(); }

    [[nodiscard]] uint64_t GetBox2DParallelTasksDispatched() const noexcept { return m_Box2DParallelTasksDispatched; }
    [[nodiscard]] uint64_t GetBox2DSerialTasksExecuted() const noexcept { return m_Box2DSerialTasksExecuted; }
    [[nodiscard]] uint64_t GetBox2DTotalTasks() const noexcept { return m_Box2DParallelTasksDispatched + m_Box2DSerialTasksExecuted; }
    [[nodiscard]] float GetPhysicsAlpha() const noexcept { return m_PhysicsAlpha; }
    void ResetPhysicsAccumulator() noexcept { m_PhysicsAccumulator = 0.0f; m_PhysicsAlpha = 0.0f; }

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
    [[nodiscard]] const Box2DTask* GetBox2DTaskSlots() const noexcept { return m_Box2DTasks.data(); }
    [[nodiscard]] size_t GetBox2DTaskSlotCount() const noexcept { return kMaxBox2DTasks; }

private:
    static void* Box2DEnqueueTask(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext);
    static void Box2DFinishTask(void* userTask, void* userContext);

    void UpdateCollidersAndGravity(float deltaTime);
    void ResolveEntityCollisions();
    void ResolveTilemapCollisions();

    SceneRenderContext GatherRenderContext(const PerspectiveCamera* overrideCamera);
    void RenderPassShadowMap(SceneRenderContext& ctx);
    void RenderPassGroundPlane(SceneRenderContext& ctx);
    void RenderPassCutoutStandees(SceneRenderContext& ctx);
    void RenderPassParticles(SceneRenderContext& ctx);
    void RenderPassScreenUI();

    void CreateRuntimePhysicsBody(GameObject* go);
    void CreateRuntimeTilemapBody(GameObject* go);
    void IntegrateVerticalElevation(float fixedDt);
    void ProcessPhysicsEvents();
    void ApplyAirbornePassThroughFilter(GameObject* go, bool airborne);

private:
    struct EntityPool {
        std::vector<GameObject*> freeList;
        std::vector<GameObject*> allInstances;
    };
    std::unordered_map<std::string, EntityPool> m_EntityPools;

    struct ColliderCacheEntry {
        GameObject* gameObject = nullptr;
        Transform* transform = nullptr;
        BoxCollider2D* collider = nullptr;
    };

    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    std::vector<std::unique_ptr<GameObject>> m_PendingSpawnQueue;
    std::vector<GameObject*> m_PendingDestroyQueue;
    std::unordered_map<uint64_t, GameObject*> m_EntityUUIDMap;
    uint32_t m_IterationDepth = 0;
    PerspectiveCamera m_Camera;
    uint32_t m_ViewportWidth = 1280;
    uint32_t m_ViewportHeight = 720;
    float m_OrthographicSize = 5.0f;

    bool m_IsRuntimeRunning = false;
    uint32_t m_PhysicsWorldId = 0;
    float m_PhysicsAccumulator = 0.0f;
    float m_PhysicsAlpha = 0.0f;
    std::vector<GameObject*> m_PendingPhysicsSync;

    // Box2D v3 multithreaded task management pool (zero heap allocation)
    alignas(64) std::array<Box2DTask, kMaxBox2DTasks> m_Box2DTasks;
    uint64_t m_Box2DParallelTasksDispatched = 0;
    uint64_t m_Box2DSerialTasksExecuted = 0;

    // 2D Spatial Hash Grid for static colliders and foliage interactors
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_StaticGrid;
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_FoliageInteractorGrid;
    std::vector<uint32_t> m_StaticTestedStamp;
    uint32_t m_SpatialQueryStamp = 0;
    bool m_StaticCollidersDirty = true;

    // Hot-path scratch buffers to prevent per-frame dynamic heap allocations
    std::vector<StandeeRenderCommand> m_StandeeCommands;
    std::vector<StandeeRenderCommand> m_ShadowStandeeCommands;
    std::vector<std::pair<GameObject*, glm::vec3>> m_FoliageScratch;
    std::vector<ColliderCacheEntry> m_DynamicColliders;
    std::vector<ColliderCacheEntry> m_StaticColliders;
    std::vector<GameObject*> m_TilemapScratch;
    std::vector<std::pair<TerrainComponent*, Transform*>> m_TerrainScratch;
    std::vector<std::pair<Tilemap*, Transform*>> m_TilemapRenderScratch;
    std::vector<std::pair<SpriteRenderer*, Transform*>> m_FlatDecalScratch;
    std::vector<ParticleSystem*> m_ParticleScratch;
    std::vector<UIPanel*> m_UIPanelScratch;
    std::vector<UILabel*> m_UILabelScratch;
};

} // namespace Engine

#endif // SCENE_SCENE_H
