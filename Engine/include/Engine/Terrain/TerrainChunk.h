#ifndef TERRAIN_TERRAINCHUNK_H
#define TERRAIN_TERRAINCHUNK_H

#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/WorldGenTypes.h"
#include "Engine/Renderer/Texture.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace Engine {

struct ChunkCoord {
    int32_t cx = 0;
    int32_t cz = 0;

    bool operator==(const ChunkCoord&) const noexcept = default;
};

struct ChunkCoordHash {
    [[nodiscard]] size_t operator()(const ChunkCoord& coord) const noexcept {
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(coord.cx)) << 32) |
                                 static_cast<uint64_t>(static_cast<uint32_t>(coord.cz));
        uint64_t z = packed + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return static_cast<size_t>(z);
    }
};

enum class ChunkLoadState : uint8_t {
    Unloaded = 0,
    Generating,        // Background JobSystem task running
    ReadyForGpuUpload, // CPU SplatData committed in RAM; awaiting GPU upload ring
    Resident,          // Uploaded (or headless-processed) and active
    CachedInRam        // Evicted from VRAM beyond ActiveRadiusChunks; splat data kept in RAM
};

struct TerrainChunk {
    ChunkCoord Coord{};
    ChunkLoadState State = ChunkLoadState::Unloaded;
    float ChunkSizeMeters = 16.0f;
    uint32_t SplatResolution = 65;

    std::vector<uint8_t> SplatData;
    std::vector<uint8_t> SplatData1;
    std::vector<BiomeType> BiomeMap;
    std::vector<PropSpawnDesc> Props;
    std::vector<POISpawnDesc> POIs;
    std::vector<uint64_t> SpawnedEntityUUIDs;
    std::shared_ptr<Texture> GpuSplatTexture;
    std::shared_ptr<Texture> GpuSplatTexture1;
    std::vector<uint8_t> UploadScratch;

    bool GpuDirty = false;
    bool InUploadQueue = false;
    bool UserModified = false;
    bool EntitiesSpawned = false;
    uint32_t DirtyMinX = 0;
    uint32_t DirtyMinY = 0;
    uint32_t DirtyMaxX = 0;
    uint32_t DirtyMaxY = 0;
    uint64_t LastAccessFrame = 0;

    [[nodiscard]] glm::vec3 GetWorldOrigin(float groundY = 0.0f) const noexcept;
    [[nodiscard]] glm::u8vec4 GetSplatTexel(uint32_t px, uint32_t pz) const noexcept;
    [[nodiscard]] glm::u8vec4 GetSplatTexel1(uint32_t px, uint32_t pz) const noexcept;
    [[nodiscard]] std::array<uint8_t, TerrainComponent::MaxLayers> GetSplatTexel8(uint32_t px, uint32_t pz) const noexcept;
    void SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights) noexcept;
    void SetSplatTexel8(uint32_t px, uint32_t pz, const std::array<uint8_t, TerrainComponent::MaxLayers>& weights) noexcept;
    void MarkDirtyRect(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, bool markModified) noexcept;
    const std::shared_ptr<Texture>& SyncGpuSplatTexture();
    std::pair<std::shared_ptr<Texture>, std::shared_ptr<Texture>> SyncGpuSplatTextures();

    [[nodiscard]] const std::vector<uint8_t>& GetSplatData() const noexcept { return SplatData; }
    [[nodiscard]] const std::vector<uint8_t>& GetSplatData1() const noexcept { return SplatData1; }
    [[nodiscard]] bool IsResident() const noexcept {
        return State == ChunkLoadState::ReadyForGpuUpload || State == ChunkLoadState::Resident;
    }
    [[nodiscard]] bool HasGpuTexture() const noexcept {
        return GpuSplatTexture != nullptr && GpuSplatTexture->GetRendererID() != 0;
    }
};

} // namespace Engine

#endif // TERRAIN_TERRAINCHUNK_H
