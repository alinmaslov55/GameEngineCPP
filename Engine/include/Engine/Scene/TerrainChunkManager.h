#ifndef SCENE_TERRAINCHUNKMANAGER_H
#define SCENE_TERRAINCHUNKMANAGER_H

#include "Engine/Core/JobSystem.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/WorldBiomeGenerator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Engine {

class Prefab;

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

/// @brief Manages infinite XZ terrain chunk streaming, shared-border stitching,
/// background JobSystem splat generation, frame-paced GPU uploads, and LRU RAM eviction (TER-01).
class TerrainChunkManager final : public Component {
public:
    static constexpr uint32_t DefaultSplatResolution = 65;
    static constexpr uint32_t DefaultMaxUploadsPerFrame = 2;

    float ChunkSizeMeters = 16.0f;    // 16m x 16m world footprint per chunk
    uint32_t SplatResolution = 65;    // 64 cells + 1 shared border texel (C0 seamless!)
    int32_t ActiveRadiusChunks = 3;   // 7x7 = 49 active chunks (~112m x 112m loaded window)
    int32_t CacheRetentionRadius = 6; // Keeps modified/recent splats cached in RAM before LRU eviction
    uint32_t MaxGpuUploadsPerFrame = DefaultMaxUploadsPerFrame;
    bool AutoStreamWithCamera = true;

    TerrainChunkManager();
    TerrainChunkManager(float chunkSizeMeters,
                        uint32_t splatResolution = DefaultSplatResolution,
                        int32_t activeRadiusChunks = 3,
                        int32_t cacheRetentionRadius = 6);
    ~TerrainChunkManager() override;

    TerrainChunkManager(const TerrainChunkManager&) = delete;
    TerrainChunkManager& operator=(const TerrainChunkManager&) = delete;
    TerrainChunkManager(TerrainChunkManager&&) = delete;
    TerrainChunkManager& operator=(TerrainChunkManager&&) = delete;

    void OnDestroy() override;

    // Streaming lifecycle
    void UpdateStreaming(const glm::vec3& viewerWorldPos);
    void FlushPendingGenerations();
    uint32_t ProcessGpuUploadRing();

    [[nodiscard]] TerrainChunk* GetChunk(ChunkCoord coord) noexcept;
    [[nodiscard]] const TerrainChunk* GetChunk(ChunkCoord coord) const noexcept;
    [[nodiscard]] ChunkCoord WorldToChunk(float worldX, float worldZ) const noexcept;
    [[nodiscard]] std::unordered_map<ChunkCoord, TerrainChunk, ChunkCoordHash>& GetChunks() noexcept {
        return m_Chunks;
    }
    [[nodiscard]] const std::unordered_map<ChunkCoord, TerrainChunk, ChunkCoordHash>& GetChunks() const noexcept {
        return m_Chunks;
    }

    // Cross-chunk seamless splat painting (node-aligned across shared chunk borders)
    void PaintWorldBrush(float worldX,
                         float worldZ,
                         uint32_t layerIndex,
                         float radiusWorld,
                         float falloff = 0.65f,
                         float strength = 1.0f);
    void PaintWorldSegment(const glm::vec2& startXZ,
                           const glm::vec2& endXZ,
                           uint32_t layerIndex,
                           float radiusWorld,
                           float falloff = 0.65f,
                           float strength = 1.0f);

    // Inject/create a modified chunk directly (used by deserialization & tests)
    TerrainChunk& EnsureEditableChunk(ChunkCoord coord);

    // Procedural Biome Generation & Prop/POI Scattering (TER-02)
    void ConfigureProceduralWorld(const WorldGenConfig& config);
    void RegenerateWorld();
    [[nodiscard]] const WorldGenConfig& GetWorldGenConfig() const noexcept { return m_WorldGenConfig; }
    [[nodiscard]] WorldGenConfig& GetWorldGenConfigMutable() noexcept { return m_WorldGenConfig; }
    [[nodiscard]] const WorldBiomeGenerator* GetBiomeGenerator() const noexcept { return m_BiomeGenerator.get(); }
    [[nodiscard]] BiomeSample SampleBiomeAtXZ(float worldX, float worldZ) const noexcept;
    void RegisterPropPrefab(ProceduralPropKind kind, const std::shared_ptr<Prefab>& prefab);

