#ifndef SCENE_WORLDGENTYPES_H
#define SCENE_WORLDGENTYPES_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

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

} // namespace Engine

#endif // SCENE_WORLDGENTYPES_H
