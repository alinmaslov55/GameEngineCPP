#include "Panels/SceneHierarchyPanel.h"

#include <Engine/Utils/Logger.h>
#include <imgui.h>
#include <string>

namespace Sandbox {

void SceneHierarchyPanel::OnImGuiRender(const std::shared_ptr<Engine::Scene>& activeScene,
                                         Engine::GameObject*& selectedEntity,
                                         bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Hierarchy", pOpen);
    if (activeScene) {
        Engine::GameObject* entityToDelete = nullptr;

        for (const auto& gameObjectPtr : activeScene->GetGameObjects()) {
            Engine::GameObject* go = gameObjectPtr.get();

            const std::string uuidStr = std::to_string(go->GetUUID());
            ImGui::PushID(uuidStr.c_str());

            const bool isSelected = (selectedEntity == go);
            if (ImGui::Selectable(go->GetName().c_str(), isSelected)) {
                selectedEntity = go;
                ENGINE_DEBUG("Hierarchy: Selected GameObject '{}' (UUID: {})", go->GetName(), go->GetUUID());
            }

            // Right-click on an entity row to delete it
            if (ImGui::BeginPopupContextItem("EntityContextMenu")) {
                if (ImGui::MenuItem("Delete GameObject")) {
                    entityToDelete = go;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        // Support Delete key when Hierarchy window is focused
        if (!entityToDelete && selectedEntity && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            entityToDelete = selectedEntity;
        }

        // Click in empty space to deselect
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            selectedEntity = nullptr;
        }

        // Right-click in empty space to create new GameObjects
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", 1 | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty GameObject")) {
                auto* newGo = activeScene->CreateGameObject("New GameObject");
                selectedEntity = newGo;
                ENGINE_INFO("Hierarchy: Created new GameObject '{}'", newGo->GetName());
            }
            ImGui::EndPopup();
        }

        // Perform deferred entity deletion safely outside iteration loop
        if (entityToDelete) {
            const std::string deletedName = entityToDelete->GetName();
            if (selectedEntity == entityToDelete) {
                selectedEntity = nullptr;
            }
            activeScene->DestroyGameObject(entityToDelete);
            ENGINE_INFO("Hierarchy: Deleted GameObject '{}'", deletedName);
        }
    }
    ImGui::End();
}

} // namespace Sandbox

