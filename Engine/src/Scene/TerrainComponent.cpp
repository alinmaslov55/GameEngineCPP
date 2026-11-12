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
    for (uint32_t i = 0; i < MaxLayers; ++i) {
        m_Layers[i].Glossiness = DefaultLayerGlossiness(i);
    }
    const size_t bufferBytes = static_cast<size_t>(m_SplatWidth) * static_cast<size_t>(m_SplatHeight) * 4u;
    m_SplatData.resize(bufferBytes, 0u);
    m_SplatData1.resize(bufferBytes, 0u);
    FillLayer(0);
    ENGINE_CORE_DEBUG("TerrainComponent created: Size={:.1f}x{:.1f}m, Splatmap={}x{} (8-layer)",
                      m_Width, m_Depth, m_SplatWidth, m_SplatHeight);
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

    auto resampleBuffer = [&](const std::vector<uint8_t>& src) {
        std::vector<uint8_t> out(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * 4u, 0u);
        if (src.size() < static_cast<size_t>(m_SplatWidth) * static_cast<size_t>(m_SplatHeight) * 4u) {
            return out;
        }
        for (uint32_t nz = 0; nz < newHeight; ++nz) {
            const float v = (static_cast<float>(nz) + 0.5f) / static_cast<float>(newHeight);
            const uint32_t sz = std::min(m_SplatHeight - 1u, static_cast<uint32_t>(v * static_cast<float>(m_SplatHeight)));
            for (uint32_t nx = 0; nx < newWidth; ++nx) {
                const float u = (static_cast<float>(nx) + 0.5f) / static_cast<float>(newWidth);
                const uint32_t sx = std::min(m_SplatWidth - 1u, static_cast<uint32_t>(u * static_cast<float>(m_SplatWidth)));
                const size_t srcIdx = (static_cast<size_t>(sz) * m_SplatWidth + sx) * 4u;
                const size_t dstIdx = (static_cast<size_t>(nz) * newWidth + nx) * 4u;
                std::memcpy(&out[dstIdx], &src[srcIdx], 4u);
            }
        }
        return out;
    };

    std::vector<uint8_t> resampled0 = resampleBuffer(m_SplatData);
    std::vector<uint8_t> resampled1 = resampleBuffer(m_SplatData1);

    ENGINE_CORE_INFO("TerrainComponent: Resized splatmap from {}x{} to {}x{}",
                     m_SplatWidth, m_SplatHeight, newWidth, newHeight);
    m_SplatWidth = newWidth;
    m_SplatHeight = newHeight;
    m_SplatData = std::move(resampled0);
    m_SplatData1 = std::move(resampled1);
    m_GpuSplatTexture.reset();
    m_GpuSplatTexture1.reset();
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
    ENGINE_CORE_DEBUG("TerrainComponent: Set layer {} texture '{}'", layerIndex, texture ? texture->GetFilepath() : "nullptr");
}

void TerrainComponent::SetLayerHeightMapPath(uint32_t layerIndex, const std::string& path) {
    if (layerIndex < MaxLayers) {
        m_Layers[layerIndex].HeightMapPath = path;
    }
}

void TerrainComponent::SetLayerNormalMapPath(uint32_t layerIndex, const std::string& path) {
    if (layerIndex < MaxLayers) {
        m_Layers[layerIndex].NormalMapPath = path;
    }
}

void TerrainComponent::SetLayerTilingScale(uint32_t layerIndex, float tilingScale) {
    if (layerIndex >= MaxLayers) {
        return;
    }
    m_Layers[layerIndex].TilingScale = std::clamp(tilingScale, 0.02f, 20.0f);
}

void TerrainComponent::SetLayerGlossiness(uint32_t layerIndex, float glossiness) {
    if (layerIndex < MaxLayers) {
        m_Layers[layerIndex].Glossiness = std::clamp(glossiness, 0.0f, 1.0f);
    }
}

