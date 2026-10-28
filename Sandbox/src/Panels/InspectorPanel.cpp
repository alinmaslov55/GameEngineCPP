#include "Panels/InspectorPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/CameraComponent.h>
#include <Engine/Scene/TerrainComponent.h>
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

        // 8. Terrain Component (4-Layer Height-Modulated Splatmap)
        DrawComponent<Engine::TerrainComponent>("Terrain (Splatmap)", selectedEntity, [](Engine::TerrainComponent& terrain) {
            float dims[2] = { terrain.GetWidth(), terrain.GetDepth() };
            if (ImGui::DragFloat2("World Size (X, Z)", dims, 0.25f, 1.0f, 500.0f, "%.1f m")) {
                terrain.SetWidth(dims[0]);
                terrain.SetDepth(dims[1]);
            }

            ImGui::Text("Splatmap Resolution: %u x %u", terrain.GetSplatWidth(), terrain.GetSplatHeight());
            if (ImGui::SmallButton("64x64")) {
                terrain.ResizeSplatmap(64, 64);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("128x128")) {
                terrain.ResizeSplatmap(128, 128);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("256x256")) {
                terrain.ResizeSplatmap(256, 256);
            }

            float sharpness = terrain.GetBlendSharpness();
            if (ImGui::SliderFloat("Blend Sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) {
                terrain.SetBlendSharpness(sharpness);
            }

            float darkening = terrain.GetEdgeDarkening();
            if (ImGui::SliderFloat("Edge Ink Border", &darkening, 0.0f, 0.45f, "%.2f")) {
                terrain.SetEdgeDarkening(darkening);
            }

            ImGui::Separator();
            const char* layerLabels[Engine::TerrainComponent::MaxLayers] = {
                "Layer 0 (R - Grass)",
                "Layer 1 (G - Dirt)",
                "Layer 2 (B - Cobble)",
                "Layer 3 (A - Soil)"
            };
            for (uint32_t i = 0; i < Engine::TerrainComponent::MaxLayers; ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s", layerLabels[i]);

                auto& layer = terrain.GetLayer(i);
                std::string texName = "Default Color";
                if (layer.LayerTexture && !layer.LayerTexture->GetFilepath().empty()) {
                    texName = std::filesystem::path(layer.LayerTexture->GetFilepath()).filename().string();
                }

                ImGui::Button(texName.c_str(), ImVec2(145.0f, 0.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const std::string filepath(static_cast<const char*>(payload->Data));
                        if (filepath.find(".png") != std::string::npos || filepath.find(".jpg") != std::string::npos) {
                            terrain.SetLayerTexture(i, Engine::ResourceManager::Load<Engine::Texture>(filepath));
                            ENGINE_INFO("Inspector: Assigned terrain layer {} texture '{}'", i, filepath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(85.0f);
                float tiling = layer.TilingScale;
                if (ImGui::DragFloat("Tiling", &tiling, 0.01f, 0.05f, 10.0f, "%.2f")) {
                    terrain.SetLayerTilingScale(i, tiling);
                }
                ImGui::PopID();
            }
        });

        // 8b. Tilemap Component
        DrawComponent<Engine::Tilemap>("Tilemap", selectedEntity, [](Engine::Tilemap& tilemapComp) {
            ImGui::Text("Dimensions: %u x %u (%u tiles)",
                        tilemapComp.GetWidth(),
                        tilemapComp.GetHeight(),
                        tilemapComp.GetWidth() * tilemapComp.GetHeight());
            ImGui::TextDisabled("Switch Tile Palette to 'Tile Grid' to paint discrete tiles.");
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
            DrawAddComponentEntry<Engine::TerrainComponent>("Terrain (Splatmap)", selectedEntity);
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

