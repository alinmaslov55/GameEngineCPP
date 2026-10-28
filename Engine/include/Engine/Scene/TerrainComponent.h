#ifndef SCENE_TERRAINCOMPONENT_H
#define SCENE_TERRAINCOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

struct TerrainLayer {
    std::shared_ptr<Texture> LayerTexture = nullptr;
    float TilingScale = 0.45f; // UV repeats per world meter
};

class TerrainComponent : public Component {
public:
    static constexpr uint32_t MaxLayers = 4;

    TerrainComponent(float width = 14.0f,
                     float depth = 14.0f,
                     uint32_t splatWidth = 128,
                     uint32_t splatHeight = 128);
    ~TerrainComponent() override = default;

    float GetWidth() const { return m_Width; }
    void SetWidth(float width);

    float GetDepth() const { return m_Depth; }
    void SetDepth(float depth);

    uint32_t GetSplatWidth() const { return m_SplatWidth; }
    uint32_t GetSplatHeight() const { return m_SplatHeight; }
    void ResizeSplatmap(uint32_t newWidth, uint32_t newHeight);

    float GetBlendSharpness() const { return m_BlendSharpness; }
    void SetBlendSharpness(float sharpness);

    float GetEdgeDarkening() const { return m_EdgeDarkening; }
    void SetEdgeDarkening(float darkening);

    void SetLayerTexture(uint32_t layerIndex, const std::shared_ptr<Texture>& texture);
    void SetLayerTilingScale(uint32_t layerIndex, float tilingScale);
    const TerrainLayer& GetLayer(uint32_t layerIndex) const;
    TerrainLayer& GetLayer(uint32_t layerIndex);
    const std::array<TerrainLayer, MaxLayers>& GetLayers() const { return m_Layers; }

    // Splatmap CPU manipulation (headless-safe; R+G+B+A == 255 invariant)
    void FillLayer(uint32_t layerIndex);
    glm::u8vec4 GetSplatTexel(uint32_t px, uint32_t pz) const;
    void SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights);

    // Radial smoothstep brush painting in 3D world XZ coordinates
    void PaintWorldBrush(float worldX,
                         float worldZ,
                         const glm::vec3& terrainOrigin,
                         const glm::vec3& terrainScale,
                         uint32_t layerIndex,
                         float radiusWorld,
                         float falloff = 0.65f,
                         float strength = 1.0f);

    // Paints a continuous capsule stroke between two 3D world XZ points
    void PaintWorldSegment(const glm::vec2& startXZ,
                           const glm::vec2& endXZ,
                           const glm::vec3& terrainOrigin,
                           const glm::vec3& terrainScale,
                           uint32_t layerIndex,
                           float radiusWorld,
                           float falloff = 0.65f,
                           float strength = 1.0f);

    const std::vector<uint8_t>& GetSplatData() const { return m_SplatData; }

    const std::string& GetSplatmapPath() const { return m_SplatmapPath; }
    void SetSplatmapPath(const std::string& path) { m_SplatmapPath = path; }

    bool HasUnsavedMemoryEdits() const { return m_HasUnsavedMemoryEdits; }
    void ClearUnsavedMemoryEdits() { m_HasUnsavedMemoryEdits = false; }

    bool SaveSplatmapToPNG(const std::string& resolvedFilepath);
    bool LoadSplatmapFromPNG(const std::string& resolvedFilepath);

    std::string EncodeSplatBase64() const;
    bool DecodeSplatBase64(const std::string& base64Data, uint32_t width, uint32_t height);

    // Lazily allocates and updates the OpenGL RGBA8 splatmap texture when rendered
    const std::shared_ptr<Texture>& SyncGpuSplatTexture();

private:
    void MarkDirtyRect(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, bool unsavedEdit);
    static glm::u8vec4 NormalizeWeights(float r, float g, float b, float a);

private:
    float m_Width = 14.0f;
    float m_Depth = 14.0f;
    uint32_t m_SplatWidth = 128;
    uint32_t m_SplatHeight = 128;

    float m_BlendSharpness = 0.68f;
    float m_EdgeDarkening = 0.16f;

    std::array<TerrainLayer, MaxLayers> m_Layers{};
    std::vector<uint8_t> m_SplatData;
    std::string m_SplatmapPath;

    // GPU upload state (lazy so headless tests never require an OpenGL context)
    std::shared_ptr<Texture> m_GpuSplatTexture = nullptr;
    std::vector<uint8_t> m_UploadScratch;
    bool m_GpuDirty = true;
    uint32_t m_DirtyMinX = 0;
    uint32_t m_DirtyMinY = 0;
    uint32_t m_DirtyMaxX = 0;
    uint32_t m_DirtyMaxY = 0;
    bool m_HasUnsavedMemoryEdits = true;
};

} // namespace Engine

#endif // SCENE_TERRAINCOMPONENT_H

