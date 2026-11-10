#ifndef RENDERER_SCENERENDERER_H
#define RENDERER_SCENERENDERER_H

#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Scene/StandeeCommand.h"

#include <vector>
#include <utility>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Engine {

class Scene;
class GameObject;
class Transform;
class DirectionalLightComponent;
class PostProcessComponent;
class WindComponent;
class SpriteRenderer;
class TerrainComponent;
class Tilemap;
class ParticleSystem;
class UIPanel;
class UILabel;

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

/// @brief Dedicated 2.5D Diorama rendering pipeline subsystem managing camera viewport,
/// multi-pass rendering (ShadowMap, GroundPlane, CutoutStandees, Particles, ScreenUI),
/// and zero-allocation hot-path scratch buffers.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer() = default;

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    SceneRenderer(SceneRenderer&&) noexcept = default;
    SceneRenderer& operator=(SceneRenderer&&) noexcept = default;

    void Render(Scene& scene, const PerspectiveCamera* overrideCamera = nullptr);
    void OnViewportResize(Scene& scene, uint32_t width, uint32_t height);

    [[nodiscard]] PerspectiveCamera& GetCamera() noexcept { return m_Camera; }
    [[nodiscard]] const PerspectiveCamera& GetCamera() const noexcept { return m_Camera; }
    [[nodiscard]] const PerspectiveCamera& GetActiveCamera(const Scene& scene) const;

    [[nodiscard]] float GetOrthographicSize() const noexcept { return m_OrthographicSize; }
    void SetOrthographicSize(float size) noexcept { m_OrthographicSize = size; }

    [[nodiscard]] uint32_t GetViewportWidth() const noexcept { return m_ViewportWidth; }
    [[nodiscard]] uint32_t GetViewportHeight() const noexcept { return m_ViewportHeight; }

    SceneRenderContext GatherRenderContext(Scene& scene, const PerspectiveCamera* overrideCamera = nullptr);

private:
    struct PointLightCandidate {
        PointLightUniform Uniform{};
        bool CastShadows = false;
        uint8_t Priority = 0;
        float DistToFocalSq = 0.0f;
        uint32_t SubmissionOrder = 0;
    };

    void RenderPassShadowMap(SceneRenderContext& ctx);
    void RenderPassGroundPlane(SceneRenderContext& ctx);
    void RenderPassCutoutStandees(SceneRenderContext& ctx);
    void RenderPassParticles(SceneRenderContext& ctx);
    void RenderPassScreenUI();

private:
    PerspectiveCamera m_Camera;
    uint32_t m_ViewportWidth{ 1280 };
    uint32_t m_ViewportHeight{ 720 };
    float m_OrthographicSize{ 5.0f };

    // Hot-path scratch buffers to prevent per-frame dynamic heap allocations
    std::vector<StandeeRenderCommand> m_StandeeCommands;
    std::vector<StandeeRenderCommand> m_ShadowStandeeCommands;
    std::vector<PointLightCandidate> m_PointLightScratch;
    std::vector<std::pair<GameObject*, glm::vec3>> m_FoliageScratch;
    std::vector<std::pair<TerrainComponent*, Transform*>> m_TerrainScratch;
    std::vector<std::pair<Tilemap*, Transform*>> m_TilemapRenderScratch;
    std::vector<std::pair<SpriteRenderer*, Transform*>> m_FlatDecalScratch;
    std::vector<ParticleSystem*> m_ParticleScratch;
    std::vector<UIPanel*> m_UIPanelScratch;
    std::vector<UILabel*> m_UILabelScratch;
};

} // namespace Engine

#endif // RENDERER_SCENERENDERER_H
