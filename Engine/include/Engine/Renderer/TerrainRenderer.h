#ifndef RENDERER_TERRAINRENDERER_H
#define RENDERER_TERRAINRENDERER_H

#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"

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

    static void RenderChunkManager(TerrainChunkManager& chunkManager,
                                   float groundY,
                                   const PerspectiveCamera& camera,
                                   const SceneLightingData* lighting = nullptr);

    static void SyncLayerTextures(const std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers);
    static size_t GetCachedLayerSetCount();
    static uint64_t GetActiveLayerSetHash();
};

} // namespace Engine

#endif // RENDERER_TERRAINRENDERER_H
