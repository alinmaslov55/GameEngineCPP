#include "Panels/TilePalettePanel.h"

#include <Engine/Utils/Logger.h>
#include <imgui.h>

namespace Sandbox {

void TilePalettePanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Tile Palette", pOpen);
    ImGui::Text("Select a brush:");
    ImGui::Separator();

    if (ImGui::Button("Eraser (ID 0)", ImVec2(140, 30))) {
        m_SelectedTileID = 0;
        m_SelectedTileWalkable = false;
        ENGINE_DEBUG("Tile Palette: Selected Eraser (ID 0, Non-walkable)");
    }
    if (ImGui::Button("Grass (ID 1)", ImVec2(140, 30))) {
        m_SelectedTileID = 1;
        m_SelectedTileWalkable = true;
        ENGINE_DEBUG("Tile Palette: Selected Grass (ID 1, Walkable)");
    }
    if (ImGui::Button("Stone Wall (ID 2)", ImVec2(140, 30))) {
        m_SelectedTileID = 2;
        m_SelectedTileWalkable = false;
        ENGINE_DEBUG("Tile Palette: Selected Stone Wall (ID 2, Non-walkable)");
    }

    ImGui::Spacing();
    ImGui::Checkbox("Walkable Tile", &m_SelectedTileWalkable);
    ImGui::TextDisabled("Current Brush: ID %u (%s)",
                        m_SelectedTileID,
                        m_SelectedTileWalkable ? "Walkable" : "Blocked");
    ImGui::End();
}

} // namespace Sandbox