const TerrainLayer& TerrainComponent::GetLayer(uint32_t layerIndex) const {
    return m_Layers[std::min(layerIndex, MaxLayers - 1u)];
}

TerrainLayer& TerrainComponent::GetLayer(uint32_t layerIndex) {
    return m_Layers[std::min(layerIndex, MaxLayers - 1u)];
}

std::array<uint8_t, TerrainComponent::MaxLayers> TerrainComponent::NormalizeWeights8(
    const std::array<float, MaxLayers>& weights,
    uint32_t priorityChannel) {
    std::array<float, MaxLayers> vals{};
    float sum = 0.0f;
    for (uint32_t i = 0; i < MaxLayers; ++i) {
        vals[i] = std::max(0.0f, weights[i]);
        sum += vals[i];
    }

    std::array<uint8_t, MaxLayers> out{};
    if (sum <= 1e-5f) {
        out[0] = 255u;
        return out;
    }

    std::array<int, MaxLayers> ints{};
    int intSum = 0;
    uint32_t maxIdx = 0;
    for (uint32_t i = 0; i < MaxLayers; ++i) {
        const float scaled = (vals[i] / sum) * 255.0f;
        ints[i] = std::clamp(static_cast<int>(std::lround(scaled)), 0, 255);
        intSum += ints[i];
        if (vals[i] > vals[maxIdx]) {
            maxIdx = i;
        }
    }

    int residual = 255 - intSum;
    const uint32_t anchorIdx = (priorityChannel < MaxLayers && vals[priorityChannel] > 0.0f)
        ? priorityChannel
        : maxIdx;
    auto applyResidual = [&](uint32_t idx) {
        if (residual == 0) {
            return;
        }
        const int nextVal = std::clamp(ints[idx] + residual, 0, 255);
        residual -= (nextVal - ints[idx]);
        ints[idx] = nextVal;
    };

    applyResidual(anchorIdx);
    applyResidual(maxIdx);
    for (uint32_t i = 0; i < MaxLayers && residual != 0; ++i) {
        applyResidual(i);
    }

    for (uint32_t i = 0; i < MaxLayers; ++i) {
        out[i] = static_cast<uint8_t>(ints[i]);
    }
    return out;
}

glm::u8vec4 TerrainComponent::NormalizeWeights(float r, float g, float b, float a) {
    const std::array<uint8_t, MaxLayers> n = NormalizeWeights8({ r, g, b, a, 0.0f, 0.0f, 0.0f, 0.0f }, 0u);
    return { n[0], n[1], n[2], n[3] };
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
    std::fill(m_SplatData.begin(), m_SplatData.end(), static_cast<uint8_t>(0));
    std::fill(m_SplatData1.begin(), m_SplatData1.end(), static_cast<uint8_t>(0));

    std::vector<uint8_t>& targetBuf = (clampedLayer < 4u) ? m_SplatData : m_SplatData1;
    const size_t channelOffset = static_cast<size_t>(clampedLayer % 4u);
    for (size_t idx = 0; idx < targetBuf.size(); idx += 4u) {
        targetBuf[idx + channelOffset] = 255u;
    }

    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
    ENGINE_CORE_DEBUG("TerrainComponent: Filled splatmap with Layer {}", clampedLayer);
}

