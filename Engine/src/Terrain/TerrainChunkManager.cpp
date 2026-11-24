#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Terrain/TerrainHelpers.h"
#include "Engine/Terrain/ProceduralPropFactory.h"
#include "Engine/Particles/DecalManager.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Engine {

TerrainChunkManager::TerrainChunkManager()
    : m_AsyncChannel(std::make_shared<AsyncChunkChannel>()),
      m_BiomeGenerator(std::make_shared<const WorldBiomeGenerator>(m_WorldGenConfig)) {
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        m_Layers[i].Glossiness = TerrainComponent::DefaultLayerGlossiness(i);
    }
    InitFireGrid();
    ENGINE_CORE_INFO("TerrainChunkManager initialized (chunkSize={:.1f}m, splatRes={}x{}, activeRadius={}, retentionRadius={})",
                     ChunkSizeMeters, SplatResolution, SplatResolution, ActiveRadiusChunks, CacheRetentionRadius);
}

TerrainChunkManager::TerrainChunkManager(float chunkSizeMeters,
                                         uint32_t splatResolution,
                                         int32_t activeRadiusChunks,
                                         int32_t cacheRetentionRadius)
    : ChunkSizeMeters(std::max(1.0f, chunkSizeMeters)),
      SplatResolution(std::clamp(splatResolution, 5u, 513u)),
      ActiveRadiusChunks(std::max(0, activeRadiusChunks)),
      CacheRetentionRadius(std::max(ActiveRadiusChunks, cacheRetentionRadius)),
      m_AsyncChannel(std::make_shared<AsyncChunkChannel>()),
      m_BiomeGenerator(std::make_shared<const WorldBiomeGenerator>(m_WorldGenConfig)) {
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        m_Layers[i].Glossiness = TerrainComponent::DefaultLayerGlossiness(i);
    }
    InitFireGrid();
    ENGINE_CORE_INFO("TerrainChunkManager initialized (chunkSize={:.1f}m, splatRes={}x{}, activeRadius={}, retentionRadius={})",
                     ChunkSizeMeters, SplatResolution, SplatResolution, ActiveRadiusChunks, CacheRetentionRadius);
}

TerrainChunkManager::~TerrainChunkManager() {
    ENGINE_CORE_DEBUG("TerrainChunkManager shutting down ({} cached/resident chunks)", m_Chunks.size());
    ShutdownAsyncWorkers();
    m_FireGrid.Clear();
    m_FireGrid.ConfigureCallbacks(nullptr, nullptr, nullptr, nullptr);
}

void TerrainChunkManager::OnDestroy() {
    ENGINE_CORE_INFO("TerrainChunkManager::OnDestroy: Releasing {} chunks and {} active fire cells",
                     m_Chunks.size(), m_FireGrid.GetActiveFireCount());
    ShutdownAsyncWorkers();
    m_FireGrid.Clear();
    m_FireGrid.ConfigureCallbacks(nullptr, nullptr, nullptr, nullptr);
    for (auto& [coord, chunk] : m_Chunks) {
        (void)coord;
        ReleaseChunkEntities(chunk);
    }
}

void TerrainChunkManager::Update(float deltaTime) {
    UpdateFire(deltaTime);
}

void TerrainChunkManager::ShutdownAsyncWorkers() noexcept {
    if (m_AsyncChannel) {
        m_AsyncChannel->ShuttingDown.store(true, std::memory_order_release);
    }
    for (const auto& handle : m_InFlightJobs) {
        if (handle.IsValid()) {
            handle.Wait();
        }
    }
    m_InFlightJobs.clear();
}

ChunkCoord TerrainChunkManager::WorldToChunk(float worldX, float worldZ) const noexcept {
    const float safeChunkSize = std::max(1.0f, ChunkSizeMeters);
    return {
        static_cast<int32_t>(std::floor(worldX / safeChunkSize)),
        static_cast<int32_t>(std::floor(worldZ / safeChunkSize))
    };
}

TerrainChunk* TerrainChunkManager::GetChunk(ChunkCoord coord) noexcept {
    auto it = m_Chunks.find(coord);
    return (it != m_Chunks.end()) ? &it->second : nullptr;
}

const TerrainChunk* TerrainChunkManager::GetChunk(ChunkCoord coord) const noexcept {
    auto it = m_Chunks.find(coord);
    return (it != m_Chunks.end()) ? &it->second : nullptr;
}

