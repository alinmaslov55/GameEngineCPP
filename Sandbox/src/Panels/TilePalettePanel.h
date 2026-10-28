#ifndef SANDBOX_TILEPALETTEPANEL_H
#define SANDBOX_TILEPALETTEPANEL_H

#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/TerrainComponent.h>

#include <cstdint>
#include <memory>

namespace Sandbox {

enum class GroundPaintMode {
    TerrainSplat = 0,
    TileGrid = 1
};

class TilePalettePanel {
public:
    void OnImGuiRender(const std::shared_ptr<Engine::Scene>& activeScene,
                       Engine::GameObject* selectedEntity,
                       bool* pOpen);

    GroundPaintMode GetPaintMode() const { return m_PaintMode; }
    void SetPaintMode(GroundPaintMode mode) { m_PaintMode = mode; }

    // Terrain Splatmap Brush Settings
    uint32_t GetSelectedSplatLayer() const { return m_SelectedSplatLayer; }
    float GetBrushRadius() const { return m_BrushRadius; }
    void SetBrushRadius(float radius);
    float GetBrushFalloff() const { return m_BrushFalloff; }
    float GetBrushStrength() const { return m_BrushStrength; }

    // Discrete Tilemap Brush Settings
    uint32_t GetSelectedTileID() const { return m_SelectedTileID; }
    bool IsSelectedTileWalkable() const { return m_SelectedTileWalkable; }

    static Engine::GameObject* ResolveTargetTerrainEntity(const std::shared_ptr<Engine::Scene>& activeScene,
                                                          Engine::GameObject* selectedEntity);

private:
    GroundPaintMode m_PaintMode = GroundPaintMode::TerrainSplat;
    Engine::GameObject* m_LastObservedSelection = nullptr;

    uint32_t m_SelectedSplatLayer = 1; // Default to Dirt Path so first stroke is immediately visible on Grass
    float m_BrushRadius = 1.25f;
    float m_BrushFalloff = 0.65f;
    float m_BrushStrength = 0.85f;

    uint32_t m_SelectedTileID = 1;
    bool m_SelectedTileWalkable = true;
};

} // namespace Sandbox

#endif // SANDBOX_TILEPALETTEPANEL_H
