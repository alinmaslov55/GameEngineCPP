#include "Engine/Scene/TerrainChunkManager.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Engine {

glm::vec3 TerrainChunk::GetWorldOrigin(float groundY) const noexcept {
    return {
        static_cast<float>(Coord.cx) * ChunkSizeMeters,
        groundY,
        static_cast<float>(Coord.cz) * ChunkSizeMeters
    };
}

glm::u8vec4 TerrainChunk::GetSplatTexel(uint32_t px, uint32_t pz) const noexcept {
    if (px >= SplatResolution || pz >= SplatResolution || SplatData.empty()) {
        return { 255, 0, 0, 0 };
    }
    const size_t idx = (static_cast<size_t>(pz) * SplatResolution + px) * 4u;
    return {
        SplatData[idx + 0],
        SplatData[idx + 1],
        SplatData[idx + 2],
        SplatData[idx + 3]
    };
}

void TerrainChunk::SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights) noexcept {
    if (px >= SplatResolution || pz >= SplatResolution || SplatData.empty()) {
        return;
    }
    const glm::u8vec4 norm = TerrainComponent::NormalizeWeights(
        static_cast<float>(weights.r),
        static_cast<float>(weights.g),
        static_cast<float>(weights.b),
        static_cast<float>(weights.a)
    );
    const size_t idx = (static_cast<size_t>(pz) * SplatResolution + px) * 4u;
    SplatData[idx + 0] = norm.r;
    SplatData[idx + 1] = norm.g;
    SplatData[idx + 2] = norm.b;
    SplatData[idx + 3] = norm.a;
    MarkDirtyRect(px, pz, px, pz, true);
}

void TerrainChunk::MarkDirtyRect(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, bool markModified) noexcept {
    if (!GpuDirty) {
        DirtyMinX = minX;
        DirtyMinY = minY;
        DirtyMaxX = maxX;
        DirtyMaxY = maxY;
        GpuDirty = true;
    } else {
        DirtyMinX = std::min(DirtyMinX, minX);
        DirtyMinY = std::min(DirtyMinY, minY);
        DirtyMaxX = std::max(DirtyMaxX, maxX);
        DirtyMaxY = std::max(DirtyMaxY, maxY);
    }

    if (markModified) {
        UserModified = true;
    }
}

const std::shared_ptr<Texture>& TerrainChunk::SyncGpuSplatTexture() {
    return TerrainComponent::SyncRgbaGpuTexture(
        GpuSplatTexture,
        UploadScratch,
        SplatData,
        SplatResolution,
        SplatResolution,
        GpuDirty,
        DirtyMinX,
        DirtyMinY,
        DirtyMaxX,
        DirtyMaxY
    );
}

TerrainChunkManager::TerrainChunkManager()
    : m_AsyncChannel(std::make_shared<AsyncChunkChannel>()) {}

TerrainChunkManager::TerrainChunkManager(float chunkSizeMeters,
                                         uint32_t splatResolution,
                                         int32_t activeRadiusChunks,
                                         int32_t cacheRetentionRadius)
    : ChunkSizeMeters(std::max(1.0f, chunkSizeMeters)),
      SplatResolution(std::clamp(splatResolution, 5u, 513u)),
      ActiveRadiusChunks(std::max(0, activeRadiusChunks)),
      CacheRetentionRadius(std::max(ActiveRadiusChunks, cacheRetentionRadius)),
      m_AsyncChannel(std::make_shared<AsyncChunkChannel>()) {}

TerrainChunkManager::~TerrainChunkManager() {
    ShutdownAsyncWorkers();
}