glm::u8vec4 TerrainComponent::GetSplatTexel(uint32_t px, uint32_t pz) const {
    if (px >= m_SplatWidth || pz >= m_SplatHeight || m_SplatData.empty()) {
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

glm::u8vec4 TerrainComponent::GetSplatTexel1(uint32_t px, uint32_t pz) const {
    if (px >= m_SplatWidth || pz >= m_SplatHeight || m_SplatData1.empty()) {
        return { 0, 0, 0, 0 };
    }
    const size_t idx = (static_cast<size_t>(pz) * m_SplatWidth + px) * 4u;
    return {
        m_SplatData1[idx + 0],
        m_SplatData1[idx + 1],
        m_SplatData1[idx + 2],
        m_SplatData1[idx + 3]
    };
}

std::array<uint8_t, TerrainComponent::MaxLayers> TerrainComponent::GetSplatTexel8(uint32_t px, uint32_t pz) const {
    const glm::u8vec4 w0 = GetSplatTexel(px, pz);
    const glm::u8vec4 w1 = GetSplatTexel1(px, pz);
    return { w0.r, w0.g, w0.b, w0.a, w1.r, w1.g, w1.b, w1.a };
}

void TerrainComponent::SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights) {
    SetSplatTexel8(px, pz, { weights.r, weights.g, weights.b, weights.a, 0u, 0u, 0u, 0u });
}

void TerrainComponent::SetSplatTexel8(uint32_t px, uint32_t pz, const std::array<uint8_t, MaxLayers>& weights) {
    if (px >= m_SplatWidth || pz >= m_SplatHeight) {
        return;
    }
    std::array<float, MaxLayers> floatWeights{};
    for (uint32_t c = 0; c < MaxLayers; ++c) {
        floatWeights[c] = static_cast<float>(weights[c]);
    }
    const std::array<uint8_t, MaxLayers> norm = NormalizeWeights8(floatWeights, 0u);
    const size_t idx = (static_cast<size_t>(pz) * m_SplatWidth + px) * 4u;
    for (uint32_t c = 0; c < 4u; ++c) {
        m_SplatData[idx + c] = norm[c];
        m_SplatData1[idx + c] = norm[c + 4u];
    }

    MarkDirtyRect(px, pz, px, pz, true);
}

bool TerrainComponent::PaintSplatBufferXZ(std::vector<uint8_t>& splatData0,
                                            std::vector<uint8_t>& splatData1,
                                            uint32_t splatWidth,
                                            uint32_t splatHeight,
                                            int32_t chunkX,
                                            int32_t chunkZ,
                                            float originX,
                                            float originZ,
                                            float worldWidth,
                                            float worldDepth,
                                            bool nodeAligned,
                                            float brushWorldX,
                                            float brushWorldZ,
                                            uint32_t layerIndex,
                                            float radiusWorld,
                                            float falloff,
                                            float strength,
                                            uint32_t& outDirtyMinX,
                                            uint32_t& outDirtyMinZ,
                                            uint32_t& outDirtyMaxX,
                                            uint32_t& outDirtyMaxZ) {
    const size_t requiredBytes = static_cast<size_t>(splatWidth) * static_cast<size_t>(splatHeight) * 4u;
    if (layerIndex >= MaxLayers || radiusWorld <= 0.0f || strength <= 0.0f ||
        splatWidth < 2u || splatHeight < 2u ||
        splatData0.size() < requiredBytes) {
        return false;
    }
    if (splatData1.size() < requiredBytes) {
        splatData1.assign(requiredBytes, 0u);
    }

    const float totalWidth = std::max(0.001f, worldWidth);
    const float totalDepth = std::max(0.001f, worldDepth);
    const float chunkOriginX = nodeAligned ? (originX + static_cast<float>(chunkX) * totalWidth) : originX;
    const float chunkOriginZ = nodeAligned ? (originZ + static_cast<float>(chunkZ) * totalDepth) : originZ;

    const float minBrushX = brushWorldX - radiusWorld;
    const float maxBrushX = brushWorldX + radiusWorld;
    const float minBrushZ = brushWorldZ - radiusWorld;
    const float maxBrushZ = brushWorldZ + radiusWorld;

    if (maxBrushX < chunkOriginX || minBrushX > chunkOriginX + totalWidth ||
        maxBrushZ < chunkOriginZ || minBrushZ > chunkOriginZ + totalDepth) {
        return false;
    }

    const uint32_t spanX = nodeAligned ? (splatWidth - 1u) : splatWidth;
    const uint32_t spanZ = nodeAligned ? (splatHeight - 1u) : splatHeight;

    const float uMin = std::clamp((minBrushX - chunkOriginX) / totalWidth, 0.0f, 1.0f);
    const float uMax = std::clamp((maxBrushX - chunkOriginX) / totalWidth, 0.0f, 1.0f);
    const float vMin = std::clamp((minBrushZ - chunkOriginZ) / totalDepth, 0.0f, 1.0f);
    const float vMax = std::clamp((maxBrushZ - chunkOriginZ) / totalDepth, 0.0f, 1.0f);

    const uint32_t pxMin = static_cast<uint32_t>(std::floor(uMin * static_cast<float>(spanX)));
    const uint32_t pxMax = std::min(splatWidth - 1u, static_cast<uint32_t>(std::ceil(uMax * static_cast<float>(spanX))));
    const uint32_t pzMin = static_cast<uint32_t>(std::floor(vMin * static_cast<float>(spanZ)));
    const uint32_t pzMax = std::min(splatHeight - 1u, static_cast<uint32_t>(std::ceil(vMax * static_cast<float>(spanZ))));

    const float clampedFalloff = std::clamp(falloff, 0.0f, 1.0f);
    const float innerStart = std::clamp(1.0f - clampedFalloff, 0.0f, 0.999f);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);

    std::array<float, MaxLayers> target{};
    target[layerIndex] = 255.0f;

    const float cellSizeX = totalWidth / static_cast<float>(spanX);
    const float cellSizeZ = totalDepth / static_cast<float>(spanZ);

    bool anyModified = false;
    uint32_t modMinX = pxMax;
    uint32_t modMinZ = pzMax;
    uint32_t modMaxX = pxMin;
    uint32_t modMaxZ = pzMin;

    for (uint32_t pz = pzMin; pz <= pzMax; ++pz) {
        const float texelZ = nodeAligned
            ? (originZ + static_cast<float>(static_cast<int64_t>(chunkZ) * static_cast<int64_t>(spanZ) + static_cast<int64_t>(pz)) * cellSizeZ)
            : (originZ + ((static_cast<float>(pz) + 0.5f) / static_cast<float>(splatHeight)) * totalDepth);
        const float dz = texelZ - brushWorldZ;

        for (uint32_t px = pxMin; px <= pxMax; ++px) {
            const float texelX = nodeAligned
                ? (originX + static_cast<float>(static_cast<int64_t>(chunkX) * static_cast<int64_t>(spanX) + static_cast<int64_t>(px)) * cellSizeX)
                : (originX + ((static_cast<float>(px) + 0.5f) / static_cast<float>(splatWidth)) * totalWidth);
            const float dx = texelX - brushWorldX;
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

            const size_t idx = (static_cast<size_t>(pz) * splatWidth + px) * 4u;
            std::array<float, MaxLayers> blended{};
            for (uint32_t c = 0; c < 4u; ++c) {
                blended[c] = static_cast<float>(splatData0[idx + c]) * (1.0f - blendAlpha) + target[c] * blendAlpha;
                blended[c + 4u] = static_cast<float>(splatData1[idx + c]) * (1.0f - blendAlpha) + target[c + 4u] * blendAlpha;
            }

            const std::array<uint8_t, MaxLayers> norm = NormalizeWeights8(blended, layerIndex);
            for (uint32_t c = 0; c < 4u; ++c) {
                splatData0[idx + c] = norm[c];
                splatData1[idx + c] = norm[c + 4u];
            }

            anyModified = true;
            modMinX = std::min(modMinX, px);
            modMinZ = std::min(modMinZ, pz);
            modMaxX = std::max(modMaxX, px);
            modMaxZ = std::max(modMaxZ, pz);
        }
    }

    if (anyModified) {
        outDirtyMinX = modMinX;
        outDirtyMinZ = modMinZ;
        outDirtyMaxX = modMaxX;
        outDirtyMaxZ = modMaxZ;
    }
    return anyModified;
}

