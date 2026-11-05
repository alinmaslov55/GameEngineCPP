#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"

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

struct StandeeRenderCommand {
    const Transform* transform = nullptr;
    const SpriteRenderer* sprite = nullptr;

    glm::vec2 size{ 1.0f };
    float rotationZ = 0.0f;
    float windWeight = 0.0f;
    float spatialPhase = 0.0f;
    float camProximityBend = 0.0f;
    float shadowProximityBend = 0.0f;

    float alphaCutoff = 0.15f;
    float emissive = -1.0f;
    float receiverOffset = 0.48f;
    glm::vec4 color{ 1.0f };

    float viewDepth = 0.0f;
    glm::vec3 boundsCenter{ 0.0f };
    float boundsRadius = 0.0f;
};

struct SceneRenderContext {
    const PerspectiveCamera* activeCam = nullptr;
    SceneLightingData lighting;
    const DirectionalLightComponent* dirLight = nullptr;
    const PostProcessComponent* postProcess = nullptr;
    const WindComponent* windComp = nullptr;
    float totalTime = 0.0f;
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

    void Update(float deltaTime);
    void Render(const PerspectiveCamera* overrideCamera = nullptr);
    void OnViewportResize(uint32_t width, uint32_t height);

    // Box2D physics simulation lifecycle
    void OnRuntimeStart();
    void OnRuntimeStop();
    void UpdateRuntime(float deltaTime);
    [[nodiscard]] bool IsRuntimeRunning() const noexcept { return m_IsRuntimeRunning; }

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

private:
    void UpdateCollidersAndGravity(float deltaTime);
    void ResolveEntityCollisions();
    void ResolveTilemapCollisions();

    SceneRenderContext GatherRenderContext(const PerspectiveCamera* overrideCamera);
    void RenderPassShadowMap(SceneRenderContext& ctx);
    void RenderPassGroundPlane(SceneRenderContext& ctx);
    void RenderPassCutoutStandees(SceneRenderContext& ctx);
    void RenderPassParticles(SceneRenderContext& ctx);
    void RenderPassScreenUI();

private:
    struct ColliderCacheEntry {
        GameObject* gameObject = nullptr;
        Transform* transform = nullptr;
        BoxCollider2D* collider = nullptr;
    };

    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    PerspectiveCamera m_Camera;
    uint32_t m_ViewportWidth = 1280;
    uint32_t m_ViewportHeight = 720;
    float m_OrthographicSize = 5.0f;

    bool m_IsRuntimeRunning = false;
    uint32_t m_PhysicsWorldId = 0;

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