void TerrainChunkManager::DispatchChunkGeneration(ChunkCoord coord) {
    const uint32_t res = std::clamp(SplatResolution, 5u, 513u);
    const float chunkSize = std::max(1.0f, ChunkSizeMeters);

    ENGINE_CORE_DEBUG("TerrainChunkManager: Dispatching generation for chunk ({}, {}) [res={}x{}, size={:.1f}m, procedural={}]",
                      coord.cx, coord.cz, res, res, chunkSize, m_WorldGenConfig.Enabled && m_BiomeGenerator);

    TerrainChunk& placeholder = m_Chunks[coord];
    placeholder.Coord = coord;
    placeholder.State = ChunkLoadState::Generating;
    placeholder.ChunkSizeMeters = chunkSize;
    placeholder.SplatResolution = res;
    placeholder.LastAccessFrame = m_FrameCounter;

    auto channel = m_AsyncChannel;
    auto biomeGen = (m_WorldGenConfig.Enabled && m_BiomeGenerator) ? m_BiomeGenerator : nullptr;
    JobHandle handle = JobSystem::Dispatch([channel, biomeGen, coord, res, chunkSize]() {
        if (!channel || channel->ShuttingDown.load(std::memory_order_acquire)) {
            return;
        }

        GeneratedChunkPayload payload;
        payload.Coord = coord;
        payload.Resolution = res;
        payload.ChunkSize = chunkSize;

        if (biomeGen) {
            biomeGen->GenerateChunkData(
                coord.cx,
                coord.cz,
                chunkSize,
                res,
                payload.SplatData,
                payload.SplatData1,
                payload.BiomeMap,
                payload.Props,
                payload.POIs
            );
        } else {
            const size_t texelCount = static_cast<size_t>(res) * static_cast<size_t>(res);
            payload.SplatData.assign(texelCount * 4u, 0u);
            payload.SplatData1.assign(texelCount * 4u, 0u);
            for (size_t i = 0; i < payload.SplatData.size(); i += 4u) {
                payload.SplatData[i + 0] = 255u;
            }
        }

        if (channel->ShuttingDown.load(std::memory_order_acquire)) {
            return;
        }

        std::lock_guard<std::mutex> lock(channel->QueueMutex);
        if (!channel->ShuttingDown.load(std::memory_order_relaxed)) {
            channel->ReadyPayloads.push_back(std::move(payload));
        }
    });

    if (handle.IsValid() && !handle.IsComplete()) {
        m_InFlightJobs.push_back(std::move(handle));
    }
}

void TerrainChunkManager::DrainReadyPayloads() {
    if (!m_AsyncChannel) {
        return;
    }

    std::erase_if(m_InFlightJobs, [](const JobHandle& h) {
        return !h.IsValid() || h.IsComplete();
    });

    std::vector<GeneratedChunkPayload> payloads;
    {
        std::lock_guard<std::mutex> lock(m_AsyncChannel->QueueMutex);
        if (m_AsyncChannel->ReadyPayloads.empty()) {
            return;
        }
        payloads.swap(m_AsyncChannel->ReadyPayloads);
    }

    const int32_t activeRadius = std::max(0, ActiveRadiusChunks);
    const int32_t retentionRadius = std::max(activeRadius, CacheRetentionRadius);
    ENGINE_CORE_DEBUG("TerrainChunkManager::DrainReadyPayloads: Committing {} generated chunk payload(s)", payloads.size());

    for (auto& payload : payloads) {
        auto it = m_Chunks.find(payload.Coord);
        if (it == m_Chunks.end()) {
            ENGINE_CORE_TRACE("TerrainChunkManager: Dropping stale payload for evicted chunk ({}, {})",
                              payload.Coord.cx, payload.Coord.cz);
            continue;
        }
        TerrainChunk& chunk = it->second;
        if (chunk.State != ChunkLoadState::Generating || chunk.UserModified) {
            ENGINE_CORE_DEBUG("TerrainChunkManager: Skipping async payload for chunk ({}, {}) [state={}, userModified={}]",
                              payload.Coord.cx, payload.Coord.cz, static_cast<int>(chunk.State), chunk.UserModified);
            continue;
        }
        chunk.ChunkSizeMeters = payload.ChunkSize;
        chunk.SplatResolution = payload.Resolution;
        chunk.SplatData = std::move(payload.SplatData);
        chunk.SplatData1 = std::move(payload.SplatData1);
        chunk.BiomeMap = std::move(payload.BiomeMap);
        chunk.Props = std::move(payload.Props);
        chunk.POIs = std::move(payload.POIs);

        const int32_t dist = std::max(
            std::abs(payload.Coord.cx - m_LastViewerChunk.cx),
            std::abs(payload.Coord.cz - m_LastViewerChunk.cz)
        );
        if (dist > retentionRadius) {
            ENGINE_CORE_DEBUG("TerrainChunkManager: Generated chunk ({}, {}) landed outside retention radius ({} > {}), discarding",
                              payload.Coord.cx, payload.Coord.cz, dist, retentionRadius);
            ReleaseChunkEntities(chunk);
            m_Chunks.erase(it);
            continue;
        }
        chunk.GpuDirty = false;
        chunk.UserModified = false;

        SynchronizeSharedBorders(chunk);

        chunk.MarkDirtyRect(0u, 0u, chunk.SplatResolution - 1u, chunk.SplatResolution - 1u, false);

        if (dist <= activeRadius) {
            chunk.State = ChunkLoadState::ReadyForGpuUpload;
            EnqueueGpuUpload(payload.Coord, chunk);
            SpawnChunkEntities(chunk);
        } else {
            chunk.GpuSplatTexture.reset();
            chunk.GpuSplatTexture1.reset();
            chunk.State = ChunkLoadState::CachedInRam;
            ReleaseChunkEntities(chunk);
        }
    }
}