void TerrainComponent::PaintWorldBrush(float worldX,
                                       float worldZ,
                                       const glm::vec3& terrainOrigin,
                                       const glm::vec3& terrainScale,
                                       uint32_t layerIndex,
                                       float radiusWorld,
                                       float falloff,
                                       float strength) {
    uint32_t minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    const float totalWidth = std::max(0.001f, m_Width * std::abs(terrainScale.x));
    const float totalDepth = std::max(0.001f, m_Depth * std::abs(terrainScale.z));
    if (PaintSplatBufferXZ(m_SplatData,
                           m_SplatData1,
                           m_SplatWidth,
                           m_SplatHeight,
                           0,
                           0,
                           terrainOrigin.x,
                           terrainOrigin.z,
                           totalWidth,
                           totalDepth,
                           /*nodeAligned=*/false,
                           worldX,
                           worldZ,
                           layerIndex,
                           radiusWorld,
                           falloff,
                           strength,
                           minX,
                           minZ,
                           maxX,
                           maxZ)) {
        MarkDirtyRect(minX, minZ, maxX, maxZ, true);
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

SurfaceSample TerrainComponent::SampleSurfaceAtXZ(float worldX,
                                                  float worldZ,
                                                  const glm::vec3& terrainOrigin,
                                                  const glm::vec3& terrainScale) const noexcept {
    SurfaceSample sample{};
    if (m_SplatData.empty() || m_SplatWidth < 2u || m_SplatHeight < 2u) {
        return sample;
    }

    const float totalWidth = std::max(0.001f, m_Width * std::abs(terrainScale.x));
    const float totalDepth = std::max(0.001f, m_Depth * std::abs(terrainScale.z));

    const float relX = worldX - terrainOrigin.x;
    const float relZ = worldZ - terrainOrigin.z;

    const float u = std::clamp(relX / totalWidth, 0.0f, 1.0f);
    const float v = std::clamp(relZ / totalDepth, 0.0f, 1.0f);

    const float fx = std::clamp(u * static_cast<float>(m_SplatWidth) - 0.5f, 0.0f, static_cast<float>(m_SplatWidth - 1u));
    const float fz = std::clamp(v * static_cast<float>(m_SplatHeight) - 0.5f, 0.0f, static_cast<float>(m_SplatHeight - 1u));

    const auto x0 = static_cast<uint32_t>(std::floor(fx));
    const auto z0 = static_cast<uint32_t>(std::floor(fz));
    const uint32_t x1 = std::min(x0 + 1u, m_SplatWidth - 1u);
    const uint32_t z1 = std::min(z0 + 1u, m_SplatHeight - 1u);

    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const auto w00 = GetSplatTexel8(x0, z0);
    const auto w10 = GetSplatTexel8(x1, z0);
    const auto w01 = GetSplatTexel8(x0, z1);
    const auto w11 = GetSplatTexel8(x1, z1);

    std::array<float, MaxLayers> blendedWeights{};
    float sumWeights = 0.0f;
    for (size_t i = 0; i < MaxLayers; ++i) {
        const float top = std::lerp(static_cast<float>(w00[i]), static_cast<float>(w10[i]), tx);
        const float bot = std::lerp(static_cast<float>(w01[i]), static_cast<float>(w11[i]), tx);
        const float val = std::lerp(top, bot, tz);
        blendedWeights[i] = val;
        sumWeights += val;
    }

    if (sumWeights <= 1e-4f) {
        blendedWeights[1] = 1.0f; // Default Ash Barrens
        sumWeights = 1.0f;
    }

    float invSum = 1.0f / sumWeights;
    float maxWeight = -1.0f;
    uint32_t dominantLayer = 1u;
    float weightedDamping = 0.0f;
    float weightedSpeed = 0.0f;

    for (size_t i = 0; i < MaxLayers; ++i) {
        const float normW = blendedWeights[i] * invSum;
        if (normW > maxWeight) {
            maxWeight = normW;
            dominantLayer = static_cast<uint32_t>(i);
        }
        weightedDamping += normW * SurfacePhysics::LayerDamping[i];
        weightedSpeed += normW * SurfacePhysics::LayerSpeedMultiplier[i];
    }

    sample.DominantBiome = SurfacePhysics::LayerToBiome(dominantLayer);
    sample.LinearDamping = weightedDamping;
    sample.MaxSpeedMultiplier = weightedSpeed;
    sample.Flammability = std::clamp(blendedWeights[2] * invSum, 0.0f, 1.0f);
    sample.IsBurning = false;

    return sample;
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

    // Companion PNG for layers 4..7 (written only when any secondary weight is non-zero)
    const std::filesystem::path mainPath(resolvedFilepath);
    const std::filesystem::path secondaryPath = mainPath.parent_path() /
        (mainPath.stem().string() + "_layers4to7" + mainPath.extension().string());
    const bool hasSecondaryWeights = std::any_of(m_SplatData1.begin(), m_SplatData1.end(), [](uint8_t b) { return b != 0u; });
    if (hasSecondaryWeights && m_SplatData1.size() == m_SplatData.size()) {
        stbi_write_png(secondaryPath.string().c_str(),
                       static_cast<int>(m_SplatWidth),
                       static_cast<int>(m_SplatHeight),
                       4,
                       m_SplatData1.data(),
                       strideBytes);
    } else if (std::filesystem::exists(secondaryPath, ec)) {
        std::filesystem::remove(secondaryPath, ec);
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
    stbi_set_flip_vertically_on_load_thread(0);
    stbi_set_flip_vertically_on_load(0);
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(resolvedFilepath.c_str(), &w, &h, &channels, 4);

    if (!pixels || w <= 0 || h <= 0) {
        stbi_set_flip_vertically_on_load_thread(1);
        stbi_set_flip_vertically_on_load(1);
        ENGINE_CORE_ERROR("TerrainComponent: Failed to load splatmap PNG '{}'", resolvedFilepath);
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const std::filesystem::path mainPath(resolvedFilepath);
    const std::filesystem::path secondaryPath = mainPath.parent_path() /
        (mainPath.stem().string() + "_layers4to7" + mainPath.extension().string());
    int w1 = 0, h1 = 0, ch1 = 0;
    unsigned char* secPixels = stbi_load(secondaryPath.string().c_str(), &w1, &h1, &ch1, 4);

    stbi_set_flip_vertically_on_load_thread(1);
    stbi_set_flip_vertically_on_load(1);

    m_SplatWidth = static_cast<uint32_t>(w);
    m_SplatHeight = static_cast<uint32_t>(h);
    const size_t byteCount = static_cast<size_t>(m_SplatWidth) * static_cast<size_t>(m_SplatHeight) * 4u;
    m_SplatData.resize(byteCount);
    m_SplatData1.assign(byteCount, 0u);

    const bool useSecondary = (secPixels != nullptr && w1 == w && h1 == h);

    for (size_t idx = 0; idx < byteCount; idx += 4u) {
        const int r = pixels[idx + 0];
        const int g = pixels[idx + 1];
        const int b = pixels[idx + 2];
        const int a = pixels[idx + 3];

        int w3 = a;
        if (a == 255) {
            const int sum4to7 = useSecondary
                ? (secPixels[idx + 0] + secPixels[idx + 1] + secPixels[idx + 2] + secPixels[idx + 3])
                : 0;
            const int remaining = 255 - (r + g + b + sum4to7);
            w3 = std::max(0, remaining);
        }

        std::array<float, MaxLayers> weights{};
        weights[0] = static_cast<float>(r);
        weights[1] = static_cast<float>(g);
        weights[2] = static_cast<float>(b);
        weights[3] = static_cast<float>(w3);
        if (useSecondary) {
            weights[4] = static_cast<float>(secPixels[idx + 0]);
            weights[5] = static_cast<float>(secPixels[idx + 1]);
            weights[6] = static_cast<float>(secPixels[idx + 2]);
            weights[7] = static_cast<float>(secPixels[idx + 3]);
        }

        const std::array<uint8_t, MaxLayers> norm = NormalizeWeights8(weights, 0u);
        for (uint32_t c = 0; c < 4u; ++c) {
            m_SplatData[idx + c] = norm[c];
            m_SplatData1[idx + c] = norm[c + 4u];
        }
    }
    stbi_image_free(pixels);
    if (secPixels) {
        stbi_image_free(secPixels);
    }

    m_GpuSplatTexture.reset();
    m_GpuSplatTexture1.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, false);
    m_HasUnsavedMemoryEdits = false;
    return true;
}

std::string TerrainComponent::EncodeRGBABase64(const std::vector<uint8_t>& rgbaData) {
    std::string out;
    out.reserve(((rgbaData.size() + 2u) / 3u) * 4u);

    size_t i = 0;
    while (i + 2u < rgbaData.size()) {
        const uint32_t triple = (static_cast<uint32_t>(rgbaData[i]) << 16u) |
                                (static_cast<uint32_t>(rgbaData[i + 1u]) << 8u) |
                                static_cast<uint32_t>(rgbaData[i + 2u]);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[triple & 0x3Fu]);
        i += 3u;
    }

    const size_t rem = rgbaData.size() - i;
    if (rem == 1u) {
        const uint32_t triple = static_cast<uint32_t>(rgbaData[i]) << 16u;
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2u) {
        const uint32_t triple = (static_cast<uint32_t>(rgbaData[i]) << 16u) |
                                (static_cast<uint32_t>(rgbaData[i + 1u]) << 8u);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

bool TerrainComponent::DecodeRGBABase64(const std::string& base64Data,
                                        uint32_t width,
                                        uint32_t height,
                                        std::vector<uint8_t>& outRgbaData) {
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
        ENGINE_CORE_ERROR("TerrainComponent::DecodeRGBABase64 byte size mismatch (got {}, expected {})",
                          decoded.size(), expectedBytes);
        return false;
    }

    outRgbaData = std::move(decoded);
    return true;
}

std::string TerrainComponent::EncodeSplatBase64() const {
    return EncodeRGBABase64(m_SplatData);
}

bool TerrainComponent::DecodeSplatBase64(const std::string& base64Data, uint32_t width, uint32_t height) {
    std::vector<uint8_t> decoded;
    if (!DecodeRGBABase64(base64Data, width, height, decoded)) {
        return false;
    }

    m_SplatWidth = width;
    m_SplatHeight = height;
    m_SplatData = std::move(decoded);
    m_SplatData1.assign(m_SplatData.size(), 0u);
    m_GpuSplatTexture.reset();
    m_GpuSplatTexture1.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
    ENGINE_CORE_DEBUG("TerrainComponent: Decoded base64 splatmap ({} bytes, {}x{})",
                      m_SplatData.size(), m_SplatWidth, m_SplatHeight);
    return true;
}

std::string TerrainComponent::EncodeSplat1Base64() const {
    return EncodeRGBABase64(m_SplatData1);
}

bool TerrainComponent::DecodeSplat1Base64(const std::string& base64Data, uint32_t width, uint32_t height) {
    std::vector<uint8_t> decoded;
    if (!DecodeRGBABase64(base64Data, width, height, decoded)) {
        return false;
    }
    if (width != m_SplatWidth || height != m_SplatHeight) {
        return false;
    }
    m_SplatData1 = std::move(decoded);
    m_GpuSplatTexture1.reset();
    MarkDirtyRect(0, 0, m_SplatWidth - 1u, m_SplatHeight - 1u, true);
    return true;
}

const std::shared_ptr<Texture>& TerrainComponent::SyncRgbaGpuTexture(std::shared_ptr<Texture>& gpuTex,
                                                                       std::vector<uint8_t>& uploadScratch,
                                                                       const std::vector<uint8_t>& splatData,
                                                                       uint32_t splatWidth,
                                                                       uint32_t splatHeight,
                                                                       bool& inOutGpuDirty,
                                                                       uint32_t dirtyMinX,
                                                                       uint32_t dirtyMinY,
                                                                       uint32_t dirtyMaxX,
                                                                       uint32_t dirtyMaxY) {
    if (splatWidth == 0 || splatHeight == 0 ||
        splatData.size() < static_cast<size_t>(splatWidth) * static_cast<size_t>(splatHeight) * 4u) {
        return gpuTex;
    }

    if (!gpuTex ||
        gpuTex->GetWidth() != static_cast<int>(splatWidth) ||
        gpuTex->GetHeight() != static_cast<int>(splatHeight)) {
        gpuTex = std::make_shared<Texture>(
            splatWidth,
            splatHeight,
            splatData.data(),
            /*linearFilter=*/true,
            /*repeatWrap=*/false
        );
        inOutGpuDirty = false;
        return gpuTex;
    }

    if (inOutGpuDirty) {
        const uint32_t minX = std::min(dirtyMinX, splatWidth - 1u);
        const uint32_t minY = std::min(dirtyMinY, splatHeight - 1u);
        const uint32_t maxX = std::min(std::max(minX, dirtyMaxX), splatWidth - 1u);
        const uint32_t maxY = std::min(std::max(minY, dirtyMaxY), splatHeight - 1u);

        const uint32_t subW = maxX - minX + 1u;
        const uint32_t subH = maxY - minY + 1u;

        if (subW == splatWidth && subH == splatHeight) {
            gpuTex->SetData(splatData.data(), static_cast<uint32_t>(splatData.size()));
        } else {
            uploadScratch.resize(static_cast<size_t>(subW) * static_cast<size_t>(subH) * 4u);
            for (uint32_t row = 0; row < subH; ++row) {
                const size_t srcOffset = (static_cast<size_t>(minY + row) * splatWidth + minX) * 4u;
                const size_t dstOffset = static_cast<size_t>(row) * subW * 4u;
                std::memcpy(&uploadScratch[dstOffset], &splatData[srcOffset], static_cast<size_t>(subW) * 4u);
            }
            gpuTex->SetSubData(minX, minY, subW, subH, uploadScratch.data());
        }
        inOutGpuDirty = false;
    }

    return gpuTex;
}

const std::shared_ptr<Texture>& TerrainComponent::SyncGpuSplatTexture() {
    bool dirty0 = m_GpuDirty;
    bool dirty1 = m_GpuDirty;
    SyncRgbaGpuTexture(m_GpuSplatTexture,
                       m_UploadScratch,
                       m_SplatData,
                       m_SplatWidth,
                       m_SplatHeight,
                       dirty0,
                       m_DirtyMinX,
                       m_DirtyMinY,
                       m_DirtyMaxX,
                       m_DirtyMaxY);
    SyncRgbaGpuTexture(m_GpuSplatTexture1,
                       m_UploadScratch,
                       m_SplatData1,
                       m_SplatWidth,
                       m_SplatHeight,
                       dirty1,
                       m_DirtyMinX,
                       m_DirtyMinY,
                       m_DirtyMaxX,
                       m_DirtyMaxY);
    m_GpuDirty = dirty0 || dirty1;
    return m_GpuSplatTexture;
}

std::pair<std::shared_ptr<Texture>, std::shared_ptr<Texture>> TerrainComponent::SyncGpuSplatTextures() {
    SyncGpuSplatTexture();
    return { m_GpuSplatTexture, m_GpuSplatTexture1 };
}

} // namespace Engine
