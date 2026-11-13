#include "Engine/Scene/WorldBiomeGenerator.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Engine {

namespace {

inline float SmoothStep(float edge0, float edge1, float x) noexcept {
    if (edge1 <= edge0) {
        return (x >= edge1) ? 1.0f : 0.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float HashToUnitFloat(uint64_t h) noexcept {
    constexpr double kInv24 = 1.0 / 16777216.0;
    return static_cast<float>(static_cast<double>((h >> 40) & 0xFFFFFFULL) * kInv24);
}

struct PropTierSpec {
    uint64_t TierSalt = 0;
    float MinDist = 1.0f;
    float TrunkClearance = 0.0f;
    float OccupancyChance = 1.0f;
    float ScaleJitterMin = 0.90f;
    float ScaleJitterMax = 1.15f;
    bool (*SelectProp)(BiomeType biome, ProceduralPropKind& outKind, bool& outBlocking) noexcept = nullptr;
};

bool SelectTier0BlockerProp(BiomeType biome, ProceduralPropKind& outKind, bool& outBlocking) noexcept {
    switch (biome) {
        case BiomeType::BlackwoodThickets:
            outKind = ProceduralPropKind::ConiferPine;
            outBlocking = true;
            return true;
        case BiomeType::TarBogs:
            outKind = ProceduralPropKind::DeadSnag;
            outBlocking = true;
            return true;
        default:
            return false;
    }
}

bool SelectTier1MediumProp(BiomeType biome, ProceduralPropKind& outKind, bool& outBlocking) noexcept {
    switch (biome) {
        case BiomeType::BlackwoodThickets:
            outKind = ProceduralPropKind::BerryShrub;
            outBlocking = false;
            return true;
        case BiomeType::AshBarrens:
        case BiomeType::ChalkHighlands:
            outKind = ProceduralPropKind::BrokenFence;
            outBlocking = true;
            return true;
        default:
            return false;
    }
}

bool SelectTier2GroundProp(BiomeType biome, ProceduralPropKind& outKind, bool& outBlocking) noexcept {
    switch (biome) {
        case BiomeType::BlackwoodThickets:
            outKind = ProceduralPropKind::TallGrassCluster;
            outBlocking = false;
            return true;
        case BiomeType::TarBogs:
            outKind = ProceduralPropKind::BogBubbleEmitter;
            outBlocking = false;
            return true;
        case BiomeType::AshBarrens:
        case BiomeType::ChalkHighlands:
            outKind = ProceduralPropKind::BonePile;
            outBlocking = false;
            return true;
        default:
            return false;
    }
}

struct GridCandidate {
    int32_t CellX = 0;
    int32_t CellZ = 0;
    glm::vec2 PosXZ{ 0.0f };
    uint64_t PriorityHash = 0;
    bool PassesOccupancy = false;
};

GridCandidate EvaluateCellCandidate(uint64_t worldSeed,
                                    uint64_t tierSalt,
                                    int32_t cellX,
                                    int32_t cellZ,
                                    float cellSpacing,
                                    float occupancyChance) noexcept {
    const uint64_t h0 = WorldBiomeGenerator::HashCombine(worldSeed ^ tierSalt, cellX, cellZ);
    const uint64_t h1 = WorldBiomeGenerator::SplitMix64(h0 ^ 0xA5A5A5A5A5A5A5A5ULL);
    const uint64_t h2 = WorldBiomeGenerator::SplitMix64(h1 ^ 0x5A5A5A5A5A5A5A5AULL);

    const float rx = 0.12f + 0.76f * HashToUnitFloat(h0);
    const float rz = 0.12f + 0.76f * HashToUnitFloat(h1);
    const float occ = HashToUnitFloat(h2);

    GridCandidate out;
    out.CellX = cellX;
    out.CellZ = cellZ;
    out.PosXZ = { (static_cast<float>(cellX) + rx) * cellSpacing,
                  (static_cast<float>(cellZ) + rz) * cellSpacing };
    out.PriorityHash = h2;
    out.PassesOccupancy = (occ <= occupancyChance);
    return out;
}

bool HasHigherPriority(const GridCandidate& other, const GridCandidate& self) noexcept {
    if (other.PriorityHash != self.PriorityHash) {
        return other.PriorityHash > self.PriorityHash;
    }
    if (other.CellX != self.CellX) {
        return other.CellX > self.CellX;
    }
    return other.CellZ > self.CellZ;
}

bool IsInsideExclusionZone(const glm::vec2& posXZ,
                           float originSafeRadius,
                           const std::vector<POISpawnDesc>& nearbyPOIs) noexcept {
    if (glm::dot(posXZ, posXZ) < originSafeRadius * originSafeRadius) {
        return true;
    }
    for (const auto& poi : nearbyPOIs) {
        const glm::vec2 d = posXZ - glm::vec2(poi.WorldPosition.x, poi.WorldPosition.z);
        const float apron = std::max(0.5f, poi.ApronRadius);
        if (glm::dot(d, d) < apron * apron) {
            return true;
        }
    }
    return false;
}

} // namespace

NoiseGenerator::NoiseGenerator(uint64_t seed) noexcept
    : m_Seed(seed) {
    std::array<uint8_t, 256> base{};
    std::iota(base.begin(), base.end(), static_cast<uint8_t>(0));

    uint64_t state = WorldBiomeGenerator::SplitMix64(seed);
    for (int i = 255; i > 0; --i) {
        state = WorldBiomeGenerator::SplitMix64(state);
        const int j = static_cast<int>(state % static_cast<uint64_t>(i + 1));
        std::swap(base[static_cast<size_t>(i)], base[static_cast<size_t>(j)]);
    }

    for (size_t i = 0; i < 256u; ++i) {
        m_Perm[i] = base[i];
        m_Perm[i + 256u] = base[i];
    }
}

float NoiseGenerator::Simplex2D(float x, float z) const noexcept {
    static constexpr float kF2 = 0.366025403784f; // 0.5 * (sqrt(3) - 1)
    static constexpr float kG2 = 0.211324865405f; // (3 - sqrt(3)) / 6
    static constexpr float kGrad2[8][2] = {
        {  1.0f,  1.0f }, { -1.0f,  1.0f }, {  1.0f, -1.0f }, { -1.0f, -1.0f },
        {  1.0f,  0.0f }, { -1.0f,  0.0f }, {  0.0f,  1.0f }, {  0.0f, -1.0f }
    };

    const float s = (x + z) * kF2;
    const int32_t i = static_cast<int32_t>(std::floor(x + s));
    const int32_t j = static_cast<int32_t>(std::floor(z + s));
    const float t = static_cast<float>(i + j) * kG2;
    const float x0 = x - (static_cast<float>(i) - t);
    const float z0 = z - (static_cast<float>(j) - t);

    const int32_t i1 = (x0 > z0) ? 1 : 0;
    const int32_t j1 = (x0 > z0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + kG2;
    const float z1 = z0 - static_cast<float>(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2;
    const float z2 = z0 - 1.0f + 2.0f * kG2;

    const uint8_t ii = static_cast<uint8_t>(i);
    const uint8_t jj = static_cast<uint8_t>(j);

    auto cornerContrib = [&](float cx, float cz, uint8_t permIdx) noexcept -> float {
        float dist = 0.5f - cx * cx - cz * cz;
        if (dist <= 0.0f) {
            return 0.0f;
        }
        dist *= dist;
        const auto& g = kGrad2[m_Perm[permIdx] & 7u];
        return dist * dist * (g[0] * cx + g[1] * cz);
    };

    const uint8_t gi0 = m_Perm[static_cast<size_t>(ii) + m_Perm[jj]];
    const uint8_t gi1 = m_Perm[static_cast<size_t>(static_cast<uint8_t>(ii + i1)) + m_Perm[static_cast<uint8_t>(jj + j1)]];
    const uint8_t gi2 = m_Perm[static_cast<size_t>(static_cast<uint8_t>(ii + 1u)) + m_Perm[static_cast<uint8_t>(jj + 1u)]];

    const float n0 = cornerContrib(x0, z0, gi0);
    const float n1 = cornerContrib(x1, z1, gi1);
    const float n2 = cornerContrib(x2, z2, gi2);

    return std::clamp(70.0f * (n0 + n1 + n2), -1.0f, 1.0f);
}

float NoiseGenerator::FBm2D(float x, float z, int octaves, float lacunarity, float gain) const noexcept {
    const int safeOctaves = std::clamp(octaves, 1, 12);
    float sum = 0.0f;
    float amp = 1.0f;
    float ampSum = 0.0f;
    float freq = 1.0f;

    for (int o = 0; o < safeOctaves; ++o) {
        sum += amp * Simplex2D(x * freq, z * freq);
        ampSum += amp;
        freq *= lacunarity;
        amp *= gain;
    }

    return (ampSum > 1e-6f) ? std::clamp(sum / ampSum, -1.0f, 1.0f) : 0.0f;
}

glm::vec2 NoiseGenerator::DomainWarp2D(float x, float z, float amplitude) const noexcept {
    const float dx = FBm2D(x * 0.025f + 17.3f, z * 0.025f - 29.1f, 3) * amplitude;
    const float dz = FBm2D(x * 0.025f - 53.7f, z * 0.025f + 41.9f, 3) * amplitude;
    return { x + dx, z + dz };
}

glm::u8vec4 BiomeSample::ToSplatRGBA8() const noexcept {
    const float r = Weights[static_cast<size_t>(BiomeType::BlackwoodThickets)];
    const float chalk = Weights[static_cast<size_t>(BiomeType::ChalkHighlands)];
    const float g = Weights[static_cast<size_t>(BiomeType::AshBarrens)] + 0.5f * chalk;
    const float b = Weights[static_cast<size_t>(BiomeType::SanctuaryCobble)] + 0.5f * chalk;
    const float a = Weights[static_cast<size_t>(BiomeType::TarBogs)];
    return TerrainComponent::NormalizeWeights(r, g, b, a);
}

std::array<uint8_t, 8> BiomeSample::ToSplat8() const noexcept {
    // 1-to-1 canonical TER-03 mapping:
    // 0: Grass (BlackwoodThickets), 1: Ash (AshBarrens), 2: Tar (TarBogs),
    // 3: Chalk (ChalkHighlands), 4: Cobblestone (SanctuaryCobble)
    std::array<float, TerrainComponent::MaxLayers> w{};
    w[0] = Weights[static_cast<size_t>(BiomeType::BlackwoodThickets)];
    w[1] = Weights[static_cast<size_t>(BiomeType::AshBarrens)];
    w[2] = Weights[static_cast<size_t>(BiomeType::TarBogs)];
    w[3] = Weights[static_cast<size_t>(BiomeType::ChalkHighlands)];
    w[4] = Weights[static_cast<size_t>(BiomeType::SanctuaryCobble)];
    return TerrainComponent::NormalizeWeights8(w, 0u);
}

WorldBiomeGenerator::WorldBiomeGenerator(WorldGenConfig config) noexcept
    : m_Config(config),
      m_Noise(config.Seed) {
    ENGINE_CORE_INFO("WorldBiomeGenerator initialized (seed={}, originSafeRadius={:.1f}m, poiApron={:.1f}m, superCell={} chunks)",
                     m_Config.Seed, m_Config.OriginSafeRadius, m_Config.PoiApronRadius, m_Config.PoiSuperCellChunks);
}

POISpawnDesc WorldBiomeGenerator::ComputeSuperCellPOI(int32_t superX,
                                                      int32_t superZ,
                                                      float chunkSizeMeters) const noexcept {
    const int32_t superChunks = std::max(1, m_Config.PoiSuperCellChunks);
    const float superSize = static_cast<float>(superChunks) * std::max(1.0f, chunkSizeMeters);
    const float apron = std::max(1.0f, m_Config.PoiApronRadius);
    const float margin = std::min(superSize * 0.35f, apron + 2.5f);
    const float span = std::max(1.0f, superSize - 2.0f * margin);

    const uint64_t h0 = HashCombine(m_Config.Seed ^ 0x504F4931ULL, superX, superZ);
    const uint64_t h1 = SplitMix64(h0 ^ 0x93D76521ULL);
    const uint64_t h2 = SplitMix64(h1 ^ 0x2F418D17ULL);

    const float cellMinX = static_cast<float>(superX) * superSize;
    const float cellMinZ = static_cast<float>(superZ) * superSize;
    float worldX = cellMinX + margin + span * HashToUnitFloat(h0);
    float worldZ = cellMinZ + margin + span * HashToUnitFloat(h1);

    // Keep POIs strictly clear of the Origin Safe Clearing + their own apron
    const float minOriginPoiDist = m_Config.OriginSafeRadius + apron + 1.0f;
    const float distOrigin = std::sqrt(worldX * worldX + worldZ * worldZ);
    if (distOrigin < minOriginPoiDist) {
        const float cellCenterX = cellMinX + 0.5f * superSize;
        const float cellCenterZ = cellMinZ + 0.5f * superSize;
        glm::vec2 pushDir{ cellCenterX, cellCenterZ };
        const float dirLen = glm::length(pushDir);
        pushDir = (dirLen > 1e-4f) ? (pushDir / dirLen) : glm::vec2{ 0.7071f, 0.7071f };
        worldX = pushDir.x * (minOriginPoiDist + 2.0f);
        worldZ = pushDir.y * (minOriginPoiDist + 2.0f);
        worldX = std::clamp(worldX, cellMinX + margin, cellMinX + superSize - margin);
        worldZ = std::clamp(worldZ, cellMinZ + margin, cellMinZ + superSize - margin);
    }

    POISpawnDesc desc;
    desc.Kind = ((h2 & 1ULL) == 0ULL)
        ? ProceduralPropKind::POI_DeadBrazier
        : ProceduralPropKind::POI_UmbralShrine;
    desc.SuperCellX = superX;
    desc.SuperCellZ = superZ;
    desc.WorldPosition = { worldX, 0.0f, worldZ };
    desc.ApronRadius = apron;
    return desc;
}

BiomeSample WorldBiomeGenerator::EvaluateBiomeAtXZ(float worldX,
                                                     float worldZ,
                                                     float chunkSizeMeters) const noexcept {
    const glm::vec2 warped = m_Noise.DomainWarp2D(worldX, worldZ, 6.0f);
    const float moisture = m_Noise.FBm2D(warped.x * 0.018f, warped.y * 0.018f);
    const float elevation = m_Noise.FBm2D(warped.x * 0.015f + 113.0f, warped.y * 0.015f - 71.0f);

    // Whittaker 2-axis quadrant weights across the 4 procedural biomes
    struct BiomeProto {
        BiomeType type;
        float m;
        float e;
    };
    static constexpr BiomeProto kProtos[4] = {
        { BiomeType::BlackwoodThickets,  0.34f,  0.34f },
        { BiomeType::AshBarrens,        -0.34f, -0.34f },
        { BiomeType::TarBogs,            0.34f, -0.34f },
        { BiomeType::ChalkHighlands,    -0.34f,  0.34f }
    };

    std::array<float, 5> weights{};
    float procSum = 0.0f;
    static constexpr float kSharpness = 6.0f;
    for (const auto& p : kProtos) {
        const float dm = moisture - p.m;
        const float de = elevation - p.e;
        const float w = std::exp(-kSharpness * (dm * dm + de * de));
        weights[static_cast<size_t>(p.type)] = w;
        procSum += w;
    }
    if (procSum > 1e-6f) {
        const float inv = 1.0f / procSum;
        for (size_t i = 0; i < 4u; ++i) {
            weights[i] *= inv;
        }
    } else {
        weights[0] = 1.0f;
    }

    // Rule 1: Origin Safe Clearing (r < OriginSafeRadius blends SanctuaryCobble + AshBarrens)
    const float originRadius = std::max(0.5f, m_Config.OriginSafeRadius);
    const float distOrigin = std::sqrt(worldX * worldX + worldZ * worldZ);
    if (distOrigin < originRadius) {
        const float safeMask = 1.0f - SmoothStep(originRadius * 0.55f, originRadius, distOrigin);
        const float cobbleShare = 0.65f + 0.35f * (1.0f - SmoothStep(0.0f, originRadius * 0.65f, distOrigin));
        const float ashShare = 1.0f - cobbleShare;

        for (size_t i = 0; i < 5u; ++i) {
            weights[i] *= (1.0f - safeMask);
        }
        weights[static_cast<size_t>(BiomeType::SanctuaryCobble)] += safeMask * cobbleShare;
        weights[static_cast<size_t>(BiomeType::AshBarrens)] += safeMask * ashShare;
    }

    // Rule 3: Macro-Grid POI 3.5m Cobblestone apron blending across nearby super-cells
    const int32_t superChunks = std::max(1, m_Config.PoiSuperCellChunks);
    const float superSize = static_cast<float>(superChunks) * std::max(1.0f, chunkSizeMeters);
    const int32_t sx0 = static_cast<int32_t>(std::floor(worldX / superSize));
    const int32_t sz0 = static_cast<int32_t>(std::floor(worldZ / superSize));

    float maxApronBlend = 0.0f;
    for (int32_t dsz = -1; dsz <= 1; ++dsz) {
        for (int32_t dsx = -1; dsx <= 1; ++dsx) {
            const POISpawnDesc poi = ComputeSuperCellPOI(sx0 + dsx, sz0 + dsz, chunkSizeMeters);
            const float dx = worldX - poi.WorldPosition.x;
            const float dz = worldZ - poi.WorldPosition.z;
            const float distSq = dx * dx + dz * dz;
            if (distSq < poi.ApronRadius * poi.ApronRadius) {
                const float dist = std::sqrt(distSq);
                const float blend = 1.0f - SmoothStep(poi.ApronRadius * 0.45f, poi.ApronRadius, dist);
                maxApronBlend = std::max(maxApronBlend, blend);
            }
        }
    }

    if (maxApronBlend > 0.0f) {
        for (size_t i = 0; i < 5u; ++i) {
            weights[i] *= (1.0f - maxApronBlend);
        }
        weights[static_cast<size_t>(BiomeType::SanctuaryCobble)] += maxApronBlend;
    }

    // Normalize & resolve dominant biome
    float total = 0.0f;
    for (float w : weights) {
        total += w;
    }
    if (total > 1e-6f) {
        const float inv = 1.0f / total;
        for (float& w : weights) {
            w *= inv;
        }
    } else {
        weights = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }

    size_t bestIdx = 0;
    for (size_t i = 1; i < weights.size(); ++i) {
        if (weights[i] > weights[bestIdx]) {
            bestIdx = i;
        }
    }

    BiomeSample sample;
    sample.Weights = weights;
    sample.DominantBiome = static_cast<BiomeType>(bestIdx);
    sample.Moisture = moisture;
    sample.Elevation = elevation;
    return sample;
}

void WorldBiomeGenerator::GenerateChunkData(int32_t cx,
                                            int32_t cz,
                                            float chunkSizeMeters,
                                            uint32_t splatResolution,
                                            std::vector<uint8_t>& outSplatRGBA,
                                            std::vector<uint8_t>& outSplatRGBA1,
                                            std::vector<BiomeType>& outBiomeMap,
                                            std::vector<PropSpawnDesc>& outProps,
                                            std::vector<POISpawnDesc>& outPOIs) const {
    const float safeSize = std::max(1.0f, chunkSizeMeters);
    const uint32_t res = std::clamp(splatResolution, 5u, 513u);
    const uint32_t cellSpan = res - 1u;
    const float texelStep = safeSize / static_cast<float>(cellSpan);

    outSplatRGBA.resize(static_cast<size_t>(res) * res * 4u);
    outSplatRGBA1.resize(static_cast<size_t>(res) * res * 4u);
    outBiomeMap.resize(static_cast<size_t>(res) * res);
    outProps.clear();
    outPOIs.clear();

    // 1. Evaluate node-aligned biome field + seamless 8-layer RGBA8 splatweights
    for (uint32_t pz = 0; pz < res; ++pz) {
        const float worldZ = static_cast<float>(static_cast<int64_t>(cz) * cellSpan + pz) * texelStep;
        for (uint32_t px = 0; px < res; ++px) {
            const float worldX = static_cast<float>(static_cast<int64_t>(cx) * cellSpan + px) * texelStep;
            const BiomeSample sample = EvaluateBiomeAtXZ(worldX, worldZ, safeSize);
            const size_t texelIdx = static_cast<size_t>(pz) * res + px;
            outBiomeMap[texelIdx] = sample.DominantBiome;

            const std::array<uint8_t, TerrainComponent::MaxLayers> w8 = sample.ToSplat8();
            const size_t byteIdx = texelIdx * 4u;
            for (uint32_t c = 0; c < 4u; ++c) {
                outSplatRGBA[byteIdx + c] = w8[c];
                outSplatRGBA1[byteIdx + c] = w8[c + 4u];
            }
        }
    }

    const float chunkMinX = static_cast<float>(cx) * safeSize;
    const float chunkMaxX = chunkMinX + safeSize;
    const float chunkMinZ = static_cast<float>(cz) * safeSize;
    const float chunkMaxZ = chunkMinZ + safeSize;

    // 2. Collect POIs owning this chunk & neighbor aprons overlapping chunk bounds
    const int32_t superChunks = std::max(1, m_Config.PoiSuperCellChunks);
    const int32_t sxCenter = FloorDiv(cx, superChunks);
    const int32_t szCenter = FloorDiv(cz, superChunks);

    std::vector<POISpawnDesc> exclusionPOIs;
    for (int32_t dsz = -1; dsz <= 1; ++dsz) {
        for (int32_t dsx = -1; dsx <= 1; ++dsx) {
            const POISpawnDesc poi = ComputeSuperCellPOI(sxCenter + dsx, szCenter + dsz, safeSize);
            if (poi.WorldPosition.x + poi.ApronRadius >= chunkMinX - 4.0f &&
                poi.WorldPosition.x - poi.ApronRadius <= chunkMaxX + 4.0f &&
                poi.WorldPosition.z + poi.ApronRadius >= chunkMinZ - 4.0f &&
                poi.WorldPosition.z - poi.ApronRadius <= chunkMaxZ + 4.0f) {
                exclusionPOIs.push_back(poi);
            }
            if (poi.WorldPosition.x >= chunkMinX && poi.WorldPosition.x < chunkMaxX &&
                poi.WorldPosition.z >= chunkMinZ && poi.WorldPosition.z < chunkMaxZ) {
                outPOIs.push_back(poi);
            }
        }
    }

    // 3. Order-independent priority Poisson scatter across 3 size tiers
    static constexpr PropTierSpec kTiers[3] = {
        { 0x5449455230ULL, 3.2f, 0.00f, 0.88f, 0.88f, 1.18f, SelectTier0BlockerProp },
        { 0x5449455231ULL, 2.0f, 1.35f, 0.78f, 0.85f, 1.15f, SelectTier1MediumProp  },
        { 0x5449455232ULL, 0.9f, 0.75f, 0.45f, 0.85f, 1.20f, SelectTier2GroundProp  }
    };

    std::vector<glm::vec2> nearbyTier0Trunks;

    auto scatterTier = [&](const PropTierSpec& spec, bool recordTrunks) {
        const float cellSpacing = spec.MinDist * 0.62f;
        const float minDistSq = spec.MinDist * spec.MinDist;
        const float trunkClearSq = spec.TrunkClearance * spec.TrunkClearance;
        const float borderMargin = recordTrunks ? 1.5f : 0.0f;

        const int32_t minCellX = static_cast<int32_t>(std::floor((chunkMinX - borderMargin) / cellSpacing)) - 1;
        const int32_t maxCellX = static_cast<int32_t>(std::floor((chunkMaxX + borderMargin) / cellSpacing)) + 1;
        const int32_t minCellZ = static_cast<int32_t>(std::floor((chunkMinZ - borderMargin) / cellSpacing)) - 1;
        const int32_t maxCellZ = static_cast<int32_t>(std::floor((chunkMaxZ + borderMargin) / cellSpacing)) + 1;

        for (int32_t czCell = minCellZ; czCell <= maxCellZ; ++czCell) {
            for (int32_t cxCell = minCellX; cxCell <= maxCellX; ++cxCell) {
                const GridCandidate cand = EvaluateCellCandidate(
                    m_Config.Seed, spec.TierSalt, cxCell, czCell, cellSpacing, spec.OccupancyChance);
                if (!cand.PassesOccupancy) {
                    continue;
                }

                const bool insideExpanded = (cand.PosXZ.x >= chunkMinX - borderMargin &&
                                             cand.PosXZ.x <  chunkMaxX + borderMargin &&
                                             cand.PosXZ.y >= chunkMinZ - borderMargin &&
                                             cand.PosXZ.y <  chunkMaxZ + borderMargin);
                if (!insideExpanded) {
                    continue;
                }

                if (IsInsideExclusionZone(cand.PosXZ, m_Config.OriginSafeRadius, exclusionPOIs)) {
                    continue;
                }

                const BiomeSample bio = EvaluateBiomeAtXZ(cand.PosXZ.x, cand.PosXZ.y, safeSize);
                ProceduralPropKind propKind{};
                bool propBlocking = false;
                if (!spec.SelectProp(bio.DominantBiome, propKind, propBlocking)) {
                    continue;
                }

                // Check trunk clearance for medium/small foliage against Tier 0 trunks
                if (spec.TrunkClearance > 0.0f) {
                    bool blockedByTrunk = false;
                    for (const auto& trunkXZ : nearbyTier0Trunks) {
                        const glm::vec2 dt = cand.PosXZ - trunkXZ;
                        if (glm::dot(dt, dt) < trunkClearSq) {
                            blockedByTrunk = true;
                            break;
                        }
                    }
                    if (blockedByTrunk) {
                        continue;
                    }
                }

                // Enforce global minDist across ALL chunk boundaries against higher-priority eligible neighbors
                bool beatenByNeighbor = false;
                for (int32_t dz = -2; dz <= 2 && !beatenByNeighbor; ++dz) {
                    for (int32_t dx = -2; dx <= 2; ++dx) {
                        if (dx == 0 && dz == 0) {
                            continue;
                        }
                        const GridCandidate nbr = EvaluateCellCandidate(
                            m_Config.Seed, spec.TierSalt, cxCell + dx, czCell + dz, cellSpacing, spec.OccupancyChance);
                        if (!nbr.PassesOccupancy) {
                            continue;
                        }
                        const glm::vec2 diff = nbr.PosXZ - cand.PosXZ;
                        if (glm::dot(diff, diff) >= minDistSq) {
                            continue;
                        }
                        if (!HasHigherPriority(nbr, cand)) {
                            continue;
                        }
                        if (IsInsideExclusionZone(nbr.PosXZ, m_Config.OriginSafeRadius, exclusionPOIs)) {
                            continue;
                        }
                        const BiomeSample nbrBio = EvaluateBiomeAtXZ(nbr.PosXZ.x, nbr.PosXZ.y, safeSize);
                        ProceduralPropKind nbrKind{};
                        bool nbrBlocking = false;
                        if (spec.SelectProp(nbrBio.DominantBiome, nbrKind, nbrBlocking)) {
                            beatenByNeighbor = true;
                            break;
                        }
                    }
                }
                if (beatenByNeighbor) {
                    continue;
                }

                if (recordTrunks) {
                    nearbyTier0Trunks.push_back(cand.PosXZ);
                }

                if (cand.PosXZ.x >= chunkMinX && cand.PosXZ.x < chunkMaxX &&
                    cand.PosXZ.y >= chunkMinZ && cand.PosXZ.y < chunkMaxZ) {
                    const uint64_t styleHash = SplitMix64(cand.PriorityHash ^ 0x99887766ULL);
                    const float tScale = HashToUnitFloat(styleHash);
                    const float tPhase = HashToUnitFloat(SplitMix64(styleHash));

                    PropSpawnDesc desc;
                    desc.Kind = propKind;
                    desc.SourceBiome = bio.DominantBiome;
                    desc.WorldPosition = { cand.PosXZ.x, 0.0f, cand.PosXZ.y };
                    desc.ScaleMultiplier = spec.ScaleJitterMin + (spec.ScaleJitterMax - spec.ScaleJitterMin) * tScale;
                    desc.WindPhaseOffset = tPhase * 6.283185f;
                    desc.Blocking = propBlocking;
                    outProps.push_back(desc);
                }
            }
        }
    };

    scatterTier(kTiers[0], true);
    scatterTier(kTiers[1], false);
    scatterTier(kTiers[2], false);

    ENGINE_CORE_DEBUG("WorldBiomeGenerator::GenerateChunkData ({}, {}): res={}x{}, props={}, POIs={}, exclusionPOIs={}",
                      cx, cz, res, res, outProps.size(), outPOIs.size(), exclusionPOIs.size());
}

} // namespace Engine
