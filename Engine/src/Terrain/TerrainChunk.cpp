#include "Engine/Terrain/TerrainChunk.h"
#include "Engine/Terrain/TerrainHelpers.h"

#include <algorithm>

namespace Engine {

glm::vec3 TerrainChunk::GetWorldOrigin(float groundY) const noexcept {
    return {
        static_cast<float>(Coord.cx) * ChunkSizeMeters,
        groundY,
        static_cast<float>(Coord.cz) * ChunkSizeMeters
    };
}

glm::u8vec4 TerrainChunk::GetSplatTexel(uint32_t px, uint32_t pz) const noexcept {
    return TerrainHelpers::ReadSplatRgbaTexel(SplatData, SplatResolution, SplatResolution, px, pz, { 255, 0, 0, 0 });
}

glm::u8vec4 TerrainChunk::GetSplatTexel1(uint32_t px, uint32_t pz) const noexcept {
    return TerrainHelpers::ReadSplatRgbaTexel(SplatData1, SplatResolution, SplatResolution, px, pz, { 0, 0, 0, 0 });
}

std::array<uint8_t, TerrainComponent::MaxLayers> TerrainChunk::GetSplatTexel8(uint32_t px, uint32_t pz) const noexcept {
    return TerrainHelpers::ReadSplatTexel8(SplatData, SplatData1, SplatResolution, SplatResolution, px, pz);
}

void TerrainChunk::SetSplatTexel(uint32_t px, uint32_t pz, const glm::u8vec4& weights) noexcept {
    SetSplatTexel8(px, pz, { weights.r, weights.g, weights.b, weights.a, 0u, 0u, 0u, 0u });
}

void TerrainChunk::SetSplatTexel8(uint32_t px, uint32_t pz, const std::array<uint8_t, TerrainComponent::MaxLayers>& weights) noexcept {
    if (TerrainHelpers::WriteSplatTexel8(SplatData, SplatData1, SplatResolution, SplatResolution, px, pz, weights)) {
        MarkDirtyRect(px, pz, px, pz, true);
    }
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
    bool dirty0 = GpuDirty;
    bool dirty1 = GpuDirty;
    TerrainComponent::SyncRgbaGpuTexture(
        GpuSplatTexture,
        UploadScratch,
        SplatData,
        SplatResolution,
        SplatResolution,
        dirty0,
        DirtyMinX,
        DirtyMinY,
        DirtyMaxX,
        DirtyMaxY
    );
    TerrainComponent::SyncRgbaGpuTexture(
        GpuSplatTexture1,
        UploadScratch,
        SplatData1,
        SplatResolution,
        SplatResolution,
        dirty1,
        DirtyMinX,
        DirtyMinY,
        DirtyMaxX,
        DirtyMaxY
    );
    GpuDirty = dirty0 || dirty1;
    return GpuSplatTexture;
}

std::pair<std::shared_ptr<Texture>, std::shared_ptr<Texture>> TerrainChunk::SyncGpuSplatTextures() {
    SyncGpuSplatTexture();
    return { GpuSplatTexture, GpuSplatTexture1 };
}

} // namespace Engine