void TerrainChunkManager::OnDestroy() {
    ShutdownAsyncWorkers();
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

    TerrainChunk& placeholder = m_Chunks[coord];
    placeholder.Coord = coord;
    placeholder.State = ChunkLoadState::Generating;
    placeholder.ChunkSizeMeters = chunkSize;
    placeholder.SplatResolution = res;
    placeholder.LastAccessFrame = m_FrameCounter;

    auto channel = m_AsyncChannel;
    JobHandle handle = JobSystem::Dispatch([channel, coord, res, chunkSize]() {
        if (!channel || channel->ShuttingDown.load(std::memory_order_acquire)) {
            return;
        }

        const size_t texelCount = static_cast<size_t>(res) * static_cast<size_t>(res);
        std::vector<uint8_t> splat(texelCount * 4u, 0u);
        for (size_t i = 0; i < splat.size(); i += 4u) {
            splat[i + 0] = 255u;
        }

        if (channel->ShuttingDown.load(std::memory_order_acquire)) {
            return;
        }

        GeneratedChunkPayload payload;
        payload.Coord = coord;
        payload.Resolution = res;
        payload.ChunkSize = chunkSize;
        payload.SplatData = std::move(splat);

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

    for (auto& payload : payloads) {
        auto it = m_Chunks.find(payload.Coord);
        if (it == m_Chunks.end()) {
            continue;
        }
        TerrainChunk& chunk = it->second;
        chunk.ChunkSizeMeters = payload.ChunkSize;
        chunk.SplatResolution = payload.Resolution;
        chunk.SplatData = std::move(payload.SplatData);
        const int32_t dist = std::max(
            std::abs(payload.Coord.cx - m_LastViewerChunk.cx),
            std::abs(payload.Coord.cz - m_LastViewerChunk.cz)
        );
        if (dist > retentionRadius) {
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
        } else {
            chunk.GpuSplatTexture.reset();
            chunk.State = ChunkLoadState::CachedInRam;
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

            for (uint32_t oz = 0; oz < zCount; ++oz) {
                const uint32_t dstZ = dstZ0 + oz;
                const uint32_t srcZ = srcZ0 + oz;
                for (uint32_t ox = 0; ox < xCount; ++ox) {
                    const uint32_t dstX = dstX0 + ox;
                    const uint32_t srcX = srcX0 + ox;
                    const size_t dstIdx = (static_cast<size_t>(dstZ) * R + dstX) * 4u;
                    const size_t srcIdx = (static_cast<size_t>(srcZ) * R + srcX) * 4u;
                    std::memcpy(&chunk.SplatData[dstIdx], &neighbor->SplatData[srcIdx], 4u);
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
            chunk.GpuSplatTexture.reset();
            chunk.State = ChunkLoadState::CachedInRam;
            chunk.InUploadQueue = false;
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

        for (const auto& [coord, _] : evictCandidates) {
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
    return uploaded;
}

TerrainChunk& TerrainChunkManager::EnsureEditableChunk(ChunkCoord coord) {
    auto it = m_Chunks.find(coord);
    if (it != m_Chunks.end()) {
        TerrainChunk& existing = it->second;
        if (existing.SplatData.empty()) {
            existing.ChunkSizeMeters = std::max(1.0f, ChunkSizeMeters);
            existing.SplatResolution = std::clamp(SplatResolution, 5u, 513u);
            existing.SplatData.assign(static_cast<size_t>(existing.SplatResolution) * existing.SplatResolution * 4u, 0u);
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
    TerrainChunk& created = m_Chunks[coord];
    created.Coord = coord;
    created.State = ChunkLoadState::ReadyForGpuUpload;
    created.ChunkSizeMeters = std::max(1.0f, ChunkSizeMeters);
    created.SplatResolution = res;
    created.LastAccessFrame = m_FrameCounter;
    created.SplatData.assign(static_cast<size_t>(res) * res * 4u, 0u);
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
        return;
    }

    const float pad = radiusWorld + 1e-3f;
    const ChunkCoord minCoord = WorldToChunk(worldX - pad, worldZ - pad);
    const ChunkCoord maxCoord = WorldToChunk(worldX + pad, worldZ + pad);

    for (int32_t cz = minCoord.cz; cz <= maxCoord.cz; ++cz) {
        for (int32_t cx = minCoord.cx; cx <= maxCoord.cx; ++cx) {
            TerrainChunk* chunk = GetChunk({ cx, cz });
            if (!chunk || chunk->SplatData.empty() || chunk->State == ChunkLoadState::Generating) {
                continue;
            }

            uint32_t dMinX = 0, dMinZ = 0, dMaxX = 0, dMaxZ = 0;
            if (TerrainComponent::PaintSplatBufferXZ(
                    chunk->SplatData,
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
                if (chunk->IsResident() && !chunk->HasGpuTexture()) {
                    EnqueueGpuUpload({ cx, cz }, *chunk);
                }
            }
        }
    }
}

void TerrainChunkManager::PaintWorldSegment(const glm::vec2& startXZ,
                                            const glm::vec2& endXZ,
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
    if (layerIndex >= TerrainComponent::MaxLayers) {
        return;
    }
    if (texture) {
        texture->SetWrapRepeat(true);
    }
    m_Layers[layerIndex].LayerTexture = texture;
}

void TerrainChunkManager::SetLayerTilingScale(uint32_t layerIndex, float tilingScale) noexcept {
    if (layerIndex >= TerrainComponent::MaxLayers) {
        return;
    }
    m_Layers[layerIndex].TilingScale = std::clamp(tilingScale, 0.02f, 20.0f);
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

} // namespace Engine
