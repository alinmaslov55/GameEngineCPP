#include "Panels/TilePalettePanel.h"

#include <Engine/Utils/Logger.h>
#include <imgui.h>

namespace Sandbox {

void TilePalettePanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Tile Palette", pOpen);
    ImGui::Text("Select a ground brush:");
    ImGui::Separator();

    if (ImGui::Button("Eraser (ID 0)", ImVec2(160, 28))) {
        m_SelectedTileID = 0;
        m_SelectedTileWalkable = false;
        ENGINE_DEBUG("Tile Palette: Selected Eraser (ID 0, Non-walkable)");
    }
    if (ImGui::Button("Meadow Grass (ID 1)", ImVec2(160, 28))) {
        m_SelectedTileID = 1;
        m_SelectedTileWalkable = true;
        ENGINE_DEBUG("Tile Palette: Selected Meadow Grass (ID 1, Walkable)");
    }
    if (ImGui::Button("Cobblestone (ID 2)", ImVec2(160, 28))) {
        m_SelectedTileID = 2;
        m_SelectedTileWalkable = true;
        ENGINE_DEBUG("Tile Palette: Selected Cobblestone (ID 2, Walkable)");
    }
    if (ImGui::Button("Tilled Soil (ID 3)", ImVec2(160, 28))) {
        m_SelectedTileID = 3;
        m_SelectedTileWalkable = true;
        ENGINE_DEBUG("Tile Palette: Selected Tilled Soil (ID 3, Walkable)");
    }
    if (ImGui::Button("Dirt Path (ID 4)", ImVec2(160, 28))) {
        m_SelectedTileID = 4;
        m_SelectedTileWalkable = true;
        ENGINE_DEBUG("Tile Palette: Selected Dirt Path (ID 4, Walkable)");
    }

    ImGui::Spacing();
    ImGui::Checkbox("Walkable Tile", &m_SelectedTileWalkable);
    ImGui::TextDisabled("Current Brush: ID %u (%s)",
                        m_SelectedTileID,
                        m_SelectedTileWalkable ? "Walkable" : "Blocked");
    ImGui::End();
}

} // namespace Sandbox
