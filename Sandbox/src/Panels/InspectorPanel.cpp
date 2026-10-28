#include "Panels/InspectorPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/CameraComponent.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <cstdio>
#include <filesystem>
#include <string>

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
        // 1. Editable Entity Name wired to Object::SetName
        char nameBuffer[256];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", selectedEntity->GetName().c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
            if (nameBuffer[0] != '\0') {
                selectedEntity->SetName(nameBuffer);
            }
        }
        ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(selectedEntity->GetUUID()));
        ImGui::Separator();

        // 2. Transform Component (cached O(1) pointer, mandatory)
        if (auto* transform = selectedEntity->GetTransform()) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
                ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
            }
        }

        // 3. Sprite Renderer Component (with 2.5D Billboard Mode, Alpha Cutout & Drag-and-Drop texture assignment)
        DrawComponent<Engine::SpriteRenderer>("Sprite Renderer", selectedEntity, [&](Engine::SpriteRenderer& spriteRenderer) {
            glm::vec4 color = spriteRenderer.GetColor();
            if (ImGui::ColorEdit4("Color", &color.x)) {
                spriteRenderer.SetColor(color);
            }

            const char* billboardLabels[] = {
                "ViewPlane (2.5D Standee)",
                "Cylindrical Y (Vertical)",
                "Flat Ground (Decal)",
                "None (3D Transform)"
            };
            int modeIdx = static_cast<int>(spriteRenderer.GetBillboardMode());
            if (ImGui::Combo("Billboard Mode", &modeIdx, billboardLabels, 4)) {
                spriteRenderer.SetBillboardMode(static_cast<Engine::SpriteRenderer::BillboardMode>(modeIdx));
            }

            float cutoff = spriteRenderer.GetAlphaCutoff();
            if (ImGui::SliderFloat("Alpha Cutoff", &cutoff, 0.0f, 0.95f, "%.2f")) {
                spriteRenderer.SetAlphaCutoff(cutoff);
            }

            std::string texLabel = "None";
            auto currentTex = spriteRenderer.GetTexture();
            if (currentTex && !currentTex->GetFilepath().empty()) {
                texLabel = std::filesystem::path(currentTex->GetFilepath()).filename().string();
            } else if (currentTex) {
                texLabel = std::to_string(currentTex->GetRendererID());
            }

            ImGui::Button(texLabel.c_str(), ImVec2(140.0f, 0.0f));
            if (currentTex && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Path: %s\nRenderer ID: %u (%dx%d)",
                                  Engine::ResourceManager::ToRelativePath(currentTex->GetFilepath()).c_str(),
                                  currentTex->GetRendererID(),
                                  currentTex->GetWidth(),
                                  currentTex->GetHeight());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("<- Drop Texture Here");

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    const std::string filepath(path);

                    if (filepath.find(".png") != std::string::npos || filepath.find(".jpg") != std::string::npos) {
                        auto newTexture = Engine::ResourceManager::Load<Engine::Texture>(filepath);
                        spriteRenderer.SetTexture(newTexture);
                        ENGINE_INFO("Inspector: Assigned texture '{}' to '{}'", filepath, selectedEntity->GetName());
                    } else {
                        ENGINE_WARN("Invalid file type dropped on SpriteRenderer: '{}'", filepath);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        });

        // 4. Particle System Component
        DrawComponent<Engine::ParticleSystem>("Particle System", selectedEntity, [&](Engine::ParticleSystem& particleSystem) {
            ImGui::Text("Active Particles: %u / %zu",
                        particleSystem.GetActiveParticleCount(),
                        particleSystem.GetParticles().size());
            if (ImGui::Button("Emit Test Particles")) {
                Engine::ParticleProps props;
                if (auto* t = selectedEntity->GetTransform()) {
                    props.position = t->position;
                }
                props.velocity = { 0.0f, 1.0f, 0.0f };
                particleSystem.Emit(props);
                ENGINE_DEBUG("Inspector: Emitted test particle on '{}'", selectedEntity->GetName());
            }
        });

        // 5. Rigidbody 2D Component
        DrawComponent<Engine::Rigidbody2D>("Rigidbody 2D", selectedEntity, [](Engine::Rigidbody2D& rb2d) {
            const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
            const char* currentBodyTypeString = bodyTypeStrings[static_cast<int>(rb2d.Type)];

            if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                for (int i = 0; i < 3; ++i) {
                    const bool isSelected = (currentBodyTypeString == bodyTypeStrings[i]);
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                        rb2d.Type = static_cast<Engine::Rigidbody2D::BodyType>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Fixed Rotation", &rb2d.FixedRotation);
        });

        // 6. Box Collider 2D Component
        DrawComponent<Engine::BoxCollider2D>("Box Collider 2D", selectedEntity, [](Engine::BoxCollider2D& boxCollider) {
            ImGui::DragFloat2("Size", &boxCollider.size.x, 0.05f, 0.01f, 100.0f);
            ImGui::DragFloat2("Offset", &boxCollider.offset.x, 0.05f);
            ImGui::DragFloat("Density", &boxCollider.density, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Friction", &boxCollider.friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &boxCollider.restitution, 0.01f, 0.0f, 1.0f);
            ImGui::Separator();
            ImGui::Checkbox("Is Dynamic (Isometric)", &boxCollider.isDynamic);
            if (boxCollider.isDynamic) {
                ImGui::Checkbox("Use Gravity", &boxCollider.useGravity);
                ImGui::DragFloat("Gravity", &boxCollider.gravity, 0.1f, -50.0f, 0.0f);
                ImGui::DragFloat("Ground Y", &boxCollider.groundLevel, 0.05f);
            }
        });

        // 7. Camera Component (3D Diorama Perspective)
        DrawComponent<Engine::CameraComponent>("Camera", selectedEntity, [&](Engine::CameraComponent& camComp) {
            bool changed = false;
            changed |= ImGui::SliderFloat("FOV (deg)", &camComp.FOV, 15.0f, 90.0f, "%.1f");
            changed |= ImGui::DragFloat("Distance", &camComp.Distance, 0.2f, 2.0f, 80.0f);
            changed |= ImGui::SliderFloat("Pitch (deg)", &camComp.Pitch, 15.0f, 80.0f, "%.1f");
            changed |= ImGui::DragFloat("Yaw (deg)", &camComp.Yaw, 0.5f, -360.0f, 360.0f);
            ImGui::Checkbox("Fixed Aspect Ratio", &camComp.FixedAspectRatio);
            if (changed) {
                camComp.SyncFromTransform(selectedEntity->GetTransform());
            }
        });

        // 8. Tilemap Component
        DrawComponent<Engine::Tilemap>("Tilemap", selectedEntity, [](Engine::Tilemap& tilemapComp) {
            ImGui::Text("Dimensions: %u x %u (%u tiles)",
                        tilemapComp.GetWidth(),
                        tilemapComp.GetHeight(),
                        tilemapComp.GetWidth() * tilemapComp.GetHeight());
            ImGui::TextDisabled("Select a brush in Tile Palette and click in Scene to paint.");
        });

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 9. Add Component Popup
        if (ImGui::Button("Add Component")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            DrawAddComponentEntry<Engine::SpriteRenderer>("Sprite Renderer", selectedEntity);
            DrawAddComponentEntry<Engine::CameraComponent>("Camera", selectedEntity);
            DrawAddComponentEntry<Engine::ParticleSystem>("Particle System", selectedEntity);
            DrawAddComponentEntry<Engine::Rigidbody2D>("Rigidbody 2D", selectedEntity);
            DrawAddComponentEntry<Engine::BoxCollider2D>("Box Collider 2D", selectedEntity);
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled("Select an entity to view its properties.");
    }

    ImGui::End();
}

} // namespace Sandbox

