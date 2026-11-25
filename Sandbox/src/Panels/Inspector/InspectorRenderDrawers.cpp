#include "Panels/Inspector/InspectorPanel.h"

#include <Engine/Animation/AnimationClip.h>
#include <Engine/Animation/Animator.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/Components/CameraComponent.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/Components/PostProcessComponent.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Sandbox {

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
    int modeIdx = std::clamp(static_cast<int>(spriteRenderer.GetBillboardMode()), 0, IM_ARRAYSIZE(billboardLabels) - 1);
    if (ImGui::Combo("Billboard Mode", &modeIdx, billboardLabels, IM_ARRAYSIZE(billboardLabels))) {
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

    if (ImGui::TreeNodeEx("Combat & Cutout FX", ImGuiTreeNodeFlags_DefaultOpen)) {
        float dissolve = spriteRenderer.GetDissolveAmount();
        if (ImGui::SliderFloat("Dissolve Amount", &dissolve, 0.0f, 1.0f, "%.2f")) {
            spriteRenderer.SetDissolveAmount(dissolve);
        }
        if (dissolve > 0.001f) {
            float edgeWidth = spriteRenderer.GetDissolveEdgeWidth();
            if (ImGui::SliderFloat("Dissolve Edge Width", &edgeWidth, 0.01f, 0.35f, "%.2f")) {
                spriteRenderer.SetDissolveEdgeWidth(edgeWidth);
            }
        }

        float flash = spriteRenderer.GetFlashAmount();
        if (ImGui::SliderFloat("Hit Flash", &flash, 0.0f, 1.0f, "%.2f")) {
            spriteRenderer.SetFlashAmount(flash);
        }
        if (flash > 0.001f) {
            const char* flashModes[] = { "Pure White (Stun)", "Crimson Impact", "Spectral Blue", "Divine Gold" };
            float modeVal = spriteRenderer.GetFlashColorMode();
            int currentMode = 0;
            if (modeVal <= -0.5f) {
                currentMode = std::clamp(static_cast<int>(std::round(-modeVal)) - 1, 0, 3);
            } else if (modeVal >= 0.0f && modeVal <= 3.0f) {
                currentMode = static_cast<int>(modeVal);
            }
            if (ImGui::Combo("Flash Tint Mode", &currentMode, flashModes, 4)) {
                // FlashMode enum maps -1 to PureWhite, -2 to Crimson, -3 to SpectralBlue, -4 to DivineGold
                spriteRenderer.SetFlashMode(static_cast<Engine::SpriteRenderer::FlashMode>(-(currentMode + 1)));
            }
        }

        bool silhouette = spriteRenderer.GetRenderSilhouette();
        if (ImGui::Checkbox("Render Silhouette", &silhouette)) {
            spriteRenderer.SetRenderSilhouette(silhouette);
        }
        if (silhouette) {
            glm::vec4 silColor = spriteRenderer.GetSilhouetteColor();
            if (ImGui::ColorEdit4("Silhouette Color", &silColor.x)) {
                spriteRenderer.SetSilhouetteColor(silColor);
            }
        }

        ImGui::TreePop();
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

    const char* texLabel = (particleSystem.ParticleTexture && !particleSystem.ParticleTexture->GetFilepath().empty())
        ? particleSystem.ParticleTexture->GetFilepath().c_str()
        : "None (Solid Color)";
    ImGui::Text("Texture: %s", texLabel);

    static const char* kOrientations[] = { "CameraBillboard", "FlatGroundXZ", "VelocityAligned" };
    int orientationIdx = std::clamp(static_cast<int>(particleSystem.Orientation), 0, IM_ARRAYSIZE(kOrientations) - 1);
    if (ImGui::Combo("Orientation", &orientationIdx, kOrientations, IM_ARRAYSIZE(kOrientations))) {
        particleSystem.Orientation = static_cast<Engine::ParticleOrientation>(orientationIdx);
    }

    ImGui::Checkbox("Auto Emit", &particleSystem.AutoEmit);
    ImGui::SameLine();
    ImGui::Checkbox("Emissive Glow", &particleSystem.EmissiveParticles);
    if (particleSystem.AutoEmit) {
        ImGui::SliderFloat("Emit Rate (/s)", &particleSystem.EmitRate, 0.5f, 60.0f, "%.1f");
        ImGui::DragFloat3("Spawn Offset", &particleSystem.SpawnOffset.x, 0.05f);
    }

    if (ImGui::TreeNodeEx("Ballistics & Ground Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Gravity Y", &particleSystem.GravityY, 0.25f, -60.0f, 60.0f, "%.2f");
        ImGui::DragFloat("Linear Drag", &particleSystem.LinearDrag, 0.05f, 0.0f, 20.0f, "%.2f");
        ImGui::Checkbox("Collide Ground Y", &particleSystem.CollideGroundY);
        if (particleSystem.CollideGroundY) {
            ImGui::DragFloat("Ground Plane Y", &particleSystem.GroundPlaneY, 0.01f, -5.0f, 5.0f, "%.3f");
            ImGui::SliderFloat("Bounce Restitution", &particleSystem.GroundBounceRestitution, 0.0f, 1.0f, "%.2f");
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Flipbook Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& fb = particleSystem.Flipbook;
        ImGui::Checkbox("Enable Flipbook", &fb.Enabled);
        if (fb.Enabled) {
            int cols = static_cast<int>(fb.Columns);
            int rows = static_cast<int>(fb.Rows);
            int total = static_cast<int>(fb.TotalFrames);
            if (ImGui::DragInt("Columns", &cols, 1.0f, 1, 16)) {
                fb.Columns = static_cast<uint32_t>(std::max(1, cols));
            }
            if (ImGui::DragInt("Rows", &rows, 1.0f, 1, 16)) {
                fb.Rows = static_cast<uint32_t>(std::max(1, rows));
            }
            const int maxFrames = static_cast<int>(fb.Columns * fb.Rows);
            if (ImGui::DragInt("Total Frames", &total, 1.0f, 1, maxFrames)) {
                fb.TotalFrames = static_cast<uint32_t>(std::clamp(total, 1, maxFrames));
            }
            ImGui::DragFloat("Cycles / Lifetime", &fb.CyclesOverLifetime, 0.05f, 0.1f, 16.0f, "%.2f");
            ImGui::Checkbox("Random Start Frame", &fb.RandomStartFrame);
        }
        ImGui::TreePop();
    }

    if (ImGui::Button("Emit Test Particle")) {
        Engine::ParticleProps props = particleSystem.TemplateProps;
        if (auto* t = entity->GetTransform()) {
            props.position = t->GetWorldPosition() + particleSystem.SpawnOffset;
        }
        particleSystem.Emit(props);
        ENGINE_DEBUG("Inspector: Emitted test particle on '{}'", entity->GetName());
    }
    ImGui::SameLine();
    if (ImGui::Button("Emit Burst (16)")) {
        Engine::ParticleProps props = particleSystem.TemplateProps;
        if (auto* t = entity->GetTransform()) {
            props.position = t->GetWorldPosition() + particleSystem.SpawnOffset;
        }
        particleSystem.EmitBurst(props, 16u);
        ENGINE_DEBUG("Inspector: Emitted burst of 16 particles on '{}'", entity->GetName());
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
    ImGui::SliderFloat("Radius (m)", &pointLight.Radius, 0.5f, 18.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &pointLight.Falloff, 0.3f, 4.0f, "%.2f");
    ImGui::DragFloat("Height Offset (Y)", &pointLight.HeightOffset, 0.05f, 0.0f, 6.0f, "%.2f m");
    ImGui::Separator();
    const char* priorityNames[] = { "Low (VFX)", "Normal (World)", "High (Tactical)", "Critical (Hero Torch)" };
    int priorityIdx = std::clamp(static_cast<int>(pointLight.Priority), 0, 3);
    if (ImGui::Combo("Priority", &priorityIdx, priorityNames, IM_ARRAYSIZE(priorityNames))) {
        pointLight.Priority = static_cast<Engine::LightPriority>(priorityIdx);
    }
    const char* semanticNames[] = { "Safe Lifeline", "Neutral / Shrine", "Deceptive Hazard" };
    int semanticIdx = std::clamp(static_cast<int>(pointLight.Semantic), 0, 2);
    if (ImGui::Combo("Semantic", &semanticIdx, semanticNames, IM_ARRAYSIZE(semanticNames))) {
        pointLight.Semantic = static_cast<Engine::LightSemantic>(semanticIdx);
    }
    ImGui::Checkbox("Cast Shadows", &pointLight.CastShadows);
    if (pointLight.CastShadows) {
        ImGui::SliderFloat("Shadow Softness", &pointLight.ShadowSoftness, 0.25f, 4.0f, "%.2f");
        ImGui::SliderFloat("Shadow Bias", &pointLight.ShadowBias, 0.0002f, 0.01f, "%.4f");
    }
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
    int tmIdx = std::clamp(static_cast<int>(pp.ToneMapping), 0, IM_ARRAYSIZE(toneMapLabels) - 1);
    if (ImGui::Combo("Tone Mapper", &tmIdx, toneMapLabels, IM_ARRAYSIZE(toneMapLabels))) {
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


void InspectorPanel::DrawCamera(Engine::CameraComponent& camComp, Engine::GameObject* entity) {
    bool changed = false;
    const char* modeItems[] = { "Fixed Vertical (Hor+)", "Adaptive (Landscape vFOV / Portrait hFOV)" };
    int currentMode = std::clamp(static_cast<int>(camComp.FOVMode), 0, IM_ARRAYSIZE(modeItems) - 1);
    if (ImGui::Combo("FOV Adaptation", &currentMode, modeItems, IM_ARRAYSIZE(modeItems))) {
        camComp.FOVMode = static_cast<Engine::FOVAdaptationMode>(currentMode);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Landscape vFOV (deg)", &camComp.FOV, 15.0f, 90.0f, "%.1f");
    if (camComp.FOVMode == Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz) {
        changed |= ImGui::SliderFloat("Portrait hFOV (deg)", &camComp.PortraitHorizontalFOV, 12.0f, 90.0f, "%.1f");
    }
    changed |= ImGui::DragFloat("Distance", &camComp.Distance, 0.2f, 2.0f, 80.0f);
    changed |= ImGui::SliderFloat("Pitch (deg)", &camComp.Pitch, 15.0f, 80.0f, "%.1f");
    changed |= ImGui::DragFloat("Yaw (deg)", &camComp.Yaw, 0.5f, -360.0f, 360.0f);
    ImGui::Checkbox("Fixed Aspect Ratio", &camComp.FixedAspectRatio);

    if (ImGui::TreeNodeEx("Brawler Rig & Follow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Smooth Follow", &camComp.SmoothFollow);
        ImGui::SliderFloat("Follow Smooth Time", &camComp.FollowSmoothTime, 0.01f, 1.0f, "%.2f s");
        ImGui::SliderFloat("Aim Look-Ahead", &camComp.AimLookAheadDistance, 0.0f, 5.0f, "%.2f m");
        ImGui::SliderFloat("Aim Smooth Time", &camComp.AimLookAheadSmoothTime, 0.01f, 1.0f, "%.2f s");
        ImGui::SliderFloat("Dynamic Zoom Out", &camComp.DynamicZoomOutMax, 0.0f, 10.0f, "%.2f m");
        ImGui::SliderFloat("Zoom Smooth Time", &camComp.DynamicZoomSmoothTime, 0.01f, 1.0f, "%.2f s");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Trauma & Screen Shake", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Trauma Decay", &camComp.TraumaDecay, 0.1f, 5.0f, "%.2f /s");
        ImGui::SliderFloat("Max Shake Amplitude", &camComp.MaxShakeAmplitude, 0.05f, 3.0f, "%.2f m");
        ImGui::SliderFloat("Shake Frequency", &camComp.ShakeFrequency, 5.0f, 60.0f, "%.1f Hz");
        ImGui::SliderFloat("Kick Decay Rate", &camComp.KickDecayRate, 1.0f, 20.0f, "%.1f");

        const float traumaVal = camComp.GetTrauma();
        char traumaBuf[32];
        snprintf(traumaBuf, sizeof(traumaBuf), "Trauma: %.2f", traumaVal);
        ImGui::ProgressBar(traumaVal, ImVec2(-1.0f, 0.0f), traumaBuf);

        if (ImGui::Button("Add Light Trauma (0.3)")) {
            camComp.AddTrauma(0.3f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Heavy Slam (0.8 + Kick)")) {
            camComp.AddTrauma(0.8f, glm::vec2(0.4f, -0.25f));
        }
        if (ImGui::Button("Reset Camera To Target")) {
            if (entity && entity->GetTransform()) {
                camComp.ResetToTarget(entity->GetTransform()->GetWorldPosition());
            }
        }
        ImGui::TreePop();
    }

    if (changed) {
        camComp.SyncFromTransform(entity ? entity->GetTransform() : nullptr);
    }
}

void InspectorPanel::DrawAnimator(Engine::Animator& animator, [[maybe_unused]] Engine::GameObject* entity) {
    // 1. Directional Scheme & Orientation Controls
    const char* schemeNames[] = { "Omni 1-Way", "Front/Back 2-Way", "Cardinal 4-Way" };
    int schemeIdx = std::clamp(static_cast<int>(animator.GetDirectionalScheme()), 0, IM_ARRAYSIZE(schemeNames) - 1);
    if (ImGui::Combo("Directional Scheme", &schemeIdx, schemeNames, IM_ARRAYSIZE(schemeNames))) {
        animator.SetDirectionalScheme(static_cast<Engine::DirectionalScheme>(schemeIdx));
    }

    const char* facingNames[] = { "None", "Down / Front", "Side", "Up / Back", "Diag" };
    int facingIdx = std::clamp(static_cast<int>(animator.GetCurrentFacingDirection()), 0, IM_ARRAYSIZE(facingNames) - 1);
    if (ImGui::Combo("Facing Direction", &facingIdx, facingNames, IM_ARRAYSIZE(facingNames))) {
        animator.SetCurrentFacingDirection(static_cast<Engine::FacingDirection>(facingIdx));
    }

    bool flipX = animator.GetCurrentFlipX();
    if (ImGui::Checkbox("FlipX Preview", &flipX)) {
        animator.SetCurrentFlipX(flipX);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Toggle FlipX")) {
        animator.SetCurrentFlipX(!animator.GetCurrentFlipX());
    }

    float speed = animator.GetPlaybackSpeed();
    if (ImGui::DragFloat("Playback Speed", &speed, 0.05f, 0.0f, 10.0f, "%.2fx")) {
        animator.SetPlaybackSpeed(speed);
    }

    float hysteresis = animator.GetHysteresisDegrees();
    if (ImGui::SliderFloat("Hysteresis Angle", &hysteresis, 0.0f, 45.0f, "%.1f deg")) {
        animator.SetHysteresisDegrees(hysteresis);
    }

    // 2. Clip Selection & State Machine Info
    ImGui::Separator();
    const auto& clips = animator.GetClips();
    ImGui::Text("Animation Clips (%zu registered)", clips.size());

    if (clips.empty()) {
        ImGui::TextDisabled("No clips attached to Animator.");
        return;
    }

    std::vector<const char*> clipNames;
    clipNames.reserve(clips.size());
    int currentClipIndex = -1;
    int index = 0;
    const std::string currentClipName = animator.GetCurrentClipName();

    for (const auto& [name, clipPtr] : clips) {
        clipNames.push_back(name.c_str());
        if (name == currentClipName) {
            currentClipIndex = index;
        }
        index++;
    }

    if (currentClipIndex == -1 && !clipNames.empty()) {
        currentClipIndex = 0;
    }

    if (ImGui::Combo("Active Clip", &currentClipIndex, clipNames.data(), static_cast<int>(clipNames.size()))) {
        animator.Play(clipNames[currentClipIndex]);
    }

    auto currentClip = animator.GetCurrentClip();
    if (!currentClip) {
        auto it = clips.find(clipNames[currentClipIndex]);
        if (it != clips.end()) {
            currentClip = it->second;
        }
    }

    // 3. Transport & Live Scrub Bar
    if (currentClip) {
        const uint32_t frameCount = static_cast<uint32_t>(!currentClip->Frames.empty()
            ? currentClip->Frames.size()
            : currentClip->frames.size());
        const float totalDuration = currentClip->GetTotalDuration();

        ImGui::Text("Frames: %u | Total Duration: %.3f s", frameCount, totalDuration);
        ImGui::Checkbox("Looping", &currentClip->loop);
        ImGui::SameLine();
        if (ImGui::DragFloat("Frame Dur", &currentClip->frameDuration, 0.005f, 0.01f, 1.0f, "%.3f s")) {
            currentClip->SetUniformFrameDuration(currentClip->frameDuration);
        }

        if (frameCount > 0) {
            auto ensureClipActive = [&]() {
                if (!animator.GetCurrentClip()) {
                    animator.Play(clipNames[currentClipIndex]);
                }
            };

            // Transport buttons
            if (ImGui::Button("|<")) {
                ensureClipActive();
                animator.ScrubToFrame(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("< Prev")) {
                ensureClipActive();
                const uint32_t cur = animator.GetCurrentFrameIndex();
                if (cur > 0) animator.ScrubToFrame(cur - 1);
            }
            ImGui::SameLine();
            const bool isPlaying = animator.IsPreviewPlaying();
            if (ImGui::Button(isPlaying ? "Pause Preview" : "Play Preview")) {
                if (!isPlaying) {
                    ensureClipActive();
                }
                animator.SetPreviewPlaying(!isPlaying);
            }
            ImGui::SameLine();
            if (ImGui::Button("Next >")) {
                ensureClipActive();
                const uint32_t cur = animator.GetCurrentFrameIndex();
                if (cur + 1 < frameCount) animator.ScrubToFrame(cur + 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(">|")) {
                ensureClipActive();
                animator.ScrubToFrame(frameCount - 1);
            }

            // Scrub slider
            int curFrame = static_cast<int>(animator.GetCurrentFrameIndex());
            if (ImGui::SliderInt("Scrub Bar", &curFrame, 0, static_cast<int>(frameCount - 1), "Frame %d")) {
                ensureClipActive();
                animator.ScrubToFrame(static_cast<uint32_t>(curFrame));
            }

            ImGui::Text("Time: %.3f s / %.3f s (Frame %u / %u)",
                        animator.GetCurrentTime(), totalDuration,
                        animator.GetCurrentFrameIndex(), frameCount);

            // 4. Frame Event Track & Badges
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Frame Events (%zu registered)", currentClip->Events.size());

            // Timeline Badge Markers for frames with events
            ImGui::TextDisabled("Timeline Markers:");
            for (uint32_t f = 0; f < frameCount; ++f) {
                for (size_t evIdx = 0; evIdx < currentClip->Events.size(); ++evIdx) {
                    if (currentClip->Events[evIdx].FrameIndex == f) {
                        const auto& ev = currentClip->Events[evIdx];
                        ImVec4 badgeColor(0.20f, 0.70f, 0.85f, 1.0f);
                        if (ev.EventID.find("Hitbox") != std::string::npos || ev.EventID.find("Damage") != std::string::npos) {
                            badgeColor = ImVec4(0.95f, 0.35f, 0.20f, 1.0f);
                        } else if (ev.EventID.find("Footstep") != std::string::npos) {
                            badgeColor = ImVec4(0.30f, 0.85f, 0.40f, 1.0f);
                        } else if (ev.EventID.find("Slam") != std::string::npos || ev.EventID.find("Impact") != std::string::npos) {
                            badgeColor = ImVec4(0.95f, 0.80f, 0.20f, 1.0f);
                        }

                        ImGui::PushStyleColor(ImGuiCol_Button, badgeColor);
                        char badgeLabel[128];
                        std::snprintf(badgeLabel, sizeof(badgeLabel), "F%u: %s##%zu", f, ev.EventID.c_str(), evIdx);
                        if (ImGui::SmallButton(badgeLabel)) {
                            ensureClipActive();
                            animator.ScrubToFrame(f);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Frame %u: %s\nSocket Offset: (%.2f, %.2f) m\nClick to jump to frame",
                                              f, ev.EventID.c_str(), ev.SocketOffset.x, ev.SocketOffset.y);
                        }
                        ImGui::SameLine();
                    }
                }
            }
            ImGui::NewLine();

            // Add Event on current frame
            const uint32_t currentF = animator.GetCurrentFrameIndex();
            char addBtnLabel[64];
            std::snprintf(addBtnLabel, sizeof(addBtnLabel), "+ Add Event on Frame %u", currentF);
            if (ImGui::Button(addBtnLabel)) {
                currentClip->AddEvent(currentF, "HitboxActive", glm::vec2(0.5f, 0.0f));
            }

            // Detailed Event Table Editor
            if (!currentClip->Events.empty()) {
                if (ImGui::BeginTable("ClipEventsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Event ID", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Socket Offset (m)", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                    ImGui::TableHeadersRow();

                    int eventToDelete = -1;
                    for (size_t evIdx = 0; evIdx < currentClip->Events.size(); ++evIdx) {
                        auto& ev = currentClip->Events[evIdx];
                        ImGui::PushID(static_cast<int>(evIdx));

                        ImGui::TableNextRow();
                        // Frame Index
                        ImGui::TableSetColumnIndex(0);
                        int fIdx = static_cast<int>(ev.FrameIndex);
                        ImGui::SetNextItemWidth(55.0f);
                        if (ImGui::DragInt("##Frame", &fIdx, 0.1f, 0, static_cast<int>(frameCount - 1))) {
                            ev.FrameIndex = static_cast<uint32_t>(std::clamp(fIdx, 0, static_cast<int>(frameCount - 1)));
                        }

                        // Event ID
                        ImGui::TableSetColumnIndex(1);
                        char idBuf[128];
                        std::snprintf(idBuf, sizeof(idBuf), "%s", ev.EventID.c_str());
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText("##EventID", idBuf, sizeof(idBuf))) {
                            ev.EventID = idBuf;
                        }

                        // Socket Offset (m)
                        ImGui::TableSetColumnIndex(2);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::DragFloat2("##Offset", &ev.SocketOffset.x, 0.02f, -10.0f, 10.0f, "%.2f");

                        // Delete button
                        ImGui::TableSetColumnIndex(3);
                        if (ImGui::SmallButton("X")) {
                            eventToDelete = static_cast<int>(evIdx);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Delete event");
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();

                    if (eventToDelete >= 0) {
                        currentClip->RemoveEvent(static_cast<size_t>(eventToDelete));
                    }
                }
            }
        }
    }
}

} // namespace Sandbox