void TerrainChunkManager::SynchronizeSharedBorders(TerrainChunk& chunk) noexcept {
    if (chunk.SplatData.empty() || chunk.SplatResolution < 2u) {
        return;
    }

    const uint32_t R = chunk.SplatResolution;
    const uint32_t last = R - 1u;

    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const TerrainChunk* neighbor = GetChunk({ chunk.Coord.cx + dx, chunk.Coord.cz + dz });
            if (!neighbor || neighbor->SplatData.empty() || neighbor->SplatResolution != R ||
                neighbor->State == ChunkLoadState::Generating) {
                continue;
            }
            if (!neighbor->UserModified && !chunk.UserModified) {
                continue;
            }

            const uint32_t xCount = (dx == 0) ? R : 1u;
            const uint32_t zCount = (dz == 0) ? R : 1u;
            const uint32_t dstX0 = (dx > 0) ? last : 0u;
            const uint32_t srcX0 = (dx < 0) ? last : 0u;
            const uint32_t dstZ0 = (dz > 0) ? last : 0u;
            const uint32_t srcZ0 = (dz < 0) ? last : 0u;

            const bool copySecondary = (chunk.SplatData1.size() == chunk.SplatData.size() &&
                                        neighbor->SplatData1.size() == neighbor->SplatData.size());
            for (uint32_t oz = 0; oz < zCount; ++oz) {
                const uint32_t dstZ = dstZ0 + oz;
                const uint32_t srcZ = srcZ0 + oz;
                for (uint32_t ox = 0; ox < xCount; ++ox) {
                    const uint32_t dstX = dstX0 + ox;
                    const uint32_t srcX = srcX0 + ox;
                    const size_t dstIdx = (static_cast<size_t>(dstZ) * R + dstX) * 4u;
                    const size_t srcIdx = (static_cast<size_t>(srcZ) * R + srcX) * 4u;
                    std::memcpy(&chunk.SplatData[dstIdx], &neighbor->SplatData[srcIdx], 4u);
                    if (copySecondary) {
                        std::memcpy(&chunk.SplatData1[dstIdx], &neighbor->SplatData1[srcIdx], 4u);
                    }
                }
            }
        }
    }
}

void TerrainChunkManager::EnqueueGpuUpload(ChunkCoord coord, TerrainChunk& chunk) {
    if (chunk.InUploadQueue || chunk.SplatData.empty()) {
        return;
    }
    chunk.InUploadQueue = true;
    m_GpuUploadRing.push_back(coord);
}

void TerrainChunkManager::DemoteAndEvictChunks(ChunkCoord centerChunk) {
    const int32_t activeRadius = std::max(0, ActiveRadiusChunks);
    const int32_t retentionRadius = std::max(activeRadius, CacheRetentionRadius);

    std::vector<std::pair<ChunkCoord, TerrainChunk*>> evictCandidates;
    for (auto& [coord, chunk] : m_Chunks) {
        if (chunk.State == ChunkLoadState::Generating) {
            continue;
        }
        const int32_t dist = std::max(
            std::abs(coord.cx - centerChunk.cx),
            std::abs(coord.cz - centerChunk.cz)
        );

        if (dist > retentionRadius) {
            evictCandidates.emplace_back(coord, &chunk);
        } else if (dist > activeRadius && chunk.State != ChunkLoadState::CachedInRam) {
            ENGINE_CORE_DEBUG("TerrainChunkManager: Demoting chunk ({}, {}) to RAM cache (dist={} > activeRadius={})",
                              coord.cx, coord.cz, dist, activeRadius);
            chunk.GpuSplatTexture.reset();
            chunk.GpuSplatTexture1.reset();
            chunk.State = ChunkLoadState::CachedInRam;
            chunk.InUploadQueue = false;
            ReleaseChunkEntities(chunk);
            const glm::vec2 minXZ{
                static_cast<float>(coord.cx) * ChunkSizeMeters,
                static_cast<float>(coord.cz) * ChunkSizeMeters
            };
            const glm::vec2 maxXZ{
                static_cast<float>(coord.cx + 1) * ChunkSizeMeters,
                static_cast<float>(coord.cz + 1) * ChunkSizeMeters
            };
            m_FireGrid.ExtinguishInBoundsXZ(minXZ, maxXZ);
        }
    }

    if (!evictCandidates.empty()) {
        // LRU order: unmodified chunks and oldest-touched entries evict first
        std::sort(evictCandidates.begin(), evictCandidates.end(), [](const auto& a, const auto& b) noexcept {
            if (a.second->UserModified != b.second->UserModified) {
                return !a.second->UserModified;
            }
            return a.second->LastAccessFrame < b.second->LastAccessFrame;
        });

        ENGINE_CORE_DEBUG("TerrainChunkManager: LRU-evicting {} chunk(s) beyond retention radius {}",
                          evictCandidates.size(), retentionRadius);
        for (const auto& [coord, chunkPtr] : evictCandidates) {
            ENGINE_CORE_TRACE("TerrainChunkManager: Evicting chunk ({}, {}) [userModified={}, lastAccess={}]",
                              coord.cx, coord.cz, chunkPtr->UserModified, chunkPtr->LastAccessFrame);
            ReleaseChunkEntities(*chunkPtr);
            const glm::vec2 minXZ{
                static_cast<float>(coord.cx) * ChunkSizeMeters,
                static_cast<float>(coord.cz) * ChunkSizeMeters
            };
            const glm::vec2 maxXZ{
                static_cast<float>(coord.cx + 1) * ChunkSizeMeters,
                static_cast<float>(coord.cz + 1) * ChunkSizeMeters
            };
            m_FireGrid.ExtinguishInBoundsXZ(minXZ, maxXZ);
            if (auto* owner = GetGameObject()) {
                if (auto* scene = owner->GetScene()) {
                    scene->GetDecalManager().ClearInChunk(coord.cx, coord.cz, ChunkSizeMeters);
                }
            }
            m_Chunks.erase(coord);
        }
    }

    if (!m_GpuUploadRing.empty()) {
        std::erase_if(m_GpuUploadRing, [this, activeRadius, centerChunk](const ChunkCoord& coord) {
            const auto it = m_Chunks.find(coord);
            if (it == m_Chunks.end()) {
                return true;
            }
            const int32_t dist = std::max(
                std::abs(coord.cx - centerChunk.cx),
                std::abs(coord.cz - centerChunk.cz)
            );
            if (dist > activeRadius || !it->second.IsResident()) {
                it->second.InUploadQueue = false;
                return true;
            }
            return false;
        });
    }
}