    // Material & blending appearance shared across all streamed chunks
    float GetBlendSharpness() const noexcept { return m_BlendSharpness; }
    void SetBlendSharpness(float sharpness) noexcept;
    float GetEdgeDarkening() const noexcept { return m_EdgeDarkening; }
    void SetEdgeDarkening(float darkening) noexcept;
    void SetLayerTexture(uint32_t layerIndex, const std::shared_ptr<Texture>& texture);
    void SetLayerHeightMapPath(uint32_t layerIndex, const std::string& path);
    void SetLayerNormalMapPath(uint32_t layerIndex, const std::string& path);
    void SetLayerTilingScale(uint32_t layerIndex, float tilingScale) noexcept;
    void SetLayerGlossiness(uint32_t layerIndex, float glossiness) noexcept;
    const TerrainLayer& GetLayer(uint32_t layerIndex) const noexcept;
    TerrainLayer& GetLayer(uint32_t layerIndex) noexcept;
    const std::array<TerrainLayer, TerrainComponent::MaxLayers>& GetLayers() const noexcept { return m_Layers; }

    // Streaming telemetry & diagnostics
    [[nodiscard]] size_t GetActiveChunkCount() const noexcept;
    [[nodiscard]] size_t GetCachedChunkCount() const noexcept;
    [[nodiscard]] size_t GetTotalChunkCount() const noexcept { return m_Chunks.size(); }
    [[nodiscard]] size_t GetPendingUploadCount() const noexcept { return m_GpuUploadRing.size(); }
    [[nodiscard]] uint32_t GetLastFrameUploadCount() const noexcept { return m_LastFrameUploadCount; }
    [[nodiscard]] uint64_t GetFrameCounter() const noexcept { return m_FrameCounter; }
    [[nodiscard]] ChunkCoord GetLastViewerChunk() const noexcept { return m_LastViewerChunk; }

private:
    struct GeneratedChunkPayload {
        ChunkCoord Coord{};
        uint32_t Resolution = DefaultSplatResolution;
        float ChunkSize = 16.0f;
        std::vector<uint8_t> SplatData;
        std::vector<uint8_t> SplatData1;
        std::vector<BiomeType> BiomeMap;
        std::vector<PropSpawnDesc> Props;
        std::vector<POISpawnDesc> POIs;
    };

    struct AsyncChunkChannel {
        std::atomic<bool> ShuttingDown{ false };
        std::mutex QueueMutex;
        std::vector<GeneratedChunkPayload> ReadyPayloads;
    };

    void DispatchChunkGeneration(ChunkCoord coord);
    void DrainReadyPayloads();
    void SynchronizeSharedBorders(TerrainChunk& chunk) noexcept;
    void EnqueueGpuUpload(ChunkCoord coord, TerrainChunk& chunk);
    void DemoteAndEvictChunks(ChunkCoord centerChunk);
    void SpawnChunkEntities(TerrainChunk& chunk);
    void ReleaseChunkEntities(TerrainChunk& chunk);
    const std::shared_ptr<Prefab>& GetOrCreatePropPrefab(ProceduralPropKind kind);
    void ShutdownAsyncWorkers() noexcept;

private:
    std::unordered_map<ChunkCoord, TerrainChunk, ChunkCoordHash> m_Chunks;
    std::deque<ChunkCoord> m_GpuUploadRing;
    std::vector<JobHandle> m_InFlightJobs;
    std::shared_ptr<AsyncChunkChannel> m_AsyncChannel;

    WorldGenConfig m_WorldGenConfig{};
    std::shared_ptr<const WorldBiomeGenerator> m_BiomeGenerator;
    std::array<std::shared_ptr<Prefab>, 9> m_PropPrefabs{};

    std::array<TerrainLayer, TerrainComponent::MaxLayers> m_Layers{};
    float m_BlendSharpness = 0.68f;
    float m_EdgeDarkening = 0.16f;

    uint64_t m_FrameCounter = 0;
    uint32_t m_LastFrameUploadCount = 0;
    ChunkCoord m_LastViewerChunk{ 0, 0 };
};

} // namespace Engine

#endif // SCENE_TERRAINCHUNKMANAGER_H
