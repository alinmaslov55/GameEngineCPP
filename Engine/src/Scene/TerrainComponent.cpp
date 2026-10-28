#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Utils/Logger.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Engine {

namespace {
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<int8_t, 256> BuildBase64DecodeTable() {
    std::array<int8_t, 256> table{};
    table.fill(-1);
    for (int i = 0; i < 64; ++i) {
        table[static_cast<uint8_t>(kBase64Alphabet[i])] = static_cast<int8_t>(i);
    }
    return table;
}
} // namespace

TerrainComponent::TerrainComponent(float width,
                                   float depth,
                                   uint32_t splatWidth,
                                   uint32_t splatHeight)
    : m_Width(std::max(width, 0.5f)),
      m_Depth(std::max(depth, 0.5f)),
      m_SplatWidth(std::clamp(splatWidth, 4u, 2048u)),
      m_SplatHeight(std::clamp(splatHeight, 4u, 2048u)) {
    m_SplatData.resize(static_cast<size_t>(m_SplatWidth) * static_cast<size_t>(m_SplatHeight) * 4u, 0u);
    FillLayer(0);
}

void TerrainComponent::SetWidth(float width) {
    m_Width = std::max(width, 0.5f);
}

void TerrainComponent::SetDepth(float depth) {
    m_Depth = std::max(depth, 0.5f);
}

void TerrainComponent::ResizeSplatmap(uint32_t newWidth, uint32_t newHeight) {
    newWidth = std::clamp(newWidth, 4u, 2048u);
    newHeight = std::clamp(newHeight, 4u, 2048u);
    if (newWidth == m_SplatWidth && newHeight == m_SplatHeight) {
        return;
    }

    std::vector<uint8_t> resampled(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * 4u, 0u);
    for (uint32_t nz = 0; nz < newHeight; ++nz) {
        const float v = (static_cast<float>(nz) + 0.5f) / static_cast<float>(newHeight);
        const uint32_t sz = std::min(m_SplatHeight - 1u, static_cast<uint32_t>(v * static_cast<float>(m_SplatHeight)));
        for (uint32_t nx = 0; nx < newWidth; ++nx) {
            const float u = (static_cast<float>(nx) + 0.5f) / static_cast<float>(newWidth);
            const uint32_t sx = std::min(m_SplatWidth - 1u, static_cast<uint32_t>(u * static_cast<float>(m_SplatWidth)));
            const size_t srcIdx = (static_cast<size_t>(sz) * m_SplatWidth + sx) * 4u;
            const size_t dstIdx = (static_cast<size_t>(nz) * newWidth + nx) * 4u;
            std::memcpy(&resampled[dstIdx], &m_SplatData[srcIdx], 4u);
        }
    }

    m_SplatWidth = newWidth;
    m_SplatHeight = newHeight;
    m_SplatData = std::move(resampled);
    m_GpuSplatTexture.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
}

void TerrainComponent::SetBlendSharpness(float sharpness) {
    m_BlendSharpness = std::clamp(sharpness, 0.0f, 1.0f);
}

void TerrainComponent::SetEdgeDarkening(float darkening) {
    m_EdgeDarkening = std::clamp(darkening, 0.0f, 0.5f);
}

void TerrainComponent::SetLayerTexture(uint32_t layerIndex, const std::shared_ptr<Texture>& texture) {
    if (layerIndex >= MaxLayers) {
        return;
    }
    if (texture) {
        texture->SetWrapRepeat(true);
    }
    m_Layers[layerIndex].LayerTexture = texture;
}

void TerrainComponent::SetLayerTilingScale(uint32_t layerIndex, float tilingScale) {
    if (layerIndex >= MaxLayers) {
        return;
    }
    m_Layers[layerIndex].TilingScale = std::clamp(tilingScale, 0.02f, 20.0f);
}

const TerrainLayer& TerrainComponent::GetLayer(uint32_t layerIndex) const {
    return m_Layers[std::min(layerIndex, MaxLayers - 1u)];
}

TerrainLayer& TerrainComponent::GetLayer(uint32_t layerIndex) {
    return m_Layers[std::min(layerIndex, MaxLayers - 1u)];
}

