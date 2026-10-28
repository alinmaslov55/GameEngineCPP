#include "Panels/SceneViewportPanel.h"

#include <Engine/Renderer/TerrainRenderer.h>
#include <Engine/Scene/TerrainComponent.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Scene/Transform.h>

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Sandbox {

void SceneViewportPanel::OnImGuiRender(const std::shared_ptr<Engine::Framebuffer>& framebuffer,
                                       const std::shared_ptr<Engine::Scene>& activeScene,
                                       Engine::EditorCamera& editorCamera,
                                       bool isEditMode,
                                       Engine::GameObject* selectedEntity,
                                       TilePalettePanel& palettePanel) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Scene");

    const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    if (framebuffer) {
        const uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(textureID)),
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
                     ImVec2{ 0.0f, 1.0f },
                     ImVec2{ 1.0f, 0.0f });
    }
    const ImVec2 imageMin = ImGui::GetItemRectMin();

    const bool viewportHovered = ImGui::IsWindowHovered();
    ImGuiIO& io = ImGui::GetIO();

    // 1. 3D Diorama Editor Camera Pan, Zoom, Orbit & Brush Radius Shortcuts (Edit Mode)
    if (isEditMode && viewportHovered) {
        if (io.MouseWheel != 0.0f) {
            editorCamera.OnMouseScroll(io.MouseWheel);
        }

        if (io.KeyAlt && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                          ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            editorCamera.Orbit(-io.MouseDelta.x * 0.35f, io.MouseDelta.y * 0.25f);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
            editorCamera.Pan({ io.MouseDelta.x, io.MouseDelta.y }, m_ViewportSize);
        }

        const float orbitSpeed = 65.0f * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            editorCamera.Orbit(-orbitSpeed, 0.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            editorCamera.Orbit(orbitSpeed, 0.0f);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
            palettePanel.SetBrushRadius(palettePanel.GetBrushRadius() - 0.20f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
            palettePanel.SetBrushRadius(palettePanel.GetBrushRadius() + 0.20f);
        }
    }

    // 2. 3D Ground Raycasting for Splatmap Brush Preview + Painting & Tile Grid Painting
    bool showSplatRing = false;
    if (activeScene && viewportHovered && !io.KeyAlt &&
        m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 relativeMouse = {
            mousePos.x - imageMin.x,
            mousePos.y - imageMin.y
        };
        relativeMouse.y = m_ViewportSize.y - relativeMouse.y;

        if (relativeMouse.x >= 0.0f && relativeMouse.x <= m_ViewportSize.x &&
            relativeMouse.y >= 0.0f && relativeMouse.y <= m_ViewportSize.y) {
            const glm::vec2 ndc = {
                (relativeMouse.x / m_ViewportSize.x) * 2.0f - 1.0f,
                (relativeMouse.y / m_ViewportSize.y) * 2.0f - 1.0f
            };

            const Engine::PerspectiveCamera& viewCamera = isEditMode
                ? editorCamera.GetCamera()
                : activeScene->GetActiveCamera();

            if (isEditMode && palettePanel.GetPaintMode() == GroundPaintMode::TerrainSplat) {
                if (Engine::GameObject* terrainEntity = TilePalettePanel::ResolveTargetTerrainEntity(activeScene, selectedEntity)) {
                    auto* terrain = terrainEntity->GetComponent<Engine::TerrainComponent>();
                    auto* trTransform = terrainEntity->GetTransform();
                    const glm::vec3 origin = trTransform ? trTransform->position : glm::vec3(0.0f);
                    const glm::vec3 scale  = trTransform ? trTransform->scale    : glm::vec3(1.0f);

                    glm::vec3 hitWorldPos{ 0.0f };
                    if (terrain && viewCamera.RaycastGroundPlane(ndc, origin.y, hitWorldPos)) {
                        const glm::vec2 hitXZ = { hitWorldPos.x, hitWorldPos.z };
                        Engine::TerrainRenderer::SetBrushPreview(
                            true,
                            hitXZ,
                            palettePanel.GetBrushRadius(),
                            palettePanel.GetBrushFalloff()
                        );
                        showSplatRing = true;

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            terrain->PaintWorldBrush(
                                hitXZ.x,
                                hitXZ.y,
                                origin,
                                scale,
                                palettePanel.GetSelectedSplatLayer(),
                                palettePanel.GetBrushRadius(),
                                palettePanel.GetBrushFalloff(),
                                palettePanel.GetBrushStrength()
                            );
                            m_HasActiveSplatStroke = true;
                            m_LastSplatStrokeXZ = hitXZ;
                        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_HasActiveSplatStroke) {
                            const float stepThreshold = std::max(0.08f, palettePanel.GetBrushRadius() * 0.18f);
                            if (glm::length(hitXZ - m_LastSplatStrokeXZ) >= stepThreshold) {
                                terrain->PaintWorldSegment(
                                    m_LastSplatStrokeXZ,
                                    hitXZ,
                                    origin,
                                    scale,
                                    palettePanel.GetSelectedSplatLayer(),
                                    palettePanel.GetBrushRadius(),
                                    palettePanel.GetBrushFalloff(),
                                    palettePanel.GetBrushStrength() * 0.80f
                                );
                                m_LastSplatStrokeXZ = hitXZ;
                            }
                        }
                    }
                }
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && selectedEntity) {
                if (auto* tilemap = selectedEntity->GetComponent<Engine::Tilemap>()) {
                    const glm::vec3 mapOrigin = selectedEntity->GetTransform()
                        ? selectedEntity->GetTransform()->position
                        : glm::vec3(0.0f);
                    const glm::vec3 mapScale = selectedEntity->GetTransform()
                        ? selectedEntity->GetTransform()->scale
                        : glm::vec3(1.0f);

                    glm::vec3 hitWorldPos{ 0.0f };
                    if (viewCamera.RaycastGroundPlane(ndc, mapOrigin.y, hitWorldPos)) {
                        const float localX = (hitWorldPos.x - mapOrigin.x) / (mapScale.x != 0.0f ? mapScale.x : 1.0f);
                        const float localZ = (hitWorldPos.z - mapOrigin.z) / (mapScale.z != 0.0f ? mapScale.z : 1.0f);

                        const int gridX = static_cast<int>(std::round(localX));
                        const int gridZ = static_cast<int>(std::round(localZ));

                        if (gridX >= 0 && gridX < static_cast<int>(tilemap->GetWidth()) &&
                            gridZ >= 0 && gridZ < static_cast<int>(tilemap->GetHeight())) {
                            tilemap->SetTile(static_cast<uint32_t>(gridX),
                                             static_cast<uint32_t>(gridZ),
                                             palettePanel.GetSelectedTileID(),
                                             palettePanel.IsSelectedTileWalkable());
                        }
                    }
                }
            }
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_HasActiveSplatStroke = false;
    }
    if (!showSplatRing) {
        Engine::TerrainRenderer::ClearBrushPreview();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Sandbox