void TerrainChunkManager::UpdateStreaming(const glm::vec3& viewerWorldPos) {
    ++m_FrameCounter;
    ChunkSizeMeters = std::max(1.0f, ChunkSizeMeters);
    SplatResolution = std::clamp(SplatResolution, 5u, 513u);
    ActiveRadiusChunks = std::max(0, ActiveRadiusChunks);
    CacheRetentionRadius = std::max(ActiveRadiusChunks, CacheRetentionRadius);

    const ChunkCoord center = WorldToChunk(viewerWorldPos.x, viewerWorldPos.z);
    m_LastViewerChunk = center;

    // 1. Commit any background jobs completed before this frame
    DrainReadyPayloads();

    // 2. Request missing/cached chunks in the active window sorted closest-first
    std::vector<ChunkCoord> missingCoords;
    const int32_t r = ActiveRadiusChunks;
    missingCoords.reserve(static_cast<size_t>(2 * r + 1) * static_cast<size_t>(2 * r + 1));

    for (int32_t dz = -r; dz <= r; ++dz) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            const ChunkCoord coord{ center.cx + dx, center.cz + dz };
            auto it = m_Chunks.find(coord);
            if (it != m_Chunks.end()) {
                TerrainChunk& existing = it->second;
                existing.LastAccessFrame = m_FrameCounter;
                if (existing.State == ChunkLoadState::CachedInRam) {
                    existing.State = ChunkLoadState::ReadyForGpuUpload;
                    existing.MarkDirtyRect(0u, 0u, existing.SplatResolution - 1u, existing.SplatResolution - 1u, false);
                    EnqueueGpuUpload(coord, existing);
                    SpawnChunkEntities(existing);
                }
            } else {
                missingCoords.push_back(coord);
            }
        }
    }

    if (!missingCoords.empty()) {
        std::sort(missingCoords.begin(), missingCoords.end(), [center](const ChunkCoord& a, const ChunkCoord& b) noexcept {
            const int64_t adx = static_cast<int64_t>(a.cx) - center.cx;
            const int64_t adz = static_cast<int64_t>(a.cz) - center.cz;
            const int64_t bdx = static_cast<int64_t>(b.cx) - center.cx;
            const int64_t bdz = static_cast<int64_t>(b.cz) - center.cz;
            return (adx * adx + adz * adz) < (bdx * bdx + bdz * bdz);
        });

        for (const ChunkCoord& coord : missingCoords) {
            DispatchChunkGeneration(coord);
        }
    }

    // 3. Drain any immediately completed payloads (e.g. when JobSystem is uninitialized synchronous mode)
    DrainReadyPayloads();

    // 4. Demote out-of-window chunks to RAM cache and LRU-evict outside CacheRetentionRadius
    DemoteAndEvictChunks(center);

    // 5. Frame-pace GPU texture uploads via the main-thread upload ring
    ProcessGpuUploadRing();
}

void TerrainChunkManager::FlushPendingGenerations() {
    for (const auto& handle : m_InFlightJobs) {
        if (handle.IsValid()) {
            handle.Wait();
        }
    }
    m_InFlightJobs.clear();
    DrainReadyPayloads();
}

uint32_t TerrainChunkManager::ProcessGpuUploadRing() {
    const uint32_t budget = std::max(1u, MaxGpuUploadsPerFrame);
    uint32_t uploaded = 0;

    while (!m_GpuUploadRing.empty() && uploaded < budget) {
        const ChunkCoord coord = m_GpuUploadRing.front();
        m_GpuUploadRing.pop_front();

        auto it = m_Chunks.find(coord);
        if (it == m_Chunks.end()) {
            continue;
        }
        TerrainChunk& chunk = it->second;
        chunk.InUploadQueue = false;

        if (!chunk.IsResident() || chunk.SplatData.empty()) {
            continue;
        }

        chunk.SyncGpuSplatTexture();
        chunk.State = ChunkLoadState::Resident;
        ++uploaded;
    }

    m_LastFrameUploadCount = uploaded;
    if (uploaded > 0) {
        ENGINE_CORE_TRACE("TerrainChunkManager: Uploaded {} chunk splat texture(s) to GPU ({} remaining in ring)",
                          uploaded, m_GpuUploadRing.size());
    }
    return uploaded;
}

