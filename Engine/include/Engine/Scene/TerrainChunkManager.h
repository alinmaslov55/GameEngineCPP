#ifndef SCENE_TERRAINCHUNKMANAGER_H
#define SCENE_TERRAINCHUNKMANAGER_H

#include "Engine/Core/JobHandle.h"
#include "Engine/Scene/Component.h"
#include "Engine/Terrain/TerrainChunk.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/WorldBiomeGenerator.h"
#include "Engine/Terrain/SurfaceSample.h"
#include "Engine/Terrain/TerrainFireGrid.h"

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

/// @brief Manages infinite XZ terrain chunk streaming, shared-border stitching,
/// background JobSystem splat generation, frame-paced GPU uploads, and LRU RAM eviction.
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
    void Update(float deltaTime) override;

    // Streaming lifecycle
    void UpdateStreaming(const glm::vec3& viewerWorldPos);
    void FlushPendingGenerations();
    uint32_t ProcessGpuUploadRing();
    void DispatchChunkGeneration(ChunkCoord coord);
    void DrainReadyPayloads();

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

    void PaintMaterialWorld(const glm::vec2& centerXZ,
                            uint32_t layerIdx,
                            float radius,
                            float strength = 1.0f) {
        PaintWorldBrush(centerXZ.x, centerXZ.y, layerIdx, radius, 0.65f, strength);
    }

    // CPU Surface Sampler & Tar/Fire Simulation
    [[nodiscard]] SurfaceSample SampleSurfaceAtXZ(float worldX, float worldZ) const noexcept;
    void IgniteAtXZ(const glm::vec2& centerXZ, float radius);
    void ExtinguishAtXZ(const glm::vec2& centerXZ, float radius);
    void ExtinguishInBoundsXZ(const glm::vec2& minXZ, const glm::vec2& maxXZ);
    [[nodiscard]] bool IsBurningAtXZ(float worldX, float worldZ) const noexcept;
    void UpdateFire(float dt);
    [[nodiscard]] const TerrainFireGrid& GetFireGrid() const noexcept { return m_FireGrid; }
    [[nodiscard]] TerrainFireGrid& GetFireGrid() noexcept { return m_FireGrid; }

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

    void SynchronizeSharedBorders(TerrainChunk& chunk) noexcept;
    void EnqueueGpuUpload(ChunkCoord coord, TerrainChunk& chunk);
    void DemoteAndEvictChunks(ChunkCoord centerChunk);
    void SpawnChunkEntities(TerrainChunk& chunk);
    void ReleaseChunkEntities(TerrainChunk& chunk);
    const std::shared_ptr<Prefab>& GetOrCreatePropPrefab(ProceduralPropKind kind);
    void ShutdownAsyncWorkers() noexcept;

    void InitFireGrid();
    uint64_t SpawnFirewallEntity(const glm::vec2& centerXZ);
    void ReleaseFirewallEntity(uint64_t uuid);

private:
    std::unordered_map<ChunkCoord, TerrainChunk, ChunkCoordHash> m_Chunks;
    std::deque<ChunkCoord> m_GpuUploadRing;
    std::vector<JobHandle> m_InFlightJobs;
    std::shared_ptr<AsyncChunkChannel> m_AsyncChannel;
    TerrainFireGrid m_FireGrid;

    WorldGenConfig m_WorldGenConfig{};
    std::shared_ptr<const WorldBiomeGenerator> m_BiomeGenerator;
    std::array<std::shared_ptr<Prefab>, 9> m_PropPrefabs{};
    std::shared_ptr<Prefab> m_FirewallPrefab;
    std::vector<std::pair<ChunkCoord, TerrainChunk*>> m_EvictCandidatesScratch;

    std::array<TerrainLayer, TerrainComponent::MaxLayers> m_Layers{};
    float m_BlendSharpness = 0.68f;
    float m_EdgeDarkening = 0.16f;

    uint64_t m_FrameCounter = 0;
    uint32_t m_LastFrameUploadCount = 0;
    ChunkCoord m_LastViewerChunk{ 0, 0 };
};

} // namespace Engine

#endif // SCENE_TERRAINCHUNKMANAGER_H
