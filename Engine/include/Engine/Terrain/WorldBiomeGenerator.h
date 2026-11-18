#ifndef SCENE_WORLDBIOMEGENERATOR_H
#define SCENE_WORLDBIOMEGENERATOR_H

#include "Engine/Math/NoiseGenerator.h"
#include "Engine/Terrain/WorldGenTypes.h"

#include <cstdint>
#include <vector>

namespace Engine {

/// @brief Pure, thread-safe biome, POI, and Poisson prop scatter evaluator shared by worker threads
class WorldBiomeGenerator {
public:
    explicit WorldBiomeGenerator(WorldGenConfig config = {}) noexcept;

    [[nodiscard]] static constexpr uint64_t SplitMix64(uint64_t x) noexcept {
        return NoiseGenerator::SplitMix64(x);
    }

    [[nodiscard]] static constexpr uint64_t HashCombine(uint64_t seed, int32_t cx, int32_t cz) noexcept {
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                                 static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return SplitMix64(seed ^ SplitMix64(packed));
    }

    [[nodiscard]] static constexpr int32_t FloorDiv(int32_t a, int32_t b) noexcept {
        int32_t q = a / b;
        int32_t r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) {
            --q;
        }
        return q;
    }

    [[nodiscard]] static constexpr float GetBiomeFriction(BiomeType biome) noexcept {
        switch (biome) {
            case BiomeType::BlackwoodThickets: return 0.75f;
            case BiomeType::AshBarrens:        return 0.55f;
            case BiomeType::TarBogs:           return 0.95f;
            case BiomeType::ChalkHighlands:    return 0.20f;
            case BiomeType::SanctuaryCobble:   return 0.65f;
            default:                           return 0.65f;
        }
    }

    [[nodiscard]] const WorldGenConfig& GetConfig() const noexcept { return m_Config; }
    [[nodiscard]] const NoiseGenerator& GetNoise() const noexcept { return m_Noise; }

    [[nodiscard]] POISpawnDesc ComputeSuperCellPOI(int32_t superX, int32_t superZ, float chunkSizeMeters = 16.0f) const noexcept;
    [[nodiscard]] BiomeSample EvaluateBiomeAtXZ(float worldX, float worldZ, float chunkSizeMeters = 16.0f) const noexcept;
    [[nodiscard]] BiomeSample EvaluateBiomeAtXZWithPOIs(float worldX,
                                                         float worldZ,
                                                         float chunkSizeMeters,
                                                         const std::vector<POISpawnDesc>& candidatePOIs) const noexcept;

    void GenerateChunkData(int32_t cx,
                           int32_t cz,
                           float chunkSizeMeters,
                           uint32_t splatResolution,
                           std::vector<uint8_t>& outSplatRGBA,
                           std::vector<uint8_t>& outSplatRGBA1,
                           std::vector<BiomeType>& outBiomeMap,
                           std::vector<PropSpawnDesc>& outProps,
                           std::vector<POISpawnDesc>& outPOIs) const;

private:
    WorldGenConfig m_Config{};
    NoiseGenerator m_Noise;
};

} // namespace Engine

#endif // SCENE_WORLDBIOMEGENERATOR_H
