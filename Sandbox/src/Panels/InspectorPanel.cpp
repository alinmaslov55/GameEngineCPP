#include "Panels/InspectorPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/CameraComponent.h>
#include <Engine/Scene/FoliageWindComponents.h>
#include <Engine/Scene/LightComponents.h>
#include <Engine/Scene/PostProcessComponent.h>
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
        DrawComponent<Engine::BoxCollider2D>("Box Collider 2D", selectedEntity, [&](Engine::BoxCollider2D& c) {
            DrawBoxCollider2D(c, selectedEntity);
        });
        DrawComponent<Engine::CameraComponent>("Camera", selectedEntity, [&](Engine::CameraComponent& c) {
            DrawCamera(c, selectedEntity);
        });
        DrawComponent<Engine::TerrainComponent>("Terrain (Splatmap)", selectedEntity, [&](Engine::TerrainComponent& c) {
            DrawTerrain(c, selectedEntity);
        });
        DrawComponent<Engine::Tilemap>("Tilemap", selectedEntity, [&](Engine::Tilemap& c) {
            DrawTilemap(c, selectedEntity);
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
    ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(entity->GetUUID()));
    ImGui::Separator();
}

void InspectorPanel::DrawTransform(Engine::Transform* transform) {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
    }
}

void InspectorPanel::DrawSpriteRenderer(Engine::SpriteRenderer& spriteRenderer, Engine::GameObject* entity) {
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

    bool castShadows = spriteRenderer.GetCastShadows();
    if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
        spriteRenderer.SetCastShadows(castShadows);
    }
    ImGui::SameLine();
    bool receiveLighting = spriteRenderer.GetReceiveLighting();
    if (ImGui::Checkbox("Receive Lighting", &receiveLighting)) {
        spriteRenderer.SetReceiveLighting(receiveLighting);
    }

    if (receiveLighting) {
        float emissive = spriteRenderer.GetEmissive();
        if (ImGui::SliderFloat("Emissive Glow", &emissive, 0.0f, 3.0f, "%.2f")) {
            spriteRenderer.SetEmissive(emissive);
        }
    }

    if (spriteRenderer.GetBillboardMode() != Engine::SpriteRenderer::BillboardMode::FlatGround) {
        float windWeight = spriteRenderer.GetWindSwayWeight();
        if (ImGui::SliderFloat("Wind Sway Weight", &windWeight, 0.0f, 1.5f, "%.2f")) {
            spriteRenderer.SetWindSwayWeight(windWeight);
        }
        if (windWeight > 0.001f) {
            float phaseOffset = spriteRenderer.GetWindPhaseOffset();
            if (ImGui::SliderFloat("Wind Phase Offset", &phaseOffset, 0.0f, 6.283f, "%.2f rad")) {
                spriteRenderer.SetWindPhaseOffset(phaseOffset);
            }
        }
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
                ENGINE_INFO("Inspector: Assigned texture '{}' to '{}'", filepath, entity->GetName());
            } else {
                ENGINE_WARN("Invalid file type dropped on SpriteRenderer: '{}'", filepath);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void InspectorPanel::DrawParticleSystem(Engine::ParticleSystem& particleSystem, Engine::GameObject* entity) {
    ImGui::Text("Active Particles: %u / %zu",
                particleSystem.GetActiveParticleCount(),
                particleSystem.GetParticles().size());
    ImGui::Checkbox("Auto Emit", &particleSystem.AutoEmit);
    ImGui::SameLine();
    ImGui::Checkbox("Emissive Glow", &particleSystem.EmissiveParticles);
    if (particleSystem.AutoEmit) {
        ImGui::SliderFloat("Emit Rate (/s)", &particleSystem.EmitRate, 0.5f, 60.0f, "%.1f");
        ImGui::DragFloat3("Spawn Offset", &particleSystem.SpawnOffset.x, 0.05f);
    }
    if (ImGui::Button("Emit Test Particles")) {
        Engine::ParticleProps props = particleSystem.TemplateProps;
        if (auto* t = entity->GetTransform()) {
            props.position = t->position + particleSystem.SpawnOffset;
        }
        particleSystem.Emit(props);
        ENGINE_DEBUG("Inspector: Emitted test particle on '{}'", entity->GetName());
    }
}

void InspectorPanel::DrawDirectionalLight(Engine::DirectionalLightComponent& dirLight, Engine::GameObject* entity) {
    ImGui::TextDisabled("Time-of-Day Presets:");
    if (ImGui::SmallButton("Golden Afternoon")) {
        ENGINE_INFO("Inspector: Applied 'Golden Afternoon' lighting preset on '{}'", entity->GetName());
        dirLight.ElevationDegrees = 40.0f;
        dirLight.AzimuthDegrees = -42.0f;
        dirLight.SunColor = { 1.0f, 0.91f, 0.74f };
        dirLight.SunIntensity = 1.05f;
        dirLight.AmbientColor = { 0.36f, 0.41f, 0.62f };
        dirLight.AmbientIntensity = 0.62f;
        dirLight.ShadowSoftness = 1.35f;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Cult Dusk")) {
        ENGINE_INFO("Inspector: Applied 'Cult Dusk' lighting preset on '{}'", entity->GetName());
        dirLight.ElevationDegrees = 22.0f;
        dirLight.AzimuthDegrees = -64.0f;
        dirLight.SunColor = { 1.0f, 0.54f, 0.34f };
        dirLight.SunIntensity = 0.95f;
        dirLight.AmbientColor = { 0.34f, 0.24f, 0.52f };
        dirLight.AmbientIntensity = 0.52f;
        dirLight.ShadowSoftness = 2.15f;
    }
    if (ImGui::SmallButton("Midnight Ritual")) {
        ENGINE_INFO("Inspector: Applied 'Midnight Ritual' lighting preset on '{}'", entity->GetName());
        dirLight.ElevationDegrees = 52.0f;
        dirLight.AzimuthDegrees = 28.0f;
        dirLight.SunColor = { 0.38f, 0.48f, 0.86f };
        dirLight.SunIntensity = 0.28f;
        dirLight.AmbientColor = { 0.14f, 0.16f, 0.30f };
        dirLight.AmbientIntensity = 0.42f;
        dirLight.ShadowSoftness = 1.65f;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Noon")) {
        ENGINE_INFO("Inspector: Applied 'Clear Noon' lighting preset on '{}'", entity->GetName());
        dirLight.ElevationDegrees = 68.0f;
        dirLight.AzimuthDegrees = -18.0f;
        dirLight.SunColor = { 1.0f, 0.98f, 0.92f };
        dirLight.SunIntensity = 1.0f;
        dirLight.AmbientColor = { 0.52f, 0.58f, 0.72f };
        dirLight.AmbientIntensity = 0.68f;
        dirLight.ShadowSoftness = 0.95f;
    }

    ImGui::Separator();
    ImGui::SliderFloat("Elevation (deg)", &dirLight.ElevationDegrees, 10.0f, 85.0f, "%.1f");
    ImGui::SliderFloat("Azimuth (deg)", &dirLight.AzimuthDegrees, -180.0f, 180.0f, "%.1f");
    ImGui::ColorEdit3("Sun Color", &dirLight.SunColor.x);
    ImGui::SliderFloat("Sun Intensity", &dirLight.SunIntensity, 0.0f, 2.5f, "%.2f");

    ImGui::Separator();
    ImGui::ColorEdit3("Ambient Shade", &dirLight.AmbientColor.x);
    ImGui::SliderFloat("Ambient Intensity", &dirLight.AmbientIntensity, 0.0f, 1.5f, "%.2f");

    ImGui::Separator();
    ImGui::Checkbox("Cast Cutout Shadows", &dirLight.CastShadows);
    if (dirLight.CastShadows) {
        ImGui::SliderFloat("Shadow Softness", &dirLight.ShadowSoftness, 0.25f, 4.0f, "%.2f");
        ImGui::SliderFloat("Shadow Bias", &dirLight.ShadowBias, 0.0002f, 0.01f, "%.4f");
    }
}

void InspectorPanel::DrawPointLight(Engine::PointLightComponent& pointLight, Engine::GameObject* entity) {
    (void)entity;
    ImGui::ColorEdit3("Light Color", &pointLight.Color.x);
    ImGui::SliderFloat("Intensity", &pointLight.Intensity, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Radius (m)", &pointLight.Radius, 0.5f, 16.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &pointLight.Falloff, 0.3f, 4.0f, "%.2f");
    ImGui::DragFloat("Height Offset (Y)", &pointLight.HeightOffset, 0.05f, 0.0f, 6.0f, "%.2f m");
    ImGui::Separator();
    ImGui::Checkbox("Torch Flicker", &pointLight.Flicker);
    if (pointLight.Flicker) {
        ImGui::SliderFloat("Flicker Speed", &pointLight.FlickerSpeed, 0.5f, 20.0f, "%.1f");
        ImGui::SliderFloat("Flicker Amount", &pointLight.FlickerAmount, 0.0f, 0.6f, "%.2f");
    }
}

void InspectorPanel::DrawPostProcess(Engine::PostProcessComponent& pp, Engine::GameObject* entity) {
    ImGui::Checkbox("Enabled", &pp.Enabled);
    ImGui::TextDisabled("Mood Presets:");
    if (ImGui::SmallButton("Cult Storybook")) {
        ENGINE_INFO("Inspector: Applied 'Cult Storybook' post-process preset on '{}'", entity->GetName());
        pp.ApplyPreset(Engine::PostProcessComponent::Preset::CultStorybook);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Golden Clearing")) {
        ENGINE_INFO("Inspector: Applied 'Golden Clearing' post-process preset on '{}'", entity->GetName());
        pp.ApplyPreset(Engine::PostProcessComponent::Preset::GoldenClearing);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Blood Moon")) {
        ENGINE_INFO("Inspector: Applied 'Blood Moon' post-process preset on '{}'", entity->GetName());
        pp.ApplyPreset(Engine::PostProcessComponent::Preset::BloodMoonRitual);
    }
    if (ImGui::SmallButton("Midnight Sanctum")) {
        ENGINE_INFO("Inspector: Applied 'Midnight Sanctum' post-process preset on '{}'", entity->GetName());
        pp.ApplyPreset(Engine::PostProcessComponent::Preset::MidnightSanctum);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Crisp Neutral")) {
        ENGINE_INFO("Inspector: Applied 'Crisp Neutral' post-process preset on '{}'", entity->GetName());
        pp.ApplyPreset(Engine::PostProcessComponent::Preset::CrispNeutral);
    }

    ImGui::Separator();
    ImGui::Text("Exposure & Tone Mapping");
    const char* toneMapLabels[] = {
        "ACES Filmic",
        "Storybook Neutral (Hue-True)",
        "Reinhard",
        "None (Linear Clamp)"
    };
    int tmIdx = static_cast<int>(pp.ToneMapping);
    if (ImGui::Combo("Tone Mapper", &tmIdx, toneMapLabels, 4)) {
        pp.ToneMapping = static_cast<Engine::PostProcessComponent::ToneMapMode>(tmIdx);
        ENGINE_DEBUG("Inspector: Tone mapper changed to '{}' on '{}'", toneMapLabels[tmIdx], entity->GetName());
    }
    ImGui::SliderFloat("Exposure (EV)", &pp.Exposure, -3.0f, 3.0f, "%.2f");
    ImGui::ColorEdit3("HDR Backdrop", &pp.BackdropColor.x);

    ImGui::Separator();
    ImGui::Checkbox("HDR Multi-Mip Bloom", &pp.BloomEnabled);
    if (pp.BloomEnabled) {
        ImGui::SliderFloat("Threshold", &pp.BloomThreshold, 0.25f, 3.0f, "%.2f");
        ImGui::SliderFloat("Soft Knee", &pp.BloomKnee, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Intensity##Bloom", &pp.BloomIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Scatter Radius", &pp.BloomScatter, 0.0f, 0.95f, "%.2f");
    }

    ImGui::Separator();
    ImGui::Text("Color Grading & Split Toning");
    ImGui::SliderFloat("Temperature", &pp.Temperature, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Tint", &pp.Tint, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Contrast", &pp.Contrast, 0.5f, 2.0f, "%.2f");
    ImGui::SliderFloat("Saturation", &pp.Saturation, 0.0f, 2.0f, "%.2f");
    ImGui::ColorEdit3("Shadow Tint", &pp.ShadowTint.x);
    ImGui::ColorEdit3("Highlight Tint", &pp.HighlightTint.x);

    ImGui::Separator();
    ImGui::Checkbox("Diorama Vignette", &pp.VignetteEnabled);
    if (pp.VignetteEnabled) {
        ImGui::SliderFloat("Intensity##Vignette", &pp.VignetteIntensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Smoothness", &pp.VignetteSmoothness, 0.05f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Vignette Color", &pp.VignetteColor.x);
    }
}

void InspectorPanel::DrawWindZone(Engine::WindComponent& wind, Engine::GameObject* entity) {
    ImGui::Checkbox("Enabled", &wind.Enabled);
    ImGui::TextDisabled("Wind Presets:");
    if (ImGui::SmallButton("Meadow Breeze")) {
        ENGINE_INFO("Inspector: Applied 'GentleMeadowBreeze' wind preset on '{}'", entity->GetName());
        wind.ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Autumn Gusts")) {
        ENGINE_INFO("Inspector: Applied 'AutumnGusts' wind preset on '{}'", entity->GetName());
        wind.ApplyPreset(Engine::WindComponent::Preset::AutumnGusts);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Ritual Storm")) {
        ENGINE_INFO("Inspector: Applied 'RitualStorm' wind preset on '{}'", entity->GetName());
        wind.ApplyPreset(Engine::WindComponent::Preset::RitualStorm);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Still Air")) {
        ENGINE_INFO("Inspector: Applied 'StillAir' wind preset on '{}'", entity->GetName());
        wind.ApplyPreset(Engine::WindComponent::Preset::StillAir);
    }

    ImGui::Separator();
    ImGui::SliderFloat("Direction (deg)", &wind.DirectionDegrees, -180.0f, 180.0f, "%.1f");
    ImGui::SliderFloat("Sway Strength (m)", &wind.Strength, 0.0f, 0.75f, "%.2f m");
    ImGui::SliderFloat("Wave Speed", &wind.Speed, 0.2f, 8.0f, "%.2f");
    ImGui::SliderFloat("Gustiness", &wind.Gustiness, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Gust Frequency", &wind.GustFrequency, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("Spatial Wave Scale", &wind.SpatialWaveScale, 0.05f, 2.0f, "%.2f");

    ImGui::Separator();
    ImGui::Checkbox("Interactive Player Parting", &wind.InteractiveRustle);
    if (wind.InteractiveRustle) {
        ImGui::SliderFloat("Parting Radius", &wind.PlayerInteractionRadius, 0.25f, 3.5f, "%.2f m");
        ImGui::SliderFloat("Push Strength", &wind.PlayerPushStrength, 0.0f, 1.0f, "%.2f m");
    }
}

void InspectorPanel::DrawSquashStretch(Engine::SquashStretchComponent& ss, Engine::GameObject* entity) {
    ImGui::Checkbox("Enabled", &ss.Enabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("Test Pop Impulse")) {
        ENGINE_INFO("Inspector: Triggered test pop impulse on '{}'", entity->GetName());
        ss.TriggerImpulse(4.20f, 9.0f);
    }

    ImGui::SliderFloat("Spring Stiffness", &ss.Stiffness, 30.0f, 400.0f, "%.0f");
    ImGui::SliderFloat("Spring Damping", &ss.Damping, 2.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("Max Squash", &ss.MaxSquash, 0.05f, 0.60f, "%.2f");
    ImGui::SliderFloat("Max Stretch", &ss.MaxStretch, 0.05f, 0.65f, "%.2f");

    ImGui::Separator();
    ImGui::Checkbox("Walk Hop & Waddle", &ss.EnableWalkHop);
    if (ss.EnableWalkHop) {
        ImGui::SliderFloat("Walk Frequency", &ss.WalkBobFrequency, 4.0f, 22.0f, "%.1f");
        ImGui::SliderFloat("Walk Squash", &ss.WalkSquashAmount, 0.0f, 0.20f, "%.3f");
        ImGui::SliderFloat("Walk Tilt (deg)", &ss.WalkTiltDegrees, 0.0f, 15.0f, "%.1f");
    }
    ImGui::SliderFloat("Jump Launch Stretch", &ss.JumpLaunchStretch, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("Landing Squash", &ss.LandingSquashImpact, 0.0f, 2.0f, "%.2f");
}

void InspectorPanel::DrawRigidbody2D(Engine::Rigidbody2D& rb2d, Engine::GameObject* entity) {
    (void)entity;
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
}

void InspectorPanel::DrawBoxCollider2D(Engine::BoxCollider2D& boxCollider, Engine::GameObject* entity) {
    (void)entity;
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
}

void InspectorPanel::DrawCamera(Engine::CameraComponent& camComp, Engine::GameObject* entity) {
    bool changed = false;
    changed |= ImGui::SliderFloat("FOV (deg)", &camComp.FOV, 15.0f, 90.0f, "%.1f");
    changed |= ImGui::DragFloat("Distance", &camComp.Distance, 0.2f, 2.0f, 80.0f);
    changed |= ImGui::SliderFloat("Pitch (deg)", &camComp.Pitch, 15.0f, 80.0f, "%.1f");
    changed |= ImGui::DragFloat("Yaw (deg)", &camComp.Yaw, 0.5f, -360.0f, 360.0f);
    ImGui::Checkbox("Fixed Aspect Ratio", &camComp.FixedAspectRatio);
    if (changed) {
        camComp.SyncFromTransform(entity->GetTransform());
    }
}

void InspectorPanel::DrawTerrain(Engine::TerrainComponent& terrain, Engine::GameObject* entity) {
    (void)entity;
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
}

void InspectorPanel::DrawTilemap(Engine::Tilemap& tilemapComp, Engine::GameObject* entity) {
    (void)entity;
    ImGui::Text("Dimensions: %u x %u (%u tiles)",
                tilemapComp.GetWidth(),
                tilemapComp.GetHeight(),
                tilemapComp.GetWidth() * tilemapComp.GetHeight());
    ImGui::TextDisabled("Switch Tile Palette to 'Tile Grid' to paint discrete tiles.");
}

void InspectorPanel::DrawAddComponentMenu(Engine::GameObject* entity) {
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        DrawAddComponentEntry<Engine::SpriteRenderer>("Sprite Renderer", entity);
        DrawAddComponentEntry<Engine::TerrainComponent>("Terrain (Splatmap)", entity);
        DrawAddComponentEntry<Engine::DirectionalLightComponent>("Directional Light", entity);
        DrawAddComponentEntry<Engine::PointLightComponent>("Point Light", entity);
        DrawAddComponentEntry<Engine::PostProcessComponent>("Post Process Volume", entity);
        DrawAddComponentEntry<Engine::WindComponent>("Wind Zone", entity);
        DrawAddComponentEntry<Engine::SquashStretchComponent>("Squash & Stretch", entity);
        DrawAddComponentEntry<Engine::CameraComponent>("Camera", entity);
        DrawAddComponentEntry<Engine::ParticleSystem>("Particle System", entity);
        DrawAddComponentEntry<Engine::Rigidbody2D>("Rigidbody 2D", entity);
        DrawAddComponentEntry<Engine::BoxCollider2D>("Box Collider 2D", entity);
        ImGui::EndPopup();
    }
}

} // namespace Sandbox
