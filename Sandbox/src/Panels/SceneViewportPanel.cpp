#include "Panels/SceneViewportPanel.h"

#include <Engine/Math/Isometric.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Scene/Transform.h>

#include <imgui.h>
#include <cmath>
#include <cstdint>

namespace Sandbox {

void SceneViewportPanel::OnImGuiRender(const std::shared_ptr<Engine::Framebuffer>& framebuffer,
                                       const std::shared_ptr<Engine::Scene>& activeScene,
                                       Engine::EditorCamera& editorCamera,
                                       bool isEditMode,
                                       Engine::GameObject* selectedEntity,
                                       uint32_t selectedTileID,
                                       bool selectedTileWalkable) {
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

    // 1. Editor Camera Pan & Zoom (Edit Mode)
    if (isEditMode && viewportHovered) {
        if (io.MouseWheel != 0.0f) {
            editorCamera.OnMouseScroll(io.MouseWheel);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
            editorCamera.Pan({ io.MouseDelta.x, io.MouseDelta.y }, m_ViewportSize);
        }
    }

    // 2. Isometric Tile Painting via NDC -> Inverse ViewProjection Unprojection
    if (activeScene && viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 relativeMouse = {
            mousePos.x - imageMin.x,
            mousePos.y - imageMin.y
        };

        // Flip Y from ImGui top-left origin to OpenGL bottom-left origin
        relativeMouse.y = m_ViewportSize.y - relativeMouse.y;

        if (relativeMouse.x >= 0.0f && relativeMouse.x <= m_ViewportSize.x &&
            relativeMouse.y >= 0.0f && relativeMouse.y <= m_ViewportSize.y) {
            const glm::vec4 ndc = {
                (relativeMouse.x / m_ViewportSize.x) * 2.0f - 1.0f,
                (relativeMouse.y / m_ViewportSize.y) * 2.0f - 1.0f,
                0.0f,
                1.0f
            };

            const Engine::OrthographicCamera& viewCamera = isEditMode
                ? editorCamera.GetCamera()
                : activeScene->GetActiveCamera();
            const glm::vec4 isoScreenPos = glm::inverse(viewCamera.GetViewProjectionMatrix()) * ndc;

            if (selectedEntity) {
                if (auto* tilemap = selectedEntity->GetComponent<Engine::Tilemap>()) {
                    const glm::vec3 mapOffset = selectedEntity->GetTransform()
                        ? selectedEntity->GetTransform()->position
                        : glm::vec3(0.0f);

                    const glm::vec3 worldPos = Engine::Isometric::ScreenToWorld({
                        isoScreenPos.x,
                        isoScreenPos.y - mapOffset.y
                    }) - glm::vec3(mapOffset.x, 0.0f, mapOffset.z);

                    const int gridX = static_cast<int>(std::round(worldPos.x));
                    const int gridY = static_cast<int>(std::round(worldPos.z));

                    if (gridX >= 0 && gridX < static_cast<int>(tilemap->GetWidth()) &&
                        gridY >= 0 && gridY < static_cast<int>(tilemap->GetHeight())) {
                        tilemap->SetTile(static_cast<uint32_t>(gridX),
                                         static_cast<uint32_t>(gridY),
                                         selectedTileID,
                                         selectedTileWalkable);
                    }
                }
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Sandbox
