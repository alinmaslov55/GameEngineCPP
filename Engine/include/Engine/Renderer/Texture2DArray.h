#ifndef RENDERER_TEXTURE2DARRAY_H
#define RENDERER_TEXTURE2DARRAY_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine {

struct ResolvedTerrainMaps {
    std::string DiffusePath;
    std::string HeightPath;
    std::string NormalPath;
};

/// @brief OpenGL 4.1 Core 2D Texture Array (`GL_TEXTURE_2D_ARRAY`) with mipmapping,
/// GL_REPEAT wrapping, optional anisotropic filtering, and headless safety.
class Texture2DArray {
public:
    static constexpr uint32_t DefaultWidth = 1024;
    static constexpr uint32_t DefaultHeight = 1024;
    static constexpr uint32_t MaxLayers = 8;

    Texture2DArray(uint32_t width = DefaultWidth,
                   uint32_t height = DefaultHeight,
                   uint32_t layers = MaxLayers,
                   bool linearFilter = true,
                   bool repeatWrap = true);
    ~Texture2DArray();

    Texture2DArray(const Texture2DArray&) = delete;
    Texture2DArray& operator=(const Texture2DArray&) = delete;
    Texture2DArray(Texture2DArray&& other) noexcept;
    Texture2DArray& operator=(Texture2DArray&& other) noexcept;

    void Bind(uint32_t slot = 0) const;
    void Unbind() const;

    /// @brief Uploads tightly packed RGBA8 pixel data (width * height * 4 bytes) to `layerIndex`.
    void SetLayerData(uint32_t layerIndex, const void* rgba8Data, bool generateMips = true);
    void GenerateMipmaps();

    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
    [[nodiscard]] uint32_t GetLayers() const noexcept { return m_Layers; }
    [[nodiscard]] uint32_t GetRendererID() const noexcept { return m_RendererID; }
    [[nodiscard]] float GetAppliedAnisotropy() const noexcept { return m_AppliedAnisotropy; }
    [[nodiscard]] bool HasUploadedLayer(uint32_t layerIndex) const noexcept {
        return layerIndex < m_UploadedLayers.size() && m_UploadedLayers[layerIndex];
    }

    // Pure CPU image resampling & terrain channel-packing helpers (headless-safe & testable)
    static void ResizeRGBA8Bilinear(const uint8_t* src,
                                    uint32_t srcW,
                                    uint32_t srcH,
                                    uint32_t dstW,
                                    uint32_t dstH,
                                    std::vector<uint8_t>& outRgba8);

    /// @brief Packs diffuse RGB and height A into an RGBA8 buffer at targetW x targetH.
    /// If `heightPixels` is null, `fallbackHeight >= 0` fills uniform height, or `< 0` derives luminance from RGB.
    static bool PackAlbedoHeightRGBA8(const uint8_t* diffusePixels,
                                      int diffuseChannels,
                                      uint32_t diffuseW,
                                      uint32_t diffuseH,
                                      const uint8_t* heightPixels,
                                      int heightChannels,
                                      uint32_t heightW,
                                      uint32_t heightH,
                                      uint32_t targetW,
                                      uint32_t targetH,
                                      std::vector<uint8_t>& outRgba8,
                                      int fallbackHeight = -1);

    /// @brief Packs tangent-space normal XYZ and glossiness A into an RGBA8 buffer at targetW x targetH.
    /// If `normalPixels` is null, synthesizes flat tangent-space up `(128, 128, 255)`.
    static bool PackNormalGlossRGBA8(const uint8_t* normalPixels,
                                     int normalChannels,
                                     uint32_t normalW,
                                     uint32_t normalH,
                                     float defaultGlossiness,
                                     const uint8_t* glossPixels,
                                     int glossChannels,
                                     uint32_t glossW,
                                     uint32_t glossH,
                                     uint32_t targetW,
                                     uint32_t targetH,
                                     std::vector<uint8_t>& outRgba8);

    /// @brief Resolves companion `_h.png` and `_n.png` paths from a diffuse path using asset guide conventions.
    [[nodiscard]] static ResolvedTerrainMaps ResolveTerrainMapCompanions(const std::string& diffusePath,
                                                                           const std::string& explicitHeightPath = "",
                                                                           const std::string& explicitNormalPath = "");

    /// @brief Loads diffuse + optional height map from disk and packs RGBA8 slice (falls back to solid color if missing).
    static bool BuildAlbedoHeightSliceFromFiles(const std::string& diffusePath,
                                                const std::string& heightPath,
                                                uint32_t targetW,
                                                uint32_t targetH,
                                                const glm::u8vec4& fallbackRgba,
                                                std::vector<uint8_t>& outRgba8);

    /// @brief Loads optional normal map from disk and packs tangent-space normal + glossiness RGBA8 slice.
    static bool BuildNormalGlossSliceFromFiles(const std::string& normalPath,
                                               float glossiness,
                                               uint32_t targetW,
                                               uint32_t targetH,
                                               std::vector<uint8_t>& outRgba8);

private:
    uint32_t m_RendererID = 0;
    uint32_t m_Width = DefaultWidth;
    uint32_t m_Height = DefaultHeight;
    uint32_t m_Layers = MaxLayers;
    bool m_LinearFilter = true;
    bool m_RepeatWrap = true;
    float m_AppliedAnisotropy = 1.0f;
    std::vector<uint8_t> m_UploadedLayers;
};

} // namespace Engine

#endif // RENDERER_TEXTURE2DARRAY_H
