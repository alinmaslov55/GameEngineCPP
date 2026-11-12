#ifndef SCENE_TERRAINFIREGRID_H
#define SCENE_TERRAINFIREGRID_H

#include <cmath>
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <unordered_map>
#include <vector>

namespace Engine {

struct FireCellCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const FireCellCoord&) const noexcept = default;
};

struct FireCellCoordHash {
    [[nodiscard]] size_t operator()(const FireCellCoord& c) const noexcept {
        uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) |
                           static_cast<uint64_t>(static_cast<uint32_t>(c.z));
        packed ^= packed >> 33;
        packed *= 0xff51afd7ed558ccdULL;
        packed ^= packed >> 33;
        return static_cast<size_t>(packed);
    }
};

struct ActiveFireCell {
    FireCellCoord Coord{};
    float BurnTime = 0.0f;       // Counts up to MaxBurnDuration (4.5s)
    float SpreadTimer = 0.0f;    // Counts up to SpreadDelay (0.18s)
    bool HasSpread = false;      // True once it has attempted ignition on 8 neighbors
    uint64_t VisualEntityUUID = 0;
};

/// @brief Systemic Tar & Fire Cellular Automaton ticking at 10 Hz over a 0.5m world grid
class TerrainFireGrid {
public:
    static constexpr float CellSize = 0.5f;          // 0.5m grid cell footprint
    static constexpr float TickRateHz = 10.0f;       // 10 Hz cellular automaton tick rate
    static constexpr float TickInterval = 0.10f;     // 100 ms between ticks
    static constexpr float SpreadDelay = 0.18f;      // 0.18s delay before spreading to 8 neighbors
    static constexpr float MaxBurnDuration = 4.5f;   // 4.5s burn lifespan before decay to Scorched Ash
    static constexpr float MinFlammability = 0.35f;  // Flammability threshold to catch fire

    using FlammabilityQuery = std::function<float(float worldX, float worldZ)>;
    using PaintScorchedCallback = std::function<void(const glm::vec2& centerXZ, float radius, float strength)>;
    using SpawnVisualCallback = std::function<uint64_t(const glm::vec2& centerXZ)>;
    using ReleaseVisualCallback = std::function<void(uint64_t uuid)>;

    TerrainFireGrid() = default;
    ~TerrainFireGrid();

    TerrainFireGrid(const TerrainFireGrid&) = delete;
    TerrainFireGrid& operator=(const TerrainFireGrid&) = delete;
    TerrainFireGrid(TerrainFireGrid&&) noexcept = default;
    TerrainFireGrid& operator=(TerrainFireGrid&&) noexcept = default;

    void ConfigureCallbacks(FlammabilityQuery flammabilityQuery,
                            PaintScorchedCallback paintScorched,
                            SpawnVisualCallback spawnVisual = nullptr,
                            ReleaseVisualCallback releaseVisual = nullptr);

    void IgniteAtXZ(const glm::vec2& centerXZ, float radius);
    void ExtinguishAtXZ(const glm::vec2& centerXZ, float radius);
    void Clear();

    void Update(float dt);

    [[nodiscard]] bool IsBurningAtXZ(float worldX, float worldZ) const noexcept;
    [[nodiscard]] size_t GetActiveFireCount() const noexcept { return m_ActiveCells.size(); }
    [[nodiscard]] const std::unordered_map<FireCellCoord, ActiveFireCell, FireCellCoordHash>& GetActiveCells() const noexcept {
        return m_ActiveCells;
    }

    [[nodiscard]] static FireCellCoord WorldToCoord(float x, float z) noexcept {
        return {
            static_cast<int32_t>(std::floor(x / CellSize)),
            static_cast<int32_t>(std::floor(z / CellSize))
        };
    }

    [[nodiscard]] static glm::vec2 CoordToWorldCenter(FireCellCoord coord) noexcept {
        return {
            (static_cast<float>(coord.x) + 0.5f) * CellSize,
            (static_cast<float>(coord.z) + 0.5f) * CellSize
        };
    }

private:
    void SimulateTick(float tickDt);

    std::unordered_map<FireCellCoord, ActiveFireCell, FireCellCoordHash> m_ActiveCells;
    float m_TickAccumulator = 0.0f;

    FlammabilityQuery m_FlammabilityQuery;
    PaintScorchedCallback m_PaintScorched;
    SpawnVisualCallback m_SpawnVisual;
    ReleaseVisualCallback m_ReleaseVisual;
};

} // namespace Engine

#endif // SCENE_TERRAINFIREGRID_H
