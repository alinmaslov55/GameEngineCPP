#include "Panels/SceneHierarchyPanel.h"

#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>
#include <imgui.h>
#include <string>
#include <vector>

namespace Sandbox {

namespace {

void DrawEntityNode(Engine::GameObject* go,
                    Engine::GameObject*& selectedEntity,
                    Engine::GameObject*& entityToDelete) {
    if (!go) return;

    ImGuiTreeNodeFlags flags = ((selectedEntity == go) ? ImGuiTreeNodeFlags_Selected : 0)
                             | ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;

    const auto* transform = go->GetTransform();
    const bool hasChildren = transform && !transform->GetChildren().empty();
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const void* nodeId = reinterpret_cast<const void*>(static_cast<uintptr_t>(go->GetUUID()));
    const bool opened = ImGui::TreeNodeEx(nodeId, flags, "%s", go->GetName().c_str());

    if (ImGui::IsItemClicked()) {
        selectedEntity = go;
    }

    // Context menu on node
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete GameObject")) {
            entityToDelete = go;
        }
        ImGui::EndPopup();
    }

    // Drag-and-drop source (drag this entity to reparent)
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &go, sizeof(Engine::GameObject*));
        ImGui::Text("%s", go->GetName().c_str());
        ImGui::EndDragDropSource();
    }

    // Drag-and-drop target (drop entity onto this entity to make it a child)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
            auto* droppedGo = *static_cast<Engine::GameObject**>(payload->Data);
            if (droppedGo && droppedGo != go) {
                if (auto* droppedTransform = droppedGo->GetTransform()) {
                    droppedTransform->SetParent(go->GetTransform(), /*keepWorldTransform=*/true);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (opened) {
        if (transform) {
            // Copy children pointers to avoid iterator invalidation if child array changes during iteration
            const auto childrenCopy = transform->GetChildren();
            for (auto* childTransform : childrenCopy) {
                if (childTransform && childTransform->GetGameObject()) {
                    DrawEntityNode(childTransform->GetGameObject(), selectedEntity, entityToDelete);
                }
            }
        }
        ImGui::TreePop();
    }
}

} // namespace

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
            if (!go) continue;
            // Only draw root entities in the top-level loop; children are drawn recursively
            if (const auto* tr = go->GetTransform(); tr && tr->GetParent() != nullptr) {
                continue;
            }
            DrawEntityNode(go, selectedEntity, entityToDelete);
        }

        // Dropping into remaining space unparents dropped entity to root
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 0.0f) {
            ImGui::Dummy(avail);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                    auto* droppedGo = *static_cast<Engine::GameObject**>(payload->Data);
                    if (droppedGo && droppedGo->GetTransform()) {
                        droppedGo->GetTransform()->SetParent(nullptr, /*keepWorldTransform=*/true);
                    }
                }
                ImGui::EndDragDropTarget();
            }
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
            if (selectedEntity == entityToDelete ||
                (selectedEntity && selectedEntity->GetTransform() &&
                 entityToDelete->GetTransform() &&
                 selectedEntity->GetTransform()->IsDescendantOf(entityToDelete->GetTransform()))) {
                selectedEntity = nullptr;
            }
            activeScene->DestroyGameObject(entityToDelete);
            ENGINE_INFO("Hierarchy: Deleted GameObject '{}'", deletedName);
        }
    }
    ImGui::End();
}

} // namespace Sandbox

