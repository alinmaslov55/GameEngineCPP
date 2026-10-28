#ifndef RENDERER_TERRAINRENDERER_H
#define RENDERER_TERRAINRENDERER_H

#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Scene/TerrainComponent.h"

#include <glm/glm.hpp>

namespace Engine {

class TerrainRenderer {
public:
    static void Init();
    static void Shutdown();

    static void SetBrushPreview(bool enabled,
                                const glm::vec2& worldCenterXZ = { 0.0f, 0.0f },
                                float radiusWorld = 1.0f,
                                float falloff = 0.65f);
    static void ClearBrushPreview();

    static void RenderTerrain(TerrainComponent& terrain,
                              const glm::vec3& origin,
                              const glm::vec3& scale,
                              const PerspectiveCamera& camera,
                              const SceneLightingData* lighting = nullptr);
};

} // namespace Engine

#endif // RENDERER_TERRAINRENDERER_H
