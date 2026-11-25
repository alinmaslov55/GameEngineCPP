#include "Engine/Renderer/TerrainTexturePacker.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

namespace Engine {

namespace {

struct StbiDeleter {
    void operator()(unsigned char* ptr) const noexcept {
        if (ptr) {
            stbi_image_free(ptr);
        }
    }
};
using StbiPtr = std::unique_ptr<unsigned char, StbiDeleter>;

std::vector<uint8_t> ConvertToTightRGBA8(const uint8_t* pixels,
                                         int channels,
                                         uint32_t width,
                                         uint32_t height,
                                         bool alphaOpaqueDefault) {
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgba(pixelCount * 4u, 255u);
    const int safeChannels = std::clamp(channels, 1, 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t srcIdx = i * static_cast<size_t>(safeChannels);
        const size_t dstIdx = i * 4u;
        if (safeChannels == 1) {
            const uint8_t v = pixels[srcIdx];
            rgba[dstIdx + 0] = v;
            rgba[dstIdx + 1] = v;
            rgba[dstIdx + 2] = v;
            rgba[dstIdx + 3] = alphaOpaqueDefault ? 255u : v;
        } else if (safeChannels == 2) {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 0];
            rgba[dstIdx + 2] = pixels[srcIdx + 0];
            rgba[dstIdx + 3] = pixels[srcIdx + 1];
        } else if (safeChannels == 3) {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 1];
            rgba[dstIdx + 2] = pixels[srcIdx + 2];
            rgba[dstIdx + 3] = 255u;
        } else {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 1];
            rgba[dstIdx + 2] = pixels[srcIdx + 2];
            rgba[dstIdx + 3] = pixels[srcIdx + 3];
        }
    }
    return rgba;
}

uint8_t ExtractLuminance(const uint8_t* pixel, int channels) noexcept {
    if (channels <= 2) {
        return pixel[0];
    }
    const float lum = 0.299f * static_cast<float>(pixel[0]) +
                      0.587f * static_cast<float>(pixel[1]) +
                      0.114f * static_cast<float>(pixel[2]);
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(lum)), 0, 255));
}

bool FileExistsResolved(const std::string& rawOrResolvedPath, std::string& outResolved) {
    if (rawOrResolvedPath.empty()) {
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(rawOrResolvedPath, ec) && std::filesystem::is_regular_file(rawOrResolvedPath, ec)) {
        outResolved = rawOrResolvedPath;
        return true;
    }
    const std::string resolved = ResourceManager::ResolvePath(rawOrResolvedPath);
    if (std::filesystem::exists(resolved, ec) && std::filesystem::is_regular_file(resolved, ec)) {
        outResolved = resolved;
        return true;
    }
    return false;
}

std::string ProbeCompanionSuffix(const std::filesystem::path& diffusePath,
                                 const std::string& suffixTag) {
    const std::string ext = diffusePath.extension().string();
    const std::string stem = diffusePath.stem().string();
    const std::filesystem::path dir = diffusePath.parent_path();

    std::vector<std::string> candidates;
    if (stem.size() > 2u && stem.compare(stem.size() - 2u, 2u, "_d") == 0) {
        candidates.push_back(stem.substr(0, stem.size() - 2u) + suffixTag + ext);
    }
    if (stem.size() > 9u && stem.compare(stem.size() - 9u, 9u, "_seamless") == 0) {
        candidates.push_back(stem.substr(0, stem.size() - 9u) + suffixTag + ext);
    }
    candidates.push_back(stem + suffixTag + ext);

    for (const auto& name : candidates) {
        std::string resolved;
        const std::string candidate = (dir / name).string();
        if (FileExistsResolved(candidate, resolved)) {
            return resolved;
        }
    }
    return "";
}

} // namespace