glm::u8vec4 TerrainComponent::NormalizeWeights(float r, float g, float b, float a) {
    std::array<float, 4> vals = {
        std::max(0.0f, r),
        std::max(0.0f, g),
        std::max(0.0f, b),
        std::max(0.0f, a)
    };
    const float sum = vals[0] + vals[1] + vals[2] + vals[3];
    if (sum <= 1e-5f) {
        return { 255, 0, 0, 0 };
    }

    std::array<int, 4> ints{};
    int intSum = 0;
    int maxIdx = 0;
    for (int i = 0; i < 4; ++i) {
        const float scaled = (vals[i] / sum) * 255.0f;
        ints[i] = std::clamp(static_cast<int>(std::lround(scaled)), 0, 255);
        intSum += ints[i];
        if (vals[i] > vals[maxIdx]) {
            maxIdx = i;
        }
    }

    // Absorb any +-1 integer rounding residual into the dominant channel so R+G+B+A == 255
    const int residual = 255 - intSum;
    ints[maxIdx] = std::clamp(ints[maxIdx] + residual, 0, 255);

    return {
        static_cast<uint8_t>(ints[0]),
        static_cast<uint8_t>(ints[1]),
        static_cast<uint8_t>(ints[2]),
        static_cast<uint8_t>(ints[3])
    };
}

void TerrainComponent::MarkDirtyRect(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, bool unsavedEdit) {
    if (!m_GpuDirty) {
        m_DirtyMinX = minX;
        m_DirtyMinY = minY;
        m_DirtyMaxX = maxX;
        m_DirtyMaxY = maxY;
        m_GpuDirty = true;
    } else {
        m_DirtyMinX = std::min(m_DirtyMinX, minX);
        m_DirtyMinY = std::min(m_DirtyMinY, minY);
        m_DirtyMaxX = std::max(m_DirtyMaxX, maxX);
        m_DirtyMaxY = std::max(m_DirtyMaxY, maxY);
    }

    if (unsavedEdit) {
        m_HasUnsavedMemoryEdits = true;
    }
}

void TerrainComponent::FillLayer(uint32_t layerIndex) {
    const uint32_t clampedLayer = std::min(layerIndex, MaxLayers - 1u);
    glm::u8vec4 fill(0);
    fill[static_cast<glm::length_t>(clampedLayer)] = 255;

    for (size_t idx = 0; idx < m_SplatData.size(); idx += 4u) {
        m_SplatData[idx + 0] = fill.r;
        m_SplatData[idx + 1] = fill.g;
        m_SplatData[idx + 2] = fill.b;
        m_SplatData[idx + 3] = fill.a;
    }

    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
}

glm::u8vec4 TerrainComponent::GetSplatTexel(uint32_t px, uint32_t pz) const {
    if (px >= m_SplatWidth || pz >= m_SplatHeight) {
        return { 255, 0, 0, 0 };
    }
    const size_t idx = (static_cast<size_t>(pz) * m_SplatWidth + px) * 4u;
    return {
        m_SplatData[idx + 0],
        m_SplatData[idx + 1],
        m_SplatData[idx + 2],
        m_SplatData[idx + 3]
    };
}

void TerrainComponent::SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights) {
    if (px >= m_SplatWidth || pz >= m_SplatHeight) {
        return;
    }
    const glm::u8vec4 norm = NormalizeWeights(
        static_cast<float>(weights.r),
        static_cast<float>(weights.g),
        static_cast<float>(weights.b),
        static_cast<float>(weights.a)
    );
    const size_t idx = (static_cast<size_t>(pz) * m_SplatWidth + px) * 4u;
    m_SplatData[idx + 0] = norm.r;
    m_SplatData[idx + 1] = norm.g;
    m_SplatData[idx + 2] = norm.b;
    m_SplatData[idx + 3] = norm.a;

    MarkDirtyRect(px, pz, px, pz, true);
}