TerrainChunk& TerrainChunkManager::EnsureEditableChunk(ChunkCoord coord) {
    auto it = m_Chunks.find(coord);
    if (it != m_Chunks.end()) {
        TerrainChunk& existing = it->second;
        if (existing.SplatData.empty()) {
            existing.ChunkSizeMeters = std::max(1.0f, ChunkSizeMeters);
            existing.SplatResolution = std::clamp(SplatResolution, 5u, 513u);
            const size_t byteCount = static_cast<size_t>(existing.SplatResolution) * existing.SplatResolution * 4u;
            existing.SplatData.assign(byteCount, 0u);
            existing.SplatData1.assign(byteCount, 0u);
            for (size_t i = 0; i < existing.SplatData.size(); i += 4u) {
                existing.SplatData[i + 0] = 255u;
            }
        }
        if (existing.State == ChunkLoadState::Unloaded || existing.State == ChunkLoadState::Generating) {
            existing.State = ChunkLoadState::ReadyForGpuUpload;
        }
        existing.LastAccessFrame = m_FrameCounter;
        return existing;
    }

    const uint32_t res = std::clamp(SplatResolution, 5u, 513u);
    ENGINE_CORE_DEBUG("TerrainChunkManager::EnsureEditableChunk: Creating new editable chunk ({}, {}) [res={}x{}]",
                      coord.cx, coord.cz, res, res);
    TerrainChunk& created = m_Chunks[coord];
    created.Coord = coord;
    created.State = ChunkLoadState::ReadyForGpuUpload;
    created.ChunkSizeMeters = std::max(1.0f, ChunkSizeMeters);
    created.SplatResolution = res;
    created.LastAccessFrame = m_FrameCounter;
    const size_t byteCount = static_cast<size_t>(res) * res * 4u;
    created.SplatData.assign(byteCount, 0u);
    created.SplatData1.assign(byteCount, 0u);
    for (size_t i = 0; i < created.SplatData.size(); i += 4u) {
        created.SplatData[i + 0] = 255u;
    }
    SynchronizeSharedBorders(created);
    EnqueueGpuUpload(coord, created);
    return created;
}

void TerrainChunkManager::PaintWorldBrush(float worldX,
                                          float worldZ,
                                          uint32_t layerIndex,
                                          float radiusWorld,
                                          float falloff,
                                          float strength) {
    if (layerIndex >= TerrainComponent::MaxLayers || radiusWorld <= 0.0f || strength <= 0.0f) {
        ENGINE_CORE_WARN("TerrainChunkManager::PaintWorldBrush ignored invalid params (layer={}, radius={:.2f}, strength={:.2f})",
                         layerIndex, radiusWorld, strength);
        return;
    }

    const float pad = radiusWorld + 1e-3f;
    const ChunkCoord minCoord = WorldToChunk(worldX - pad, worldZ - pad);
    const ChunkCoord maxCoord = WorldToChunk(worldX + pad, worldZ + pad);
    uint32_t paintedChunks = 0;

    for (int32_t cz = minCoord.cz; cz <= maxCoord.cz; ++cz) {
        for (int32_t cx = minCoord.cx; cx <= maxCoord.cx; ++cx) {
            TerrainChunk* chunk = GetChunk({ cx, cz });
            if (!chunk || chunk->SplatData.empty() || chunk->State == ChunkLoadState::Generating) {
                continue;
            }

            uint32_t dMinX = 0, dMinZ = 0, dMaxX = 0, dMaxZ = 0;
            if (TerrainComponent::PaintSplatBufferXZ(
                    chunk->SplatData,
                    chunk->SplatData1,
                    chunk->SplatResolution,
                    chunk->SplatResolution,
                    cx,
                    cz,
                    /*originX=*/0.0f,
                    /*originZ=*/0.0f,
                    chunk->ChunkSizeMeters,
                    chunk->ChunkSizeMeters,
                    /*nodeAligned=*/true,
                    worldX,
                    worldZ,
                    layerIndex,
                    radiusWorld,
                    falloff,
                    strength,
                    dMinX,
                    dMinZ,
                    dMaxX,
                    dMaxZ)) {
                chunk->MarkDirtyRect(dMinX, dMinZ, dMaxX, dMaxZ, true);
                chunk->LastAccessFrame = m_FrameCounter;
                if (chunk->IsResident() && (!chunk->HasGpuTexture() || chunk->GpuSplatTexture1 == nullptr)) {
                    EnqueueGpuUpload({ cx, cz }, *chunk);
                }
                ++paintedChunks;
            }
        }
    }

    if (paintedChunks > 0) {
        ENGINE_CORE_TRACE("TerrainChunkManager::PaintWorldBrush: layer={} at ({:.2f}, {:.2f}) r={:.2f} -> {} chunk(s) dirtied",
                          layerIndex, worldX, worldZ, radiusWorld, paintedChunks);
    }
}