void TerrainTexturePacker::ResizeRGBA8Bilinear(const uint8_t* src,
                                               uint32_t srcW,
                                               uint32_t srcH,
                                               uint32_t dstW,
                                               uint32_t dstH,
                                               std::vector<uint8_t>& outRgba8) {
    if (!src || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        outRgba8.clear();
        return;
    }

    const size_t dstBytes = static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u;
    outRgba8.resize(dstBytes);

    if (srcW == dstW && srcH == dstH) {
        std::memcpy(outRgba8.data(), src, dstBytes);
        return;
    }

    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (uint32_t y = 0; y < dstH; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const float cy = std::clamp(fy, 0.0f, static_cast<float>(srcH - 1u));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(cy));
        const uint32_t y1 = std::min(srcH - 1u, y0 + 1u);
        const float wy = cy - static_cast<float>(y0);

        for (uint32_t x = 0; x < dstW; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const float cx = std::clamp(fx, 0.0f, static_cast<float>(srcW - 1u));
            const uint32_t x0 = static_cast<uint32_t>(std::floor(cx));
            const uint32_t x1 = std::min(srcW - 1u, x0 + 1u);
            const float wx = cx - static_cast<float>(x0);

            const size_t idx00 = (static_cast<size_t>(y0) * srcW + x0) * 4u;
            const size_t idx10 = (static_cast<size_t>(y0) * srcW + x1) * 4u;
            const size_t idx01 = (static_cast<size_t>(y1) * srcW + x0) * 4u;
            const size_t idx11 = (static_cast<size_t>(y1) * srcW + x1) * 4u;
            const size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4u;

            for (int c = 0; c < 4; ++c) {
                const float top = static_cast<float>(src[idx00 + c]) * (1.0f - wx) +
                                  static_cast<float>(src[idx10 + c]) * wx;
                const float bot = static_cast<float>(src[idx01 + c]) * (1.0f - wx) +
                                  static_cast<float>(src[idx11 + c]) * wx;
                const float val = top * (1.0f - wy) + bot * wy;
                outRgba8[dstIdx + c] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(val)), 0, 255));
            }
        }
    }
}

void TerrainTexturePacker::ResizeR8Bilinear(const uint8_t* src,
                                            uint32_t srcW,
                                            uint32_t srcH,
                                            uint32_t dstW,
                                            uint32_t dstH,
                                            std::vector<uint8_t>& outR8) {
    if (!src || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        outR8.clear();
        return;
    }

    const size_t dstBytes = static_cast<size_t>(dstW) * static_cast<size_t>(dstH);
    outR8.resize(dstBytes);

    if (srcW == dstW && srcH == dstH) {
        std::memcpy(outR8.data(), src, dstBytes);
        return;
    }

    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (uint32_t y = 0; y < dstH; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const float cy = std::clamp(fy, 0.0f, static_cast<float>(srcH - 1u));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(cy));
        const uint32_t y1 = std::min(srcH - 1u, y0 + 1u);
        const float wy = cy - static_cast<float>(y0);

        for (uint32_t x = 0; x < dstW; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const float cx = std::clamp(fx, 0.0f, static_cast<float>(srcW - 1u));
            const uint32_t x0 = static_cast<uint32_t>(std::floor(cx));
            const uint32_t x1 = std::min(srcW - 1u, x0 + 1u);
            const float wx = cx - static_cast<float>(x0);

            const size_t idx00 = static_cast<size_t>(y0) * srcW + x0;
            const size_t idx10 = static_cast<size_t>(y0) * srcW + x1;
            const size_t idx01 = static_cast<size_t>(y1) * srcW + x0;
            const size_t idx11 = static_cast<size_t>(y1) * srcW + x1;
            const size_t dstIdx = static_cast<size_t>(y) * dstW + x;

            const float top = static_cast<float>(src[idx00]) * (1.0f - wx) +
                              static_cast<float>(src[idx10]) * wx;
            const float bot = static_cast<float>(src[idx01]) * (1.0f - wx) +
                              static_cast<float>(src[idx11]) * wx;
            const float val = top * (1.0f - wy) + bot * wy;
            outR8[dstIdx] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(val)), 0, 255));
        }
    }
}

bool TerrainTexturePacker::PackAlbedoHeightRGBA8(const uint8_t* diffusePixels,
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
                                                 int fallbackHeight) {
    if (!diffusePixels || diffuseW == 0 || diffuseH == 0 || targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("TerrainTexturePacker::PackAlbedoHeightRGBA8 rejected invalid dimensions/input ({}x{} -> {}x{})",
                         diffuseW, diffuseH, targetW, targetH);
        return false;
    }

    const std::vector<uint8_t> srcRgba = ConvertToTightRGBA8(diffusePixels, diffuseChannels, diffuseW, diffuseH, true);
    ResizeRGBA8Bilinear(srcRgba.data(), diffuseW, diffuseH, targetW, targetH, outRgba8);

    const size_t pixelCount = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    if (heightPixels && heightW > 0 && heightH > 0) {
        const int safeHCh = std::clamp(heightChannels, 1, 4);
        std::vector<uint8_t> heightR8(static_cast<size_t>(heightW) * static_cast<size_t>(heightH));
        for (size_t i = 0; i < static_cast<size_t>(heightW) * static_cast<size_t>(heightH); ++i) {
            heightR8[i] = ExtractLuminance(&heightPixels[i * static_cast<size_t>(safeHCh)], safeHCh);
        }

        std::vector<uint8_t> resizedHeight;
        ResizeR8Bilinear(heightR8.data(), heightW, heightH, targetW, targetH, resizedHeight);
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = resizedHeight[i];
        }
    } else if (fallbackHeight >= 0) {
        const uint8_t hConst = static_cast<uint8_t>(std::clamp(fallbackHeight, 0, 255));
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = hConst;
        }
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = ExtractLuminance(&outRgba8[i * 4u], 3);
        }
    }

    return true;
}

