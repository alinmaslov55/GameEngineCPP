#include "Engine/Scene/TerrainFireGrid.h"

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

TerrainFireGrid::~TerrainFireGrid() {
    Clear();
}

void TerrainFireGrid::ConfigureCallbacks(FlammabilityQuery flammabilityQuery,
                                        PaintScorchedCallback paintScorched,
                                        SpawnVisualCallback spawnVisual,
                                        ReleaseVisualCallback releaseVisual) {
    m_FlammabilityQuery = std::move(flammabilityQuery);
    m_PaintScorched = std::move(paintScorched);
    m_SpawnVisual = std::move(spawnVisual);
    m_ReleaseVisual = std::move(releaseVisual);
}

void TerrainFireGrid::IgniteAtXZ(const glm::vec2& centerXZ, float radius) {
    const float safeRadius = std::max(radius, 0.05f);
    const float radSq = safeRadius * safeRadius;

    const int32_t minX = static_cast<int32_t>(std::floor((centerXZ.x - safeRadius) / CellSize));
    const int32_t maxX = static_cast<int32_t>(std::floor((centerXZ.x + safeRadius) / CellSize));
    const int32_t minZ = static_cast<int32_t>(std::floor((centerXZ.y - safeRadius) / CellSize));
    const int32_t maxZ = static_cast<int32_t>(std::floor((centerXZ.y + safeRadius) / CellSize));

    for (int32_t cz = minZ; cz <= maxZ; ++cz) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            const FireCellCoord coord{ cx, cz };
            if (m_ActiveCells.contains(coord)) {
                continue;
            }

            const glm::vec2 center = CoordToWorldCenter(coord);
            const glm::vec2 delta = center - centerXZ;
            if (glm::dot(delta, delta) > radSq) {
                continue;
            }

            const float flammability = m_FlammabilityQuery ? m_FlammabilityQuery(center.x, center.y) : 0.0f;
            if (flammability >= MinFlammability) {
                ActiveFireCell cell{};
                cell.Coord = coord;
                cell.BurnTime = 0.0f;
                cell.SpreadTimer = 0.0f;
                cell.HasSpread = false;
                if (m_SpawnVisual) {
                    cell.VisualEntityUUID = m_SpawnVisual(center);
                }
                m_ActiveCells.emplace(coord, cell);
            }
        }
    }
}

void TerrainFireGrid::ExtinguishAtXZ(const glm::vec2& centerXZ, float radius) {
    const float safeRadius = std::max(radius, 0.05f);
    const float radSq = safeRadius * safeRadius;

    std::vector<FireCellCoord> toRemove;
    toRemove.reserve(m_ActiveCells.size());

    for (const auto& [coord, cell] : m_ActiveCells) {
        const glm::vec2 center = CoordToWorldCenter(coord);
        const glm::vec2 delta = center - centerXZ;
        if (glm::dot(delta, delta) <= radSq) {
            toRemove.push_back(coord);
        }
    }

    for (const auto& coord : toRemove) {
        auto it = m_ActiveCells.find(coord);
        if (it != m_ActiveCells.end()) {
            if (m_ReleaseVisual && it->second.VisualEntityUUID != 0) {
                m_ReleaseVisual(it->second.VisualEntityUUID);
            }
            m_ActiveCells.erase(it);
        }
    }
}

void TerrainFireGrid::Clear() {
    if (m_ReleaseVisual) {
        for (const auto& [coord, cell] : m_ActiveCells) {
            if (cell.VisualEntityUUID != 0) {
                m_ReleaseVisual(cell.VisualEntityUUID);
            }
        }
    }
    m_ActiveCells.clear();
    m_TickAccumulator = 0.0f;
}

bool TerrainFireGrid::IsBurningAtXZ(float worldX, float worldZ) const noexcept {
    if (m_ActiveCells.empty()) {
        return false;
    }
    return m_ActiveCells.contains(WorldToCoord(worldX, worldZ));
}

void TerrainFireGrid::Update(float dt) {
    if (dt <= 0.0f || m_ActiveCells.empty()) {
        m_TickAccumulator = 0.0f;
        return;
    }

    m_TickAccumulator += dt;
    while (m_TickAccumulator >= TickInterval) {
        m_TickAccumulator -= TickInterval;
        SimulateTick(TickInterval);
        if (m_ActiveCells.empty()) {
            m_TickAccumulator = 0.0f;
            break;
        }
    }
}

void TerrainFireGrid::SimulateTick(float tickDt) {
    if (m_ActiveCells.empty()) {
        return;
    }

    static constexpr std::pair<int32_t, int32_t> kNeighbors[8] = {
        { -1, -1 }, {  0, -1 }, {  1, -1 },
        { -1,  0 },             {  1,  0 },
        { -1,  1 }, {  0,  1 }, {  1,  1 }
    };

    std::vector<FireCellCoord> pendingIgnitions;
    std::vector<FireCellCoord> pendingDecays;
    pendingIgnitions.reserve(m_ActiveCells.size() * 4);
    pendingDecays.reserve(m_ActiveCells.size());

    for (auto& [coord, cell] : m_ActiveCells) {
        cell.BurnTime += tickDt;
        cell.SpreadTimer += tickDt;

        if (!cell.HasSpread && cell.SpreadTimer >= SpreadDelay) {
            cell.HasSpread = true;
            for (const auto& [dx, dz] : kNeighbors) {
                const FireCellCoord nCoord{ coord.x + dx, coord.z + dz };
                if (m_ActiveCells.contains(nCoord)) {
                    continue;
                }
                const glm::vec2 nCenter = CoordToWorldCenter(nCoord);
                const float flam = m_FlammabilityQuery ? m_FlammabilityQuery(nCenter.x, nCenter.y) : 0.0f;
                if (flam >= MinFlammability) {
                    pendingIgnitions.push_back(nCoord);
                }
            }
        }

        if (cell.BurnTime >= MaxBurnDuration) {
            pendingDecays.push_back(coord);
        }
    }

    // Process new ignitions (deduplicating)
    std::sort(pendingIgnitions.begin(), pendingIgnitions.end(), [](const FireCellCoord& a, const FireCellCoord& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });
    pendingIgnitions.erase(std::unique(pendingIgnitions.begin(), pendingIgnitions.end()), pendingIgnitions.end());

    for (const auto& coord : pendingIgnitions) {
        if (!m_ActiveCells.contains(coord)) {
            ActiveFireCell newCell{};
            newCell.Coord = coord;
            newCell.BurnTime = 0.0f;
            newCell.SpreadTimer = 0.0f;
            newCell.HasSpread = false;
            if (m_SpawnVisual) {
                newCell.VisualEntityUUID = m_SpawnVisual(CoordToWorldCenter(coord));
            }
            m_ActiveCells.emplace(coord, newCell);
        }
    }

    // Process decays: paint Scorched Ash (Layer 5) and release visual entities
    for (const auto& coord : pendingDecays) {
        auto it = m_ActiveCells.find(coord);
        if (it != m_ActiveCells.end()) {
            const glm::vec2 center = CoordToWorldCenter(coord);
            if (m_PaintScorched) {
                m_PaintScorched(center, CellSize * 1.1f, 0.85f);
            }
            if (m_ReleaseVisual && it->second.VisualEntityUUID != 0) {
                m_ReleaseVisual(it->second.VisualEntityUUID);
            }
            m_ActiveCells.erase(it);
        }
    }
}

} // namespace Engine
