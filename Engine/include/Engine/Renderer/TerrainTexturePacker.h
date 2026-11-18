#ifndef RENDERER_TERRAINTEXTUREPACKER_H
#define RENDERER_TERRAINTEXTUREPACKER_H

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

/// @brief Pure CPU image resampling, companion-map discovery, and terrain channel-packing
/// utilities (Albedo+Height RGBA8 and Tangent-Normal+Gloss RGBA8). Headless-safe & zero-GL.
class TerrainTexturePacker {
public:
    static void ResizeRGBA8Bilinear(const uint8_t* src,
                                    uint32_t srcW,
                                    uint32_t srcH,
                                    uint32_t dstW,
                                    uint32_t dstH,
                                    std::vector<uint8_t>& outRgba8);

    static void ResizeR8Bilinear(const uint8_t* src,
                                 uint32_t srcW,
                                 uint32_t srcH,
                                 uint32_t dstW,
                                 uint32_t dstH,
                                 std::vector<uint8_t>& outR8);

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

    [[nodiscard]] static ResolvedTerrainMaps ResolveTerrainMapCompanions(const std::string& diffusePath,
                                                                           const std::string& explicitHeightPath = "",
                                                                           const std::string& explicitNormalPath = "");

    static bool BuildAlbedoHeightSliceFromFiles(const std::string& diffusePath,
                                                const std::string& heightPath,
                                                uint32_t targetW,
                                                uint32_t targetH,
                                                const glm::u8vec4& fallbackRgba,
                                                std::vector<uint8_t>& outRgba8);

    static bool BuildNormalGlossSliceFromFiles(const std::string& normalPath,
                                               float glossiness,
                                               uint32_t targetW,
                                               uint32_t targetH,
                                               std::vector<uint8_t>& outRgba8);
};

} // namespace Engine

#endif // RENDERER_TERRAINTEXTUREPACKER_H