bool TerrainTexturePacker::PackNormalGlossRGBA8(const uint8_t* normalPixels,
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
                                                std::vector<uint8_t>& outRgba8) {
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("TerrainTexturePacker::PackNormalGlossRGBA8 rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    const size_t targetPixels = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    const uint8_t defaultGlossByte = static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::lround(std::clamp(defaultGlossiness, 0.0f, 1.0f) * 255.0f)), 0, 255)
    );

    if (normalPixels && normalW > 0 && normalH > 0) {
        const std::vector<uint8_t> srcNormal = ConvertToTightRGBA8(normalPixels, normalChannels, normalW, normalH, true);
        ResizeRGBA8Bilinear(srcNormal.data(), normalW, normalH, targetW, targetH, outRgba8);

        // Ensure decoded tangent normals remain normalized and upward-facing (+Z tangent space)
        for (size_t i = 0; i < targetPixels; ++i) {
            const size_t idx = i * 4u;
            float nx = (static_cast<float>(outRgba8[idx + 0]) / 255.0f) * 2.0f - 1.0f;
            float ny = (static_cast<float>(outRgba8[idx + 1]) / 255.0f) * 2.0f - 1.0f;
            float nz = (static_cast<float>(outRgba8[idx + 2]) / 255.0f) * 2.0f - 1.0f;
            const float lenSq = nx * nx + ny * ny + nz * nz;
            if (lenSq < 1e-5f) {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            } else {
                const float invLen = 1.0f / std::sqrt(lenSq);
                nx *= invLen;
                ny *= invLen;
                nz = std::max(nz * invLen, 0.0f);
            }
            outRgba8[idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((nx * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((ny * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((nz * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 3] = defaultGlossByte;
        }
    } else {
        outRgba8.resize(targetPixels * 4u);
        for (size_t i = 0; i < targetPixels; ++i) {
            const size_t idx = i * 4u;
            outRgba8[idx + 0] = 128u;
            outRgba8[idx + 1] = 128u;
            outRgba8[idx + 2] = 255u;
            outRgba8[idx + 3] = defaultGlossByte;
        }
    }

    if (glossPixels && glossW > 0 && glossH > 0) {
        const int safeGCh = std::clamp(glossChannels, 1, 4);
        const size_t srcPixels = static_cast<size_t>(glossW) * static_cast<size_t>(glossH);
        std::vector<uint8_t> glossR8(srcPixels);
        for (size_t i = 0; i < srcPixels; ++i) {
            glossR8[i] = ExtractLuminance(&glossPixels[i * static_cast<size_t>(safeGCh)], safeGCh);
        }
        std::vector<uint8_t> resizedGloss;
        ResizeR8Bilinear(glossR8.data(), glossW, glossH, targetW, targetH, resizedGloss);
        for (size_t i = 0; i < targetPixels; ++i) {
            outRgba8[i * 4u + 3] = resizedGloss[i];
        }
    }

    return true;
}

ResolvedTerrainMaps TerrainTexturePacker::ResolveTerrainMapCompanions(const std::string& diffusePath,
                                                                        const std::string& explicitHeightPath,
                                                                        const std::string& explicitNormalPath) {
    ResolvedTerrainMaps out;
    FileExistsResolved(diffusePath, out.DiffusePath);
    FileExistsResolved(explicitHeightPath, out.HeightPath);
    FileExistsResolved(explicitNormalPath, out.NormalPath);

    if (!diffusePath.empty()) {
        const std::filesystem::path probeBase(!out.DiffusePath.empty() ? out.DiffusePath : diffusePath);
        if (out.HeightPath.empty()) {
            out.HeightPath = ProbeCompanionSuffix(probeBase, "_h");
        }
        if (out.NormalPath.empty()) {
            out.NormalPath = ProbeCompanionSuffix(probeBase, "_n");
        }
    }
    return out;
}

bool TerrainTexturePacker::BuildAlbedoHeightSliceFromFiles(const std::string& diffusePath,
                                                           const std::string& heightPath,
                                                           uint32_t targetW,
                                                           uint32_t targetH,
                                                           const glm::u8vec4& fallbackRgba,
                                                           std::vector<uint8_t>& outRgba8) {
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("TerrainTexturePacker::BuildAlbedoHeightSliceFromFiles rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    std::string resolvedDiffuse;
    std::string resolvedHeight;
    FileExistsResolved(diffusePath, resolvedDiffuse);
    FileExistsResolved(heightPath, resolvedHeight);

    stbi_set_flip_vertically_on_load_thread(1);

    int dw = 0, dh = 0, dch = 0;
    StbiPtr dPixels(!resolvedDiffuse.empty() ? stbi_load(resolvedDiffuse.c_str(), &dw, &dh, &dch, 0) : nullptr);

    int hw = 0, hh = 0, hch = 0;
    StbiPtr hPixels(!resolvedHeight.empty() ? stbi_load(resolvedHeight.c_str(), &hw, &hh, &hch, 0) : nullptr);

    if (!diffusePath.empty() && !dPixels) {
        ENGINE_CORE_WARN("TerrainTexturePacker: Failed to load terrain diffuse '{}' (using solid fallback color)", diffusePath);
    }
    if (!heightPath.empty() && !hPixels) {
        ENGINE_CORE_WARN("TerrainTexturePacker: Failed to load terrain height map '{}' (using fallback height)", heightPath);
    }

    if (!dPixels || dw <= 0 || dh <= 0) {
        const uint8_t solidPixel[4] = { fallbackRgba.r, fallbackRgba.g, fallbackRgba.b, fallbackRgba.a };
        return PackAlbedoHeightRGBA8(solidPixel,
                                     4,
                                     1u,
                                     1u,
                                     hPixels.get(),
                                     hch,
                                     static_cast<uint32_t>(std::max(0, hw)),
                                     static_cast<uint32_t>(std::max(0, hh)),
                                     targetW,
                                     targetH,
                                     outRgba8,
                                     hPixels ? -1 : static_cast<int>(fallbackRgba.a));
    }

    ENGINE_CORE_DEBUG("TerrainTexturePacker: Built albedo+height slice ({}x{}) from '{}' (diffuse {}x{}, height={})",
                      targetW, targetH, resolvedDiffuse, dw, dh,
                      hPixels ? resolvedHeight : "<luminance fallback>");

    return PackAlbedoHeightRGBA8(dPixels.get(),
                                 dch,
                                 static_cast<uint32_t>(dw),
                                 static_cast<uint32_t>(dh),
                                 hPixels.get(),
                                 hch,
                                 static_cast<uint32_t>(std::max(0, hw)),
                                 static_cast<uint32_t>(std::max(0, hh)),
                                 targetW,
                                 targetH,
                                 outRgba8);
}

bool TerrainTexturePacker::BuildNormalGlossSliceFromFiles(const std::string& normalPath,
                                                          float glossiness,
                                                          uint32_t targetW,
                                                          uint32_t targetH,
                                                          std::vector<uint8_t>& outRgba8) {
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("TerrainTexturePacker::BuildNormalGlossSliceFromFiles rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    std::string resolvedNormal;
    FileExistsResolved(normalPath, resolvedNormal);

    stbi_set_flip_vertically_on_load_thread(1);
    int nw = 0, nh = 0, nch = 0;
    StbiPtr nPixels(!resolvedNormal.empty() ? stbi_load(resolvedNormal.c_str(), &nw, &nh, &nch, 0) : nullptr);

    if (!normalPath.empty() && !nPixels) {
        ENGINE_CORE_WARN("TerrainTexturePacker: Failed to load terrain normal map '{}' (using flat +Z fallback)", normalPath);
    } else if (nPixels) {
        ENGINE_CORE_DEBUG("TerrainTexturePacker: Built normal+gloss slice ({}x{}, gloss={:.2f}) from '{}'",
                          targetW, targetH, glossiness, resolvedNormal);
    }

    return PackNormalGlossRGBA8(nPixels.get(),
                                nch,
                                static_cast<uint32_t>(std::max(0, nw)),
                                static_cast<uint32_t>(std::max(0, nh)),
                                glossiness,
                                nullptr,
                                0,
                                0u,
                                0u,
                                targetW,
                                targetH,
                                outRgba8);
}

} // namespace Engine