void TerrainComponent::PaintWorldBrush(float worldX,
                                       float worldZ,
                                       const glm::vec3& terrainOrigin,
                                       const glm::vec3& terrainScale,
                                       uint32_t layerIndex,
                                       float radiusWorld,
                                       float falloff,
                                       float strength) {
    if (layerIndex >= MaxLayers || radiusWorld <= 0.0f || strength <= 0.0f) {
        return;
    }

    const float totalWidth = std::max(0.001f, m_Width * std::abs(terrainScale.x));
    const float totalDepth = std::max(0.001f, m_Depth * std::abs(terrainScale.z));

    const float minBrushX = worldX - radiusWorld;
    const float maxBrushX = worldX + radiusWorld;
    const float minBrushZ = worldZ - radiusWorld;
    const float maxBrushZ = worldZ + radiusWorld;

    if (maxBrushX < terrainOrigin.x || minBrushX > terrainOrigin.x + totalWidth ||
        maxBrushZ < terrainOrigin.z || minBrushZ > terrainOrigin.z + totalDepth) {
        return;
    }

    const float uMin = std::clamp((minBrushX - terrainOrigin.x) / totalWidth, 0.0f, 1.0f);
    const float uMax = std::clamp((maxBrushX - terrainOrigin.x) / totalWidth, 0.0f, 1.0f);
    const float vMin = std::clamp((minBrushZ - terrainOrigin.z) / totalDepth, 0.0f, 1.0f);
    const float vMax = std::clamp((maxBrushZ - terrainOrigin.z) / totalDepth, 0.0f, 1.0f);

    const uint32_t pxMin = static_cast<uint32_t>(std::floor(uMin * static_cast<float>(m_SplatWidth)));
    const uint32_t pxMax = std::min(m_SplatWidth - 1u, static_cast<uint32_t>(std::ceil(uMax * static_cast<float>(m_SplatWidth))));
    const uint32_t pzMin = static_cast<uint32_t>(std::floor(vMin * static_cast<float>(m_SplatHeight)));
    const uint32_t pzMax = std::min(m_SplatHeight - 1u, static_cast<uint32_t>(std::ceil(vMax * static_cast<float>(m_SplatHeight))));

    const float clampedFalloff = std::clamp(falloff, 0.0f, 1.0f);
    const float innerStart = std::clamp(1.0f - clampedFalloff, 0.0f, 0.999f);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);

    float target[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    target[layerIndex] = 255.0f;

    bool anyModified = false;
    for (uint32_t pz = pzMin; pz <= pzMax; ++pz) {
        const float texelZ = terrainOrigin.z + ((static_cast<float>(pz) + 0.5f) / static_cast<float>(m_SplatHeight)) * totalDepth;
        const float dz = texelZ - worldZ;

        for (uint32_t px = pxMin; px <= pxMax; ++px) {
            const float texelX = terrainOrigin.x + ((static_cast<float>(px) + 0.5f) / static_cast<float>(m_SplatWidth)) * totalWidth;
            const float dx = texelX - worldX;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > radiusWorld) {
                continue;
            }

            const float t = dist / radiusWorld;
            float radialMask = 1.0f;
            if (t > innerStart) {
                const float u = (t - innerStart) / (1.0f - innerStart);
                radialMask = 1.0f - u * u * (3.0f - 2.0f * u);
            }

            const float blendAlpha = std::clamp(radialMask * clampedStrength, 0.0f, 1.0f);
            if (blendAlpha <= 1e-4f) {
                continue;
            }

            const size_t idx = (static_cast<size_t>(pz) * m_SplatWidth + px) * 4u;
            const float blendedR = static_cast<float>(m_SplatData[idx + 0]) * (1.0f - blendAlpha) + target[0] * blendAlpha;
            const float blendedG = static_cast<float>(m_SplatData[idx + 1]) * (1.0f - blendAlpha) + target[1] * blendAlpha;
            const float blendedB = static_cast<float>(m_SplatData[idx + 2]) * (1.0f - blendAlpha) + target[2] * blendAlpha;
            const float blendedA = static_cast<float>(m_SplatData[idx + 3]) * (1.0f - blendAlpha) + target[3] * blendAlpha;

            const glm::u8vec4 norm = NormalizeWeights(blendedR, blendedG, blendedB, blendedA);
            m_SplatData[idx + 0] = norm.r;
            m_SplatData[idx + 1] = norm.g;
            m_SplatData[idx + 2] = norm.b;
            m_SplatData[idx + 3] = norm.a;
            anyModified = true;
        }
    }

    if (anyModified) {
        MarkDirtyRect(pxMin, pzMin, pxMax, pzMax, true);
    }
}

void TerrainComponent::PaintWorldSegment(const glm::vec2& startXZ,
                                         const glm::vec2& endXZ,
                                         const glm::vec3& terrainOrigin,
                                         const glm::vec3& terrainScale,
                                         uint32_t layerIndex,
                                         float radiusWorld,
                                         float falloff,
                                         float strength) {
    const float segLen = glm::length(endXZ - startXZ);
    const float stepDist = std::max(0.1f, radiusWorld * 0.35f);
    const int steps = std::max(1, static_cast<int>(std::ceil(segLen / stepDist)));

    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec2 pt = glm::mix(startXZ, endXZ, t);
        PaintWorldBrush(pt.x, pt.y, terrainOrigin, terrainScale, layerIndex, radiusWorld, falloff, strength);
    }
}

