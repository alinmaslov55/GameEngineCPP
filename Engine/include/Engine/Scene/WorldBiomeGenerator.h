#ifndef SCENE_WORLDBIOMEGENERATOR_H
#define SCENE_WORLDBIOMEGENERATOR_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

/// @brief Immutable, thread-safe 2D Simplex / fBm / domain-warp noise evaluator
class NoiseGenerator {
public:
    explicit NoiseGenerator(uint64_t seed = 42ULL) noexcept;

    [[nodiscard]] float Simplex2D(float x, float z) const noexcept;
    [[nodiscard]] float FBm2D(float x, float z, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
    [[nodiscard]] glm::vec2 DomainWarp2D(float x, float z, float amplitude = 6.0f) const noexcept;
    [[nodiscard]] uint64_t GetSeed() const noexcept { return m_Seed; }

private:
    uint64_t m_Seed = 42ULL;
    std::array<uint8_t, 512> m_Perm{};
};

enum class BiomeType : uint8_t {
    BlackwoodThickets = 0, // Dense trees + hiding grass, Friction = 0.75
    AshBarrens        = 1, // Safe flat soot, zero wood, Friction = 0.55
    TarBogs           = 2, // Flammable sludge, 30% slow, Friction = 0.95
    ChalkHighlands    = 3, // Slippery limestone, long knockback slides, Friction = 0.20
    SanctuaryCobble   = 4  // Origin safe ring & POI plazas, Friction = 0.65
};

enum class ProceduralPropKind : uint8_t {
    ConiferPine = 0,
    BerryShrub,
    TallGrassCluster,
    DeadSnag,
    BogBubbleEmitter,
    BrokenFence,
    BonePile,
    POI_DeadBrazier,
    POI_UmbralShrine
};

struct BiomeSample {
    std::array<float, 5> Weights{};
    BiomeType DominantBiome = BiomeType::BlackwoodThickets;
    float Moisture = 0.0f;
    float Elevation = 0.0f;

    [[nodiscard]] glm::u8vec4 ToSplatRGBA8() const noexcept;
    [[nodiscard]] std::array<uint8_t, 8> ToSplat8() const noexcept;
};

struct PropSpawnDesc {
    ProceduralPropKind Kind = ProceduralPropKind::TallGrassCluster;
    BiomeType SourceBiome = BiomeType::BlackwoodThickets;
    glm::vec3 WorldPosition{ 0.0f };
    float ScaleMultiplier = 1.0f;
    float WindPhaseOffset = 0.0f;
    bool Blocking = false;
};

struct POISpawnDesc {
    ProceduralPropKind Kind = ProceduralPropKind::POI_DeadBrazier;
    int32_t SuperCellX = 0;
    int32_t SuperCellZ = 0;
    glm::vec3 WorldPosition{ 0.0f };
    float ApronRadius = 3.5f;
};

struct WorldGenConfig {
    bool Enabled = false;
    bool SpawnSceneEntities = true;
    uint64_t Seed = 42ULL;
    float OriginSafeRadius = 6.5f;
    float PoiApronRadius = 3.5f;
    int32_t PoiSuperCellChunks = 3;
};

/// @brief Pure, thread-safe biome, POI, and Poisson prop scatter evaluator shared by worker threads
class WorldBiomeGenerator {
public:
    explicit WorldBiomeGenerator(WorldGenConfig config = {}) noexcept;

    [[nodiscard]] static constexpr uint64_t SplitMix64(uint64_t x) noexcept {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
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