void TerrainChunkManager::PaintWorldSegment(const glm::vec2& startXZ,
                                            const glm::vec2& endXZ,
                                            uint32_t layerIndex,
                                            float radiusWorld,
                                            float falloff,
                                            float strength) {
    const float segLen = glm::length(endXZ - startXZ);
    if (segLen < 1e-4f) {
        PaintWorldBrush(startXZ.x, startXZ.y, layerIndex, radiusWorld, falloff, strength);
        return;
    }

    const float stepDist = std::max(0.1f, radiusWorld * 0.35f);
    const int steps = std::max(1, static_cast<int>(std::ceil(segLen / stepDist)));

    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec2 pt = glm::mix(startXZ, endXZ, t);
        PaintWorldBrush(pt.x, pt.y, layerIndex, radiusWorld, falloff, strength);
    }
}

void TerrainChunkManager::SetBlendSharpness(float sharpness) noexcept {
    m_BlendSharpness = std::clamp(sharpness, 0.0f, 1.0f);
}

void TerrainChunkManager::SetEdgeDarkening(float darkening) noexcept {
    m_EdgeDarkening = std::clamp(darkening, 0.0f, 0.5f);
}

void TerrainChunkManager::SetLayerTexture(uint32_t layerIndex, const std::shared_ptr<Texture>& texture) {
    TerrainHelpers::SetLayerTexture(m_Layers, layerIndex, texture);
}

void TerrainChunkManager::SetLayerHeightMapPath(uint32_t layerIndex, const std::string& path) {
    TerrainHelpers::SetLayerHeightMapPath(m_Layers, layerIndex, path);
}

void TerrainChunkManager::SetLayerNormalMapPath(uint32_t layerIndex, const std::string& path) {
    TerrainHelpers::SetLayerNormalMapPath(m_Layers, layerIndex, path);
}

void TerrainChunkManager::SetLayerTilingScale(uint32_t layerIndex, float tilingScale) noexcept {
    TerrainHelpers::SetLayerTilingScale(m_Layers, layerIndex, tilingScale);
}

void TerrainChunkManager::SetLayerGlossiness(uint32_t layerIndex, float glossiness) noexcept {
    TerrainHelpers::SetLayerGlossiness(m_Layers, layerIndex, glossiness);
}

const TerrainLayer& TerrainChunkManager::GetLayer(uint32_t layerIndex) const noexcept {
    return m_Layers[std::min(layerIndex, TerrainComponent::MaxLayers - 1u)];
}

TerrainLayer& TerrainChunkManager::GetLayer(uint32_t layerIndex) noexcept {
    return m_Layers[std::min(layerIndex, TerrainComponent::MaxLayers - 1u)];
}

size_t TerrainChunkManager::GetActiveChunkCount() const noexcept {
    size_t count = 0;
    for (const auto& [_, chunk] : m_Chunks) {
        if (chunk.IsResident() || chunk.State == ChunkLoadState::Generating) {
            ++count;
        }
    }
    return count;
}

size_t TerrainChunkManager::GetCachedChunkCount() const noexcept {
    size_t count = 0;
    for (const auto& [_, chunk] : m_Chunks) {
        if (chunk.State == ChunkLoadState::CachedInRam) {
            ++count;
        }
    }
    return count;
}

void TerrainChunkManager::ConfigureProceduralWorld(const WorldGenConfig& config) {
    m_WorldGenConfig = config;
    m_WorldGenConfig.OriginSafeRadius = std::max(0.5f, m_WorldGenConfig.OriginSafeRadius);
    m_WorldGenConfig.PoiApronRadius = std::max(0.5f, m_WorldGenConfig.PoiApronRadius);
    m_WorldGenConfig.PoiSuperCellChunks = std::max(1, m_WorldGenConfig.PoiSuperCellChunks);
    ENGINE_CORE_INFO("TerrainChunkManager: Configuring procedural world (enabled={}, seed={}, safeRadius={:.1f}m, poiSuperCell={} chunks)",
                     m_WorldGenConfig.Enabled, m_WorldGenConfig.Seed,
                     m_WorldGenConfig.OriginSafeRadius, m_WorldGenConfig.PoiSuperCellChunks);
    RegenerateWorld();
}

void TerrainChunkManager::RegenerateWorld() {
    ENGINE_CORE_INFO("TerrainChunkManager::RegenerateWorld: Clearing {} chunks (seed={})",
                     m_Chunks.size(), m_WorldGenConfig.Seed);
    ShutdownAsyncWorkers();
    m_FireGrid.Clear();
    for (auto& [coord, chunk] : m_Chunks) {
        (void)coord;
        ReleaseChunkEntities(chunk);
    }
    m_Chunks.clear();
    m_GpuUploadRing.clear();
    m_AsyncChannel = std::make_shared<AsyncChunkChannel>();
    m_BiomeGenerator = std::make_shared<const WorldBiomeGenerator>(m_WorldGenConfig);
}

BiomeSample TerrainChunkManager::SampleBiomeAtXZ(float worldX, float worldZ) const noexcept {
    if (m_BiomeGenerator) {
        return m_BiomeGenerator->EvaluateBiomeAtXZ(worldX, worldZ, ChunkSizeMeters);
    }
    return BiomeSample{};
}