bool TerrainComponent::SaveSplatmapToPNG(const std::string& resolvedFilepath) {
    if (resolvedFilepath.empty() || m_SplatData.empty()) {
        return false;
    }

    std::error_code ec;
    const auto parentDir = std::filesystem::path(resolvedFilepath).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    // Encode normalized 4-layer weights (w0+w1+w2+w3 == 255) as 100% opaque RGBA (R=w0, G=w1, B=w2, A=255)
    // where w3 = 255 - (R + G + B). This is a bit-exact bijection that keeps the PNG human-inspectable
    // and immune to RGB zeroing on alpha=0 in external image editors.
    std::vector<uint8_t> opaquePngPixels(m_SplatData.size());
    for (size_t idx = 0; idx < m_SplatData.size(); idx += 4u) {
        opaquePngPixels[idx + 0] = m_SplatData[idx + 0];
        opaquePngPixels[idx + 1] = m_SplatData[idx + 1];
        opaquePngPixels[idx + 2] = m_SplatData[idx + 2];
        opaquePngPixels[idx + 3] = 255u;
    }

    // Row 0 of m_SplatData corresponds to minZ (north/top edge), matching unflipped PNG top row
    stbi_flip_vertically_on_write(0);
    const int strideBytes = static_cast<int>(m_SplatWidth * 4u);
    const int ok = stbi_write_png(resolvedFilepath.c_str(),
                                  static_cast<int>(m_SplatWidth),
                                  static_cast<int>(m_SplatHeight),
                                  4,
                                  opaquePngPixels.data(),
                                  strideBytes);
    if (!ok) {
        ENGINE_CORE_ERROR("TerrainComponent: Failed to write splatmap PNG '{}'", resolvedFilepath);
        return false;
    }

    m_HasUnsavedMemoryEdits = false;
    ENGINE_CORE_INFO("TerrainComponent: Saved {}x{} splatmap PNG to '{}'", m_SplatWidth, m_SplatHeight, resolvedFilepath);
    return true;
}

bool TerrainComponent::LoadSplatmapFromPNG(const std::string& resolvedFilepath) {
    if (resolvedFilepath.empty()) {
        return false;
    }

    // Preserve row 0 = minZ orientation without vertical inversion, then restore engine default (1)
    stbi_set_flip_vertically_on_load(0);
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(resolvedFilepath.c_str(), &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(1);

    if (!pixels || w <= 0 || h <= 0) {
        ENGINE_CORE_ERROR("TerrainComponent: Failed to load splatmap PNG '{}'", resolvedFilepath);
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    m_SplatWidth = static_cast<uint32_t>(w);
    m_SplatHeight = static_cast<uint32_t>(h);
    const size_t byteCount = static_cast<size_t>(m_SplatWidth) * static_cast<size_t>(m_SplatHeight) * 4u;
    m_SplatData.resize(byteCount);

    for (size_t idx = 0; idx < byteCount; idx += 4u) {
        const int r = pixels[idx + 0];
        const int g = pixels[idx + 1];
        const int b = pixels[idx + 2];
        const int a = pixels[idx + 3];

        if (a == 255) {
            const int rgbSum = r + g + b;
            if (rgbSum <= 255) {
                m_SplatData[idx + 0] = static_cast<uint8_t>(r);
                m_SplatData[idx + 1] = static_cast<uint8_t>(g);
                m_SplatData[idx + 2] = static_cast<uint8_t>(b);
                m_SplatData[idx + 3] = static_cast<uint8_t>(255 - rgbSum);
            } else {
                // Normalize externally painted RGB strokes where R+G+B exceeded 255
                const glm::u8vec4 norm = NormalizeWeights(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 0.0f);
                m_SplatData[idx + 0] = norm.r;
                m_SplatData[idx + 1] = norm.g;
                m_SplatData[idx + 2] = norm.b;
                m_SplatData[idx + 3] = norm.a;
            }
        } else {
            const glm::u8vec4 norm = NormalizeWeights(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a));
            m_SplatData[idx + 0] = norm.r;
            m_SplatData[idx + 1] = norm.g;
            m_SplatData[idx + 2] = norm.b;
            m_SplatData[idx + 3] = norm.a;
        }
    }
    stbi_image_free(pixels);

    m_GpuSplatTexture.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, false);
    m_HasUnsavedMemoryEdits = false;
    return true;
}

