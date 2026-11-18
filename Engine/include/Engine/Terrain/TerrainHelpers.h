#ifndef SCENE_TERRAINHELPERS_H
#define SCENE_TERRAINHELPERS_H

#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/SurfaceSample.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Engine::TerrainHelpers {

inline void SetLayerTexture(std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                            uint32_t layerIndex,
                            const std::shared_ptr<Texture>& texture) {
    if (layerIndex >= TerrainComponent::MaxLayers) {
        return;
    }
    if (texture) {
        texture->SetWrapRepeat(true);
    }
    layers[layerIndex].LayerTexture = texture;
}

inline void SetLayerHeightMapPath(std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                                  uint32_t layerIndex,
                                  const std::string& path) {
    if (layerIndex < TerrainComponent::MaxLayers) {
        layers[layerIndex].HeightMapPath = path;
    }
}

inline void SetLayerNormalMapPath(std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                                  uint32_t layerIndex,
                                  const std::string& path) {
    if (layerIndex < TerrainComponent::MaxLayers) {
        layers[layerIndex].NormalMapPath = path;
    }
}

inline void SetLayerTilingScale(std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                                uint32_t layerIndex,
                                float tilingScale) noexcept {
    if (layerIndex < TerrainComponent::MaxLayers) {
        layers[layerIndex].TilingScale = std::clamp(tilingScale, 0.02f, 20.0f);
    }
}

inline void SetLayerGlossiness(std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                               uint32_t layerIndex,
                               float glossiness) noexcept {
    if (layerIndex < TerrainComponent::MaxLayers) {
        layers[layerIndex].Glossiness = std::clamp(glossiness, 0.0f, 1.0f);
    }
}

[[nodiscard]] inline glm::u8vec4 ReadSplatRgbaTexel(const std::vector<uint8_t>& buffer,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    uint32_t px,
                                                    uint32_t pz,
                                                    const glm::u8vec4& fallback) noexcept {
    const size_t required = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (px >= width || pz >= height || buffer.size() < required) {
        return fallback;
    }
    const size_t idx = (static_cast<size_t>(pz) * width + px) * 4u;
    return {
        buffer[idx + 0],
        buffer[idx + 1],
        buffer[idx + 2],
        buffer[idx + 3]
    };
}

[[nodiscard]] inline std::array<uint8_t, TerrainComponent::MaxLayers> ReadSplatTexel8(
    const std::vector<uint8_t>& splat0,
    const std::vector<uint8_t>& splat1,
    uint32_t width,
    uint32_t height,
    uint32_t px,
    uint32_t pz) noexcept {
    const glm::u8vec4 w0 = ReadSplatRgbaTexel(splat0, width, height, px, pz, { 255, 0, 0, 0 });
    const glm::u8vec4 w1 = ReadSplatRgbaTexel(splat1, width, height, px, pz, { 0, 0, 0, 0 });
    return { w0.r, w0.g, w0.b, w0.a, w1.r, w1.g, w1.b, w1.a };
}

inline bool WriteSplatTexel8(std::vector<uint8_t>& splat0,
                             std::vector<uint8_t>& splat1,
                             uint32_t width,
                             uint32_t height,
                             uint32_t px,
                             uint32_t pz,
                             const std::array<uint8_t, TerrainComponent::MaxLayers>& weights) {
    if (px >= width || pz >= height) {
        return false;
    }
    std::array<float, TerrainComponent::MaxLayers> floatWeights{};
    for (uint32_t c = 0; c < TerrainComponent::MaxLayers; ++c) {
        floatWeights[c] = static_cast<float>(weights[c]);
    }
    const std::array<uint8_t, TerrainComponent::MaxLayers> norm = TerrainComponent::NormalizeWeights8(floatWeights, 0u);
    const size_t requiredBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (splat0.size() < requiredBytes) {
        splat0.assign(requiredBytes, 0u);
    }
    if (splat1.size() < requiredBytes) {
        splat1.assign(requiredBytes, 0u);
    }
    const size_t idx = (static_cast<size_t>(pz) * width + px) * 4u;
    for (uint32_t c = 0; c < 4u; ++c) {
        splat0[idx + c] = norm[c];
        splat1[idx + c] = norm[c + 4u];
    }
    return true;
}

[[nodiscard]] inline SurfaceSample EvaluateSurfaceFromWeights8(
    std::array<float, TerrainComponent::MaxLayers> weights,
    float sumWeights,
    bool isBurning) noexcept {
    if (sumWeights <= 1e-4f) {
        weights[1] = 1.0f; // Default Ash Barrens
        sumWeights = 1.0f;
    }

    const float invSum = 1.0f / sumWeights;
    float maxWeight = -1.0f;
    uint32_t dominantLayer = 1u;
    float weightedDamping = 0.0f;
    float weightedSpeed = 0.0f;

    for (size_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const float normW = weights[i] * invSum;
        if (normW > maxWeight) {
            maxWeight = normW;
            dominantLayer = static_cast<uint32_t>(i);
        }
        weightedDamping += normW * SurfacePhysics::LayerDamping[i];
        weightedSpeed += normW * SurfacePhysics::LayerSpeedMultiplier[i];
    }

    SurfaceSample sample{};
    sample.DominantBiome = SurfacePhysics::LayerToBiome(dominantLayer);
    sample.LinearDamping = weightedDamping;
    sample.MaxSpeedMultiplier = weightedSpeed;
    sample.Flammability = std::clamp(weights[2] * invSum, 0.0f, 1.0f);
    sample.IsBurning = isBurning;
    return sample;
}

[[nodiscard]] inline SurfaceSample EvaluateBilinearSurfaceSample(const std::vector<uint8_t>& splat0,
                                                                 const std::vector<uint8_t>& splat1,
                                                                 uint32_t width,
                                                                 uint32_t height,
                                                                 float fx,
                                                                 float fz,
                                                                 bool isBurning) noexcept {
    const float clampedFx = std::clamp(fx, 0.0f, static_cast<float>(width - 1u));
    const float clampedFz = std::clamp(fz, 0.0f, static_cast<float>(height - 1u));

    const auto x0 = static_cast<uint32_t>(std::floor(clampedFx));
    const auto z0 = static_cast<uint32_t>(std::floor(clampedFz));
    const uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const uint32_t z1 = std::min(z0 + 1u, height - 1u);

    const float tx = clampedFx - static_cast<float>(x0);
    const float tz = clampedFz - static_cast<float>(z0);

    const auto w00 = ReadSplatTexel8(splat0, splat1, width, height, x0, z0);
    const auto w10 = ReadSplatTexel8(splat0, splat1, width, height, x1, z0);
    const auto w01 = ReadSplatTexel8(splat0, splat1, width, height, x0, z1);
    const auto w11 = ReadSplatTexel8(splat0, splat1, width, height, x1, z1);

    std::array<float, TerrainComponent::MaxLayers> blendedWeights{};
    float sumWeights = 0.0f;
    for (size_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const float top = std::lerp(static_cast<float>(w00[i]), static_cast<float>(w10[i]), tx);
        const float bot = std::lerp(static_cast<float>(w01[i]), static_cast<float>(w11[i]), tx);
        const float val = std::lerp(top, bot, tz);
        blendedWeights[i] = val;
        sumWeights += val;
    }

    return EvaluateSurfaceFromWeights8(blendedWeights, sumWeights, isBurning);
}

} // namespace Engine::TerrainHelpers

#endif // SCENE_TERRAINHELPERS_H
