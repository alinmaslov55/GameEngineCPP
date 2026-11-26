#include "Panels/Inspector/InspectorPanel.h"

#include <Engine/Animation/Animator.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Components/CameraComponent.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/Components/PostProcessComponent.h>
#include <Engine/Terrain/TerrainChunkManager.h>
#include <Engine/Terrain/TerrainComponent.h>
#include <Engine/Terrain/Tilemap.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace Sandbox {

namespace {

template<typename T, typename UIFunction>
void DrawComponent(const char* label, Engine::GameObject* entity, UIFunction uiFunction) {
    if (auto* component = entity->GetComponent<T>()) {
        bool removeComp = false;
        ImGui::PushID(label);
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (ImGui::BeginPopupContextItem("ComponentSettings")) {
            if (ImGui::MenuItem("Remove Component")) {
                removeComp = true;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
        if (ImGui::SmallButton("X")) {
            removeComp = true;
        }

        if (open && !removeComp) {
            uiFunction(*component);
        }

        if (removeComp) {
            entity->RemoveComponent<T>();
            ENGINE_INFO("Inspector: Removed {} from '{}'", label, entity->GetName());
        }
        ImGui::PopID();
    }
}

template<typename T, typename UIFunction>
void DrawComponents(const char* label, Engine::GameObject* entity, UIFunction uiFunction) {
    const auto components = entity->GetComponents<T>();
    for (size_t i = 0; i < components.size(); ++i) {
        T* component = components[i];
        if (!component) continue;

        bool removeComp = false;
        ImGui::PushID(component);
        const std::string headerLabel = (components.size() > 1)
            ? (std::string(label) + " #" + std::to_string(i + 1))
            : std::string(label);
        const bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (ImGui::BeginPopupContextItem("ComponentSettings")) {
            if (ImGui::MenuItem("Remove Component")) {
                removeComp = true;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
        if (ImGui::SmallButton("X")) {
            removeComp = true;
        }

        if (open && !removeComp) {
            uiFunction(*component);
        }

        if (removeComp) {
            entity->RemoveComponent<T>();
            ENGINE_INFO("Inspector: Removed {} from '{}'", headerLabel, entity->GetName());
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}

void MarkHierarchyPhysicsDirty(Engine::Scene* scene, Engine::GameObject* go) {
    if (!scene || !go) return;
    scene->MarkPhysicsDirty(go);
    if (auto* t = go->GetTransform()) {
        for (Engine::Transform* child : t->GetChildren()) {
            if (child) {
                MarkHierarchyPhysicsDirty(scene, child->GetGameObject());
            }
        }
    }
}

template<typename T>
void DrawAddComponentEntry(const char* label, Engine::GameObject* entity) {
    if (ImGui::MenuItem(label)) {
        if (!entity->GetComponent<T>()) {
            entity->AddComponent<T>();
            ENGINE_INFO("Inspector: Added {} to '{}'", label, entity->GetName());
        } else {
            ENGINE_WARN("Inspector: '{}' already has {}", entity->GetName(), label);
        }
        ImGui::CloseCurrentPopup();
    }
}

} // namespace

void InspectorPanel::OnImGuiRender(Engine::GameObject* selectedEntity, bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Inspector", pOpen);

    if (selectedEntity) {
        DrawEntityHeader(selectedEntity);

        if (auto* transform = selectedEntity->GetTransform()) {
            DrawTransform(transform);
        }

        DrawComponent<Engine::SpriteRenderer>("Sprite Renderer", selectedEntity, [&](Engine::SpriteRenderer& c) {
            DrawSpriteRenderer(c, selectedEntity);
        });
        DrawComponent<Engine::ParticleSystem>("Particle System", selectedEntity, [&](Engine::ParticleSystem& c) {
            DrawParticleSystem(c, selectedEntity);
        });
        DrawComponent<Engine::DirectionalLightComponent>("Directional Light", selectedEntity, [&](Engine::DirectionalLightComponent& c) {
            DrawDirectionalLight(c, selectedEntity);
        });
        DrawComponent<Engine::PointLightComponent>("Point Light", selectedEntity, [&](Engine::PointLightComponent& c) {
            DrawPointLight(c, selectedEntity);
        });
        DrawComponent<Engine::PostProcessComponent>("Post Process Volume", selectedEntity, [&](Engine::PostProcessComponent& c) {
            DrawPostProcess(c, selectedEntity);
        });
        DrawComponent<Engine::WindComponent>("Wind Zone", selectedEntity, [&](Engine::WindComponent& c) {
            DrawWindZone(c, selectedEntity);
        });
        DrawComponent<Engine::SquashStretchComponent>("Squash & Stretch", selectedEntity, [&](Engine::SquashStretchComponent& c) {
            DrawSquashStretch(c, selectedEntity);
        });
        DrawComponent<Engine::Rigidbody2D>("Rigidbody 2D", selectedEntity, [&](Engine::Rigidbody2D& c) {
            DrawRigidbody2D(c, selectedEntity);
        });
        DrawComponents<Engine::BoxCollider2D>("Box Collider 2D", selectedEntity, [&](Engine::BoxCollider2D& c) {
            DrawBoxCollider2D(c, selectedEntity);
        });
        DrawComponents<Engine::CircleCollider2D>("Circle Collider 2D", selectedEntity, [&](Engine::CircleCollider2D& c) {
            DrawCircleCollider2D(c, selectedEntity);
        });
        DrawComponents<Engine::CapsuleCollider2D>("Capsule Collider 2D", selectedEntity, [&](Engine::CapsuleCollider2D& c) {
            DrawCapsuleCollider2D(c, selectedEntity);
        });
        DrawComponent<Engine::CameraComponent>("Camera", selectedEntity, [&](Engine::CameraComponent& c) {
            DrawCamera(c, selectedEntity);
        });
        DrawComponent<Engine::TerrainComponent>("Terrain (Splatmap)", selectedEntity, [&](Engine::TerrainComponent& c) {
            DrawTerrain(c, selectedEntity);
        });
        DrawComponent<Engine::TerrainChunkManager>("Terrain Chunk Manager (Infinite)", selectedEntity, [&](Engine::TerrainChunkManager& c) {
            DrawTerrainChunkManager(c, selectedEntity);
        });
        DrawComponent<Engine::Tilemap>("Tilemap", selectedEntity, [&](Engine::Tilemap& c) {
            DrawTilemap(c, selectedEntity);
        });
        DrawComponent<Engine::Animator>("Animator", selectedEntity, [&](Engine::Animator& c) {
            DrawAnimator(c, selectedEntity);
        });

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawAddComponentMenu(selectedEntity);
    } else {
        ImGui::TextDisabled("Select an entity to view its properties.");
    }

    ImGui::End();
}

void InspectorPanel::DrawEntityHeader(Engine::GameObject* entity) {
    char nameBuffer[256];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", entity->GetName().c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        if (nameBuffer[0] != '\0') {
            entity->SetName(nameBuffer);
        }
    }

    bool active = entity->IsActive();
    if (ImGui::Checkbox("Active", &active)) {
        entity->SetActive(active);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(entity->GetUUID()));

    static const char* kTagLabels[] = {
        "Untagged", "Player", "Enemy", "Companion", "Foliage",
        "HarvestableTree", "DestructibleProp", "TacticalStructure",
        "SafeZoneBrazier", "Shrine", "ResourcePickup", "Projectile"
    };
    int currentTagIdx = std::clamp(static_cast<int>(entity->GetTag()), 0, IM_ARRAYSIZE(kTagLabels) - 1);
    if (ImGui::Combo("Tag", &currentTagIdx, kTagLabels, IM_ARRAYSIZE(kTagLabels))) {
        entity->SetTag(static_cast<Engine::EntityTag>(currentTagIdx));
    }

    ImGui::Separator();
}

void InspectorPanel::DrawTransform(Engine::Transform* transform) {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool transformChanged = false;
        if (auto* parent = transform->GetParent()) {
            std::string parentName = "Unknown";
            if (auto* parentGo = parent->GetGameObject()) {
                parentName = parentGo->GetName();
            }
            ImGui::Text("Parent: %s", parentName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) {
                transform->SetParent(nullptr, /*keepWorldTransform=*/true);
            }
            transformChanged |= ImGui::DragFloat3("Position (Local)", &transform->position.x, 0.1f);
            transformChanged |= ImGui::DragFloat3("Rotation (Local)", &transform->rotation.x, 1.0f);
            transformChanged |= ImGui::DragFloat3("Scale (Local)", &transform->scale.x, 0.1f);
            const glm::vec3 worldPos = transform->GetWorldPosition();
            ImGui::TextDisabled("World Pos: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);
        } else {
            transformChanged |= ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
            transformChanged |= ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
            transformChanged |= ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
        }

        if (transformChanged) {
            transform->SetDirty();
            if (auto* go = transform->GetGameObject()) {
                if (auto* scene = go->GetScene()) {
                    MarkHierarchyPhysicsDirty(scene, go);
                }
            }
        }
    }
}


void InspectorPanel::DrawAddComponentMenu(Engine::GameObject* entity) {
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        DrawAddComponentEntry<Engine::SpriteRenderer>("Sprite Renderer", entity);
        DrawAddComponentEntry<Engine::Animator>("Animator", entity);
        DrawAddComponentEntry<Engine::TerrainComponent>("Terrain (Splatmap)", entity);
        DrawAddComponentEntry<Engine::TerrainChunkManager>("Terrain Chunk Manager (Infinite)", entity);
        DrawAddComponentEntry<Engine::DirectionalLightComponent>("Directional Light", entity);
        DrawAddComponentEntry<Engine::PointLightComponent>("Point Light", entity);
        DrawAddComponentEntry<Engine::PostProcessComponent>("Post Process Volume", entity);
        DrawAddComponentEntry<Engine::WindComponent>("Wind Zone", entity);
        DrawAddComponentEntry<Engine::SquashStretchComponent>("Squash & Stretch", entity);
        DrawAddComponentEntry<Engine::CameraComponent>("Camera", entity);
        DrawAddComponentEntry<Engine::ParticleSystem>("Particle System", entity);
        DrawAddComponentEntry<Engine::Rigidbody2D>("Rigidbody 2D", entity);
        DrawAddComponentEntry<Engine::BoxCollider2D>("Box Collider 2D", entity);
        DrawAddComponentEntry<Engine::CircleCollider2D>("Circle Collider 2D", entity);
        DrawAddComponentEntry<Engine::CapsuleCollider2D>("Capsule Collider 2D", entity);
        ImGui::EndPopup();
    }
}

} // namespace Sandbox