void TerrainChunkManager::RegisterPropPrefab(ProceduralPropKind kind, const std::shared_ptr<Prefab>& prefab) {
    const size_t idx = static_cast<size_t>(kind);
    if (idx < m_PropPrefabs.size()) {
        m_PropPrefabs[idx] = prefab;
        ENGINE_CORE_DEBUG("TerrainChunkManager: Registered prop prefab for kind={} (valid={})",
                          idx, prefab && prefab->IsValid());
    } else {
        ENGINE_CORE_WARN("TerrainChunkManager::RegisterPropPrefab: Out-of-range prop kind index {}", idx);
    }
}

const std::shared_ptr<Prefab>& TerrainChunkManager::GetOrCreatePropPrefab(ProceduralPropKind kind) {
    const size_t idx = std::min(static_cast<size_t>(kind), m_PropPrefabs.size() - 1u);
    if (!m_PropPrefabs[idx]) {
        m_PropPrefabs[idx] = ProceduralPropFactory::BuildBuiltinPropPrefab(static_cast<ProceduralPropKind>(idx));
    }
    return m_PropPrefabs[idx];
}

void TerrainChunkManager::SpawnChunkEntities(TerrainChunk& chunk) {
    if (chunk.EntitiesSpawned || !m_WorldGenConfig.Enabled || !m_WorldGenConfig.SpawnSceneEntities) {
        return;
    }
    GameObject* owner = GetGameObject();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (!scene) {
        ENGINE_CORE_WARN("TerrainChunkManager::SpawnChunkEntities: Missing owner/scene for chunk ({}, {})",
                         chunk.Coord.cx, chunk.Coord.cz);
        return;
    }

    auto acquireProp = [&](ProceduralPropKind kind,
                           const glm::vec3& pos,
                           float scaleMult,
                           float windPhase) {
        const size_t idx = static_cast<size_t>(kind);
        if (idx >= m_PropPrefabs.size()) {
            return;
        }
        const auto& prefab = GetOrCreatePropPrefab(kind);
        if (!prefab || !prefab->IsValid()) {
            ENGINE_CORE_WARN("TerrainChunkManager: Skipping prop kind {} due to invalid prefab", idx);
            return;
        }
        const auto& spec = ProceduralPropFactory::GetPropSpec(kind);
        GameObject* spawned = scene->AcquirePooled(spec.PoolKey, *prefab, pos);
        if (!spawned) {
            ENGINE_CORE_WARN("TerrainChunkManager: Failed to acquire pooled prop '{}' at ({:.2f}, {:.2f}, {:.2f})",
                             spec.PoolKey, pos.x, pos.y, pos.z);
            return;
        }
        if (auto* t = spawned->GetTransform()) {
            t->scale = spec.BaseScale * scaleMult;
            t->SnapshotPrevious();
            t->SetDirty();
            scene->MarkPhysicsDirty(spawned);
        }
        if (auto* sprite = spawned->GetComponent<SpriteRenderer>()) {
            sprite->SetWindPhaseOffset(windPhase);
        }
        chunk.SpawnedEntityUUIDs.push_back(spawned->GetUUID());
    };

    for (const auto& prop : chunk.Props) {
        acquireProp(prop.Kind, prop.WorldPosition, prop.ScaleMultiplier, prop.WindPhaseOffset);
    }
    for (const auto& poi : chunk.POIs) {
        acquireProp(poi.Kind, poi.WorldPosition, 1.0f, 0.0f);
    }
    chunk.EntitiesSpawned = true;
    ENGINE_CORE_DEBUG("TerrainChunkManager: Spawned {} entity/entities for chunk ({}, {}) [props={}, POIs={}]",
                      chunk.SpawnedEntityUUIDs.size(), chunk.Coord.cx, chunk.Coord.cz,
                      chunk.Props.size(), chunk.POIs.size());
}

void TerrainChunkManager::ReleaseChunkEntities(TerrainChunk& chunk) {
    if (!chunk.EntitiesSpawned) {
        return;
    }
    ENGINE_CORE_DEBUG("TerrainChunkManager: Releasing {} entity/entities for chunk ({}, {})",
                      chunk.SpawnedEntityUUIDs.size(), chunk.Coord.cx, chunk.Coord.cz);
    GameObject* owner = GetGameObject();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (scene) {
        for (uint64_t uuid : chunk.SpawnedEntityUUIDs) {
            if (GameObject* go = scene->FindGameObjectByUUID(uuid)) {
                scene->ReleaseToPool(go);
            }
        }
    }
    chunk.SpawnedEntityUUIDs.clear();
    chunk.EntitiesSpawned = false;
}

void TerrainChunkManager::InitFireGrid() {
    m_FireGrid.ConfigureCallbacks(
        [this](float wx, float wz) -> float {
            return SampleSurfaceAtXZ(wx, wz).Flammability;
        },
        [this](const glm::vec2& centerXZ, float radius, float strength) {
            PaintMaterialWorld(centerXZ, 5u /*Scorched Ash*/, radius, strength);
            if (auto* owner = GetGameObject()) {
                if (auto* scene = owner->GetScene()) {
                    GroundDecal decal{};
                    decal.Position = glm::vec3(centerXZ.x, 0.0f, centerXZ.y);
                    decal.SizeXZ = glm::vec2(radius * 2.0f);
                    decal.Color = glm::vec4(0.12f, 0.10f, 0.08f, 0.85f);
                    decal.MaxLifetime = 30.0f;
                    decal.FadeOutDuration = 5.0f;
                    scene->GetDecalManager().SpawnDecal(decal);
                }
            }
        },
        [this](const glm::vec2& centerXZ) -> uint64_t {
            return SpawnFirewallEntity(centerXZ);
        },
        [this](uint64_t uuid) {
            ReleaseFirewallEntity(uuid);
        }
    );
}

