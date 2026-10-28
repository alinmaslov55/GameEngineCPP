#include "Panels/TilePalettePanel.h"

#include <Engine/Scene/Tilemap.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <cstdint>

namespace Sandbox {

void TilePalettePanel::SetBrushRadius(float radius) {
    m_BrushRadius = std::clamp(radius, 0.20f, 6.0f);
}

Engine::GameObject* TilePalettePanel::ResolveTargetTerrainEntity(const std::shared_ptr<Engine::Scene>& activeScene,
                                                                 Engine::GameObject* selectedEntity) {
    if (selectedEntity && selectedEntity->GetComponent<Engine::TerrainComponent>()) {
        return selectedEntity;
    }
    if (!activeScene) {
        return nullptr;
    }
    for (const auto& go : activeScene->GetGameObjects()) {
        if (go->GetComponent<Engine::TerrainComponent>()) {
            return go.get();
        }
    }
    return nullptr;
}

void TilePalettePanel::OnImGuiRender(const std::shared_ptr<Engine::Scene>& activeScene,
                                     Engine::GameObject* selectedEntity,
                                     bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    // Automatically switch mode when the user explicitly selects a Terrain vs Tilemap entity
    if (selectedEntity != m_LastObservedSelection) {
        m_LastObservedSelection = selectedEntity;
        if (selectedEntity) {
            if (selectedEntity->GetComponent<Engine::TerrainComponent>()) {
                m_PaintMode = GroundPaintMode::TerrainSplat;
            } else if (selectedEntity->GetComponent<Engine::Tilemap>()) {
                m_PaintMode = GroundPaintMode::TileGrid;
            }
        }
    }

    ImGui::Begin("Tile Palette", pOpen);

    // Mode Toggle Header
    const bool isSplat = (m_PaintMode == GroundPaintMode::TerrainSplat);
    if (isSplat) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.52f, 0.34f, 1.0f));
    }
    if (ImGui::Button("Splat Brush", ImVec2(108.0f, 24.0f))) {
        m_PaintMode = GroundPaintMode::TerrainSplat;
    }
    if (isSplat) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (!isSplat) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.44f, 0.64f, 1.0f));
    }
    if (ImGui::Button("Tile Grid", ImVec2(108.0f, 24.0f))) {
        m_PaintMode = GroundPaintMode::TileGrid;
    }
    if (!isSplat) {
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (m_PaintMode == GroundPaintMode::TerrainSplat) {
        Engine::GameObject* terrainEntity = ResolveTargetTerrainEntity(activeScene, selectedEntity);
        Engine::TerrainComponent* terrain = terrainEntity
            ? terrainEntity->GetComponent<Engine::TerrainComponent>()
            : nullptr;

        if (!terrain) {
            ImGui::TextDisabled("No TerrainComponent in scene.");
        } else {
            ImGui::TextDisabled("Target: %s (%ux%u splat)",
                                terrainEntity->GetName().c_str(),
                                terrain->GetSplatWidth(),
                                terrain->GetSplatHeight());

            const char* layerNames[Engine::TerrainComponent::MaxLayers] = {
                "0: Meadow Grass (R)",
                "1: Dirt Path (G)",
                "2: Mossy Cobble (B)",
                "3: Tilled Soil (A)"
            };

            for (uint32_t i = 0; i < Engine::TerrainComponent::MaxLayers; ++i) {
                ImGui::PushID(static_cast<int>(i));
                const bool selected = (m_SelectedSplatLayer == i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.58f, 0.36f, 1.0f));
                }

                const auto& layerTex = terrain->GetLayer(i).LayerTexture;
                if (layerTex && layerTex->GetRendererID() != 0) {
                    ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(layerTex->GetRendererID())),
                                 ImVec2(24.0f, 24.0f),
                                 ImVec2(0.0f, 1.0f),
                                 ImVec2(1.0f, 0.0f));
                    ImGui::SameLine();
                }

                if (ImGui::Button(layerNames[i], ImVec2(175.0f, 26.0f))) {
                    m_SelectedSplatLayer = i;
                    ENGINE_DEBUG("Terrain Brush: Selected Layer {} ({})", i, layerNames[i]);
                }

                if (selected) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::SliderFloat("Radius", &m_BrushRadius, 0.25f, 5.0f, "%.2f m");
            ImGui::SliderFloat("Falloff", &m_BrushFalloff, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Strength", &m_BrushStrength, 0.05f, 1.0f, "%.2f");

            if (ImGui::Button("Fill All With Selected Layer", ImVec2(210.0f, 24.0f))) {
                terrain->FillLayer(m_SelectedSplatLayer);
                ENGINE_INFO("Terrain Brush: Filled '{}' with layer {}", terrainEntity->GetName(), m_SelectedSplatLayer);
            }
            ImGui::TextDisabled("Tip: [ / ] keys resize brush in Scene");
        }
    } else {
        ImGui::Text("Select a discrete tile brush:");
        if (ImGui::Button("Eraser / Invisible (ID 0)", ImVec2(195.0f, 26.0f))) {
            m_SelectedTileID = 0;
            m_SelectedTileWalkable = false;
            ENGINE_DEBUG("Tile Palette: Selected Eraser (ID 0, Non-walkable)");
        }
        if (ImGui::Button("Meadow Grass (ID 1)", ImVec2(195.0f, 26.0f))) {
            m_SelectedTileID = 1;
            m_SelectedTileWalkable = true;
        }
        if (ImGui::Button("Cobblestone (ID 2)", ImVec2(195.0f, 26.0f))) {
            m_SelectedTileID = 2;
            m_SelectedTileWalkable = true;
        }
        if (ImGui::Button("Tilled Soil (ID 3)", ImVec2(195.0f, 26.0f))) {
            m_SelectedTileID = 3;
            m_SelectedTileWalkable = true;
        }
        if (ImGui::Button("Dirt Path (ID 4)", ImVec2(195.0f, 26.0f))) {
            m_SelectedTileID = 4;
            m_SelectedTileWalkable = true;
        }

        ImGui::Spacing();
        ImGui::Checkbox("Walkable Tile", &m_SelectedTileWalkable);
        ImGui::TextDisabled("Current Brush: ID %u (%s)",
                            m_SelectedTileID,
                            m_SelectedTileWalkable ? "Walkable" : "Blocked");
    }

    ImGui::End();
}

} // namespace Sandbox