std::string TerrainComponent::EncodeSplatBase64() const {
    std::string out;
    out.reserve(((m_SplatData.size() + 2u) / 3u) * 4u);

    size_t i = 0;
    while (i + 2u < m_SplatData.size()) {
        const uint32_t triple = (static_cast<uint32_t>(m_SplatData[i]) << 16u) |
                                (static_cast<uint32_t>(m_SplatData[i + 1u]) << 8u) |
                                static_cast<uint32_t>(m_SplatData[i + 2u]);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[triple & 0x3Fu]);
        i += 3u;
    }

    const size_t rem = m_SplatData.size() - i;
    if (rem == 1u) {
        const uint32_t triple = static_cast<uint32_t>(m_SplatData[i]) << 16u;
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2u) {
        const uint32_t triple = (static_cast<uint32_t>(m_SplatData[i]) << 16u) |
                                (static_cast<uint32_t>(m_SplatData[i + 1u]) << 8u);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

bool TerrainComponent::DecodeSplatBase64(const std::string& base64Data, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || base64Data.empty()) {
        return false;
    }

    static const auto kDecodeTable = BuildBase64DecodeTable();
    const size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    std::vector<uint8_t> decoded;
    decoded.reserve(expectedBytes);

    uint32_t buffer = 0;
    int bitsCollected = 0;
    for (unsigned char ch : base64Data) {
        if (ch == '=') {
            break;
        }
        const int8_t val = kDecodeTable[ch];
        if (val < 0) {
            continue;
        }
        buffer = (buffer << 6u) | static_cast<uint32_t>(val);
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            decoded.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFFu));
        }
    }

    if (decoded.size() != expectedBytes) {
        ENGINE_CORE_ERROR("TerrainComponent::DecodeSplatBase64 byte size mismatch (got {}, expected {})",
                          decoded.size(), expectedBytes);
        return false;
    }

    m_SplatWidth = width;
    m_SplatHeight = height;
    m_SplatData = std::move(decoded);
    m_GpuSplatTexture.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
    return true;
}

const std::shared_ptr<Texture>& TerrainComponent::SyncGpuSplatTexture() {
    if (!m_GpuSplatTexture ||
        m_GpuSplatTexture->GetWidth() != static_cast<int>(m_SplatWidth) ||
        m_GpuSplatTexture->GetHeight() != static_cast<int>(m_SplatHeight)) {
        m_GpuSplatTexture = std::make_shared<Texture>(
            m_SplatWidth,
            m_SplatHeight,
            m_SplatData.data(),
            /*linearFilter=*/true,
            /*repeatWrap=*/false
        );
        m_GpuDirty = false;
        return m_GpuSplatTexture;
    }

    if (m_GpuDirty) {
        const uint32_t minX = std::min(m_DirtyMinX, m_SplatWidth - 1u);
        const uint32_t minY = std::min(m_DirtyMinY, m_SplatHeight - 1u);
        const uint32_t maxX = std::min(std::max(minX, m_DirtyMaxX), m_SplatWidth - 1u);
        const uint32_t maxY = std::min(std::max(minY, m_DirtyMaxY), m_SplatHeight - 1u);

        const uint32_t subW = maxX - minX + 1u;
        const uint32_t subH = maxY - minY + 1u;

        if (subW == m_SplatWidth && subH == m_SplatHeight) {
            m_GpuSplatTexture->SetData(m_SplatData.data(), static_cast<uint32_t>(m_SplatData.size()));
        } else {
            m_UploadScratch.resize(static_cast<size_t>(subW) * static_cast<size_t>(subH) * 4u);
            for (uint32_t row = 0; row < subH; ++row) {
                const size_t srcOffset = (static_cast<size_t>(minY + row) * m_SplatWidth + minX) * 4u;
                const size_t dstOffset = static_cast<size_t>(row) * subW * 4u;
                std::memcpy(&m_UploadScratch[dstOffset], &m_SplatData[srcOffset], static_cast<size_t>(subW) * 4u);
            }
            m_GpuSplatTexture->SetSubData(minX, minY, subW, subH, m_UploadScratch.data());
        }
        m_GpuDirty = false;
    }

    return m_GpuSplatTexture;
}

} // namespace Engine