uint64_t TerrainChunkManager::SpawnFirewallEntity(const glm::vec2& centerXZ) {
    GameObject* owner = GetGameObject();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (!scene) {
        return 0;
    }

    static const std::shared_ptr<Prefab> s_FirewallPrefab = ProceduralPropFactory::BuildTarFirewallPrefab();
    if (!s_FirewallPrefab) {
        return 0;
    }

    GameObject* firewall = scene->GetPoolManager().AcquirePooled(
        "pool_tar_firewall", *s_FirewallPrefab, glm::vec3(centerXZ.x, 0.45f, centerXZ.y));
    if (!firewall) {
        ENGINE_CORE_WARN("TerrainChunkManager::SpawnFirewallEntity: Failed to acquire TarFirewall at ({:.2f}, {:.2f})",
                         centerXZ.x, centerXZ.y);
        return 0;
    }

    if (auto* t = firewall->GetTransform()) {
        t->position = glm::vec3(centerXZ.x, 0.45f, centerXZ.y);
        t->SnapshotPrevious();
    }

    return firewall->GetUUID();
}

void TerrainChunkManager::ReleaseFirewallEntity(uint64_t uuid) {
    if (uuid == 0) return;
    GameObject* owner = GetGameObject();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (scene) {
        if (GameObject* go = scene->FindGameObjectByUUID(uuid)) {
            ENGINE_CORE_TRACE("TerrainChunkManager: Releasing TarFirewall entity (UUID: {})", uuid);
            scene->GetPoolManager().ReleaseToPool("pool_tar_firewall", go);
        }
    }
}

SurfaceSample TerrainChunkManager::SampleSurfaceAtXZ(float worldX, float worldZ) const noexcept {
    const ChunkCoord coord = WorldToChunk(worldX, worldZ);
    const TerrainChunk* chunk = GetChunk(coord);
    const bool isBurning = m_FireGrid.IsBurningAtXZ(worldX, worldZ);

    if (chunk && (chunk->IsResident() || chunk->State == ChunkLoadState::CachedInRam) &&
        !chunk->SplatData.empty() && chunk->SplatResolution >= 2u) {
        const float chunkOriginX = static_cast<float>(coord.cx) * ChunkSizeMeters;
        const float chunkOriginZ = static_cast<float>(coord.cz) * ChunkSizeMeters;
        const float u = std::clamp((worldX - chunkOriginX) / ChunkSizeMeters, 0.0f, 1.0f);
        const float v = std::clamp((worldZ - chunkOriginZ) / ChunkSizeMeters, 0.0f, 1.0f);

        const float span = static_cast<float>(chunk->SplatResolution - 1u);
        return TerrainHelpers::EvaluateBilinearSurfaceSample(
            chunk->SplatData, chunk->SplatData1, chunk->SplatResolution, chunk->SplatResolution,
            u * span, v * span, isBurning);
    }

    if (!m_WorldGenConfig.Enabled) {
        SurfaceSample defaultSample{};
        defaultSample.DominantBiome = BiomeType::AshBarrens;
        defaultSample.LinearDamping = SurfacePhysics::LayerDamping[1];
        defaultSample.MaxSpeedMultiplier = SurfacePhysics::LayerSpeedMultiplier[1];
        defaultSample.Flammability = 0.0f;
        defaultSample.IsBurning = isBurning;
        return defaultSample;
    }

    const BiomeSample bs = SampleBiomeAtXZ(worldX, worldZ);
    const auto w8 = bs.ToSplat8();
    std::array<float, TerrainComponent::MaxLayers> fallbackWeights{};
    float sumWeights = 0.0f;
    for (size_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        fallbackWeights[i] = static_cast<float>(w8[i]);
        sumWeights += fallbackWeights[i];
    }
    return TerrainHelpers::EvaluateSurfaceFromWeights8(fallbackWeights, sumWeights, isBurning);
}

void TerrainChunkManager::IgniteAtXZ(const glm::vec2& centerXZ, float radius) {
    m_FireGrid.IgniteAtXZ(centerXZ, radius);
}

void TerrainChunkManager::ExtinguishAtXZ(const glm::vec2& centerXZ, float radius) {
    m_FireGrid.ExtinguishAtXZ(centerXZ, radius);
}

void TerrainChunkManager::ExtinguishInBoundsXZ(const glm::vec2& minXZ, const glm::vec2& maxXZ) {
    m_FireGrid.ExtinguishInBoundsXZ(minXZ, maxXZ);
}

bool TerrainChunkManager::IsBurningAtXZ(float worldX, float worldZ) const noexcept {
    return m_FireGrid.IsBurningAtXZ(worldX, worldZ);
}

void TerrainChunkManager::UpdateFire(float dt) {
    m_FireGrid.Update(dt);
}

} // namespace Engine
