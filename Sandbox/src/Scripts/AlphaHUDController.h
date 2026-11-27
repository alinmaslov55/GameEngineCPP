#ifndef SANDBOX_ALPHA_HUD_CONTROLLER_H
#define SANDBOX_ALPHA_HUD_CONTROLLER_H

#include "Scripts/DeployableStructures.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/RuneCasterComponent.h"
#include "Scripts/RunProgressionComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WaveDirectorComponent.h"

#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/UI/MinimapFogSystem.h>
#include <Engine/UI/UICanvas.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIMinimapComponent.h>
#include <Engine/UI/UIPanel.h>
#include <Engine/UI/UIRadialWheelComponent.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

class AlphaHUDController final : public Engine::Component {
public:
    void Awake() override {
        BuildHUD();
    }

    void Update(float deltaTime) override {
        if (!m_Built) {
            BuildHUD();
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        if (std::isfinite(deltaTime) && deltaTime > 0.0f) {
            m_PulseTime += deltaTime * 6.0f;
        }

        auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        auto* stats = player ? player->GetComponent<PlayerStatsComponent>() : nullptr;
        auto* torch = ResolveTorch(player);
        auto* runeCaster = player ? player->GetComponent<RuneCasterComponent>() : nullptr;
        auto* prog = player ? player->GetComponent<RunProgressionComponent>() : nullptr;
        auto* director = FindWaveDirector(*scene);

        // 1. Top-Left: HP Bar, Torch Fuel Bar, Active Rune RadialSweepCW
        if (m_HealthBar && stats) {
            m_HealthBar->fillAmount = (stats->MaxHP > 1e-4f) ? std::clamp(stats->HP / stats->MaxHP, 0.0f, 1.0f) : 0.0f;
            if (m_HealthLabel) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "HP %d / %d", static_cast<int>(std::ceil(stats->HP)), static_cast<int>(stats->MaxHP));
                m_HealthLabel->text = buf;
            }
        }

        if (m_TorchBar && torch) {
            const float fuel01 = torch->GetFuel01();
            m_TorchBar->fillAmount = fuel01;
            if (torch->IsAshFrenzy()) {
                const float pulse = 0.5f + 0.5f * std::sin(m_PulseTime);
                m_TorchBar->color = { 0.92f, 0.14f + 0.16f * pulse, 0.14f, 0.98f };
            } else if (torch->IsDarknessDread()) {
                m_TorchBar->color = { 0.35f, 0.12f, 0.18f, 0.85f };
            } else {
                m_TorchBar->color = { 1.0f, 0.62f, 0.18f, 0.96f };
            }
            if (m_TorchLabel) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "TORCH %d%%", static_cast<int>(std::round(fuel01 * 100.0f)));
                m_TorchLabel->text = buf;
            }
        }

        if (m_RuneCooldownPanel && runeCaster) {
            const auto& rune = runeCaster->GetEquippedRune();
            const float cd = runeCaster->GetCooldownRemaining();
            const float maxCd = std::max(0.01f, rune.CooldownSec);
            m_RuneCooldownPanel->fillAmount = std::clamp(1.0f - (cd / maxCd), 0.0f, 1.0f);
            if (m_RuneLabel) {
                m_RuneLabel->text = rune.Name + " [Q]";
            }
        }

        // 2. Top-Center: Clock, Phase Banner, Level/XP, Boss Bar
        if (director) {
            const float elapsed = director->GetElapsedSeconds();
            const int mins = static_cast<int>(elapsed) / 60;
            const int secs = static_cast<int>(elapsed) % 60;
            if (m_ClockLabel) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d / 05:00", mins, secs);
                m_ClockLabel->text = buf;
            }
            if (m_PhaseLabel) {
                m_PhaseLabel->text = PhaseToString(director->GetCurrentPhase());
            }

            Engine::GameObject* boss = director->GetActiveBoss();
            if (m_BossBar && m_BossLabel) {
                if (boss) {
                    auto* bHealth = boss->GetComponent<HealthComponent>();
                    const float ratio = (bHealth && bHealth->MaxHP > 1e-4f)
                        ? std::clamp(bHealth->CurrentHP / bHealth->MaxHP, 0.0f, 1.0f)
                        : 1.0f;
                    m_BossBar->fillAmount = ratio;
                    m_BossBar->color = { 0.82f, 0.14f, 0.22f, 0.95f };
                    m_BossLabel->text = "APEX WENDIGO";
                } else {
                    m_BossBar->fillAmount = 0.0f;
                    m_BossBar->color = { 0.0f, 0.0f, 0.0f, 0.0f };
                    m_BossLabel->text.clear();
                }
            }
        }

        if (m_XPLabel && m_XPBar && stats) {
            const int lvl = prog ? prog->GetLevel() : 0;
            const int next = prog ? prog->GetNextLevelThreshold() : 15;
            m_XPBar->fillAmount = (next > 0) ? std::clamp(static_cast<float>(stats->XP) / static_cast<float>(next), 0.0f, 1.0f) : 0.0f;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Lv.%d  XP %d/%d", lvl, stats->XP, next);
            m_XPLabel->text = buf;
        }

        // 3. Top-Right: Circular MinimapFogSystem
        if (m_Minimap && m_FogSystem && player) {
            const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
            m_FogSystem->UpdateFromScene(*scene, { pPos.x, pPos.z });
            m_FogSystem->ClearPins();
            for (Engine::GameObject* brazier : scene->FindGameObjectsWithTag(Engine::EntityTag::SafeZoneBrazier)) {
                if (!brazier || brazier->IsPendingDestroy()) {
                    continue;
                }
                const glm::vec3 bPos = brazier->GetTransform()->GetWorldPosition();
                Engine::MinimapPOIPin pin;
                pin.EntityUUID = brazier->GetUUID();
                pin.Type = brazier->GetComponent<FlarePegComponent>() ? Engine::POIType::FlarePeg : Engine::POIType::IgnitedBrazier;
                pin.WorldXZ = { bPos.x, bPos.z };
                pin.Label = brazier->GetName();
                pin.IconColor = { 1.0f, 0.72f, 0.24f, 1.0f };
                pin.Discovered = true;
                m_FogSystem->AddPin(pin);
            }
            m_FogSystem->SyncGpuTexture();
        }

        // 4. Bottom-Left: Wood & Run-Ash counters + quick-drop hotkey strip
        if (m_ResourcesLabel && stats) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Wood: %d   Ashes: %d", stats->Wood, stats->RunAshes);
            m_ResourcesLabel->text = buf;
        }

        // 5. Center Interaction Prompt
        if (m_PromptLabel && torch && stats) {
            if (torch->GetFuel01() <= 0.50f && stats->Wood >= torch->StokeWoodCost) {
                m_PromptLabel->text = "[E] Stoke Torch (2 Wood)";
            } else {
                m_PromptLabel->text.clear();
            }
        }
    }

    [[nodiscard]] Engine::UIPanel* GetHealthBar() const noexcept { return m_HealthBar; }
    [[nodiscard]] Engine::UIPanel* GetTorchBar() const noexcept { return m_TorchBar; }
    [[nodiscard]] Engine::UIPanel* GetRuneCooldownPanel() const noexcept { return m_RuneCooldownPanel; }
    [[nodiscard]] Engine::UIPanel* GetBossBar() const noexcept { return m_BossBar; }
    [[nodiscard]] Engine::UILabel* GetClockLabel() const noexcept { return m_ClockLabel; }
    [[nodiscard]] Engine::UILabel* GetPhaseLabel() const noexcept { return m_PhaseLabel; }
    [[nodiscard]] Engine::UILabel* GetResourcesLabel() const noexcept { return m_ResourcesLabel; }
    [[nodiscard]] Engine::UIMinimapComponent* GetMinimap() const noexcept { return m_Minimap; }
    [[nodiscard]] Engine::UIRadialWheelComponent* GetRadialWheel() const noexcept { return m_RadialWheel; }

private:
    void BuildHUD() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene || m_Built) {
            return;
        }
        m_Built = true;

        auto makePanel = [&](const char* name, Engine::UIAnchor anchor, glm::vec2 pivot,
                             glm::vec2 offset, glm::vec2 size, glm::vec4 color,
                             Engine::UIRenderMode mode, int32_t zOrder) -> Engine::UIPanel* {
            auto* obj = scene->CreateGameObject(name);
            obj->GetTransform()->SetParent(go->GetTransform(), false);
            auto* p = obj->AddComponent<Engine::UIPanel>();
            p->useRectTransform = true;
            p->rectTransform.Anchor = anchor;
            p->rectTransform.Pivot = pivot;
            p->rectTransform.AnchoredOffset = offset;
            p->rectTransform.Size = size;
            p->rectTransform.ZOrder = zOrder;
            p->color = color;
            p->renderMode = mode;
            return p;
        };

        auto makeLabel = [&](const char* name, Engine::UIAnchor anchor, glm::vec2 pivot,
                             glm::vec2 offset, const std::string& text, float scale,
                             int32_t zOrder) -> Engine::UILabel* {
            auto* obj = scene->CreateGameObject(name);
            obj->GetTransform()->SetParent(go->GetTransform(), false);
            auto* l = obj->AddComponent<Engine::UILabel>();
            l->useRectTransform = true;
            l->rectTransform.Anchor = anchor;
            l->rectTransform.Pivot = pivot;
            l->rectTransform.AnchoredOffset = offset;
            l->rectTransform.ZOrder = zOrder;
            l->text = text;
            l->scale = scale;
            return l;
        };

        // Top-Left: HP Bar, Torch Bar, Rune RadialSweepCW
        m_HealthBar = makePanel("HUD_HealthBar", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                                { 24.0f, -24.0f }, { 260.0f, 22.0f }, { 0.85f, 0.18f, 0.20f, 0.95f },
                                Engine::UIRenderMode::HorizontalFill, 100);
        m_HealthLabel = makeLabel("HUD_HealthLabel", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                                  { 32.0f, -26.0f }, "HP 100 / 100", 0.75f, 101);

        m_TorchBar = makePanel("HUD_TorchBar", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                               { 24.0f, -54.0f }, { 260.0f, 20.0f }, { 1.0f, 0.62f, 0.18f, 0.96f },
                               Engine::UIRenderMode::HorizontalFill, 100);
        m_TorchLabel = makeLabel("HUD_TorchLabel", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                                 { 32.0f, -56.0f }, "TORCH 100%", 0.72f, 101);

        m_RuneCooldownPanel = makePanel("HUD_RuneCooldown", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                                        { 24.0f, -86.0f }, { 56.0f, 56.0f }, { 0.95f, 0.52f, 0.14f, 0.92f },
                                        Engine::UIRenderMode::RadialSweepCW, 100);
        m_RuneLabel = makeLabel("HUD_RuneLabel", Engine::UIAnchor::TopLeft, { 0.0f, 1.0f },
                                { 90.0f, -104.0f }, "Rune of Ignition [Q]", 0.75f, 101);

        // Top-Center: Clock, Phase Banner, XP Strip, Boss HP
        m_ClockLabel = makeLabel("HUD_Clock", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                                 { 0.0f, -18.0f }, "00:00 / 05:00", 1.0f, 101);
        m_PhaseLabel = makeLabel("HUD_Phase", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                                 { 0.0f, -48.0f }, "Dusk", 0.80f, 101);
        m_XPBar = makePanel("HUD_XPBar", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                            { 0.0f, -74.0f }, { 240.0f, 10.0f }, { 0.72f, 0.88f, 1.0f, 0.90f },
                            Engine::UIRenderMode::HorizontalFill, 100);
        m_XPLabel = makeLabel("HUD_XPLabel", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                              { 0.0f, -88.0f }, "Lv.0  XP 0/15", 0.68f, 101);

        m_BossBar = makePanel("HUD_BossBar", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                              { 0.0f, -118.0f }, { 420.0f, 18.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
                              Engine::UIRenderMode::HorizontalFill, 100);
        m_BossLabel = makeLabel("HUD_BossLabel", Engine::UIAnchor::TopCenter, { 0.5f, 1.0f },
                                { 0.0f, -120.0f }, "", 0.75f, 101);

        // Top-Right: Circular Fog-of-War Minimap
        auto* minimapGo = scene->CreateGameObject("HUD_Minimap");
        minimapGo->GetTransform()->SetParent(go->GetTransform(), false);
        m_Minimap = minimapGo->AddComponent<Engine::UIMinimapComponent>();
        m_Minimap->useRectTransform = true;
        m_Minimap->rectTransform.Anchor = Engine::UIAnchor::TopRight;
        m_Minimap->rectTransform.Pivot = { 1.0f, 1.0f };
        m_Minimap->rectTransform.AnchoredOffset = { -24.0f, -24.0f };
        m_Minimap->rectTransform.Size = { 172.0f, 172.0f };
        m_Minimap->rectTransform.ZOrder = 100;
        m_FogSystem = std::make_shared<Engine::MinimapFogSystem>();
        m_Minimap->fogSystem = m_FogSystem;

        // Bottom-Left: Wood/Ash Counters & Quick-Drop Hotkey Strip
        m_ResourcesLabel = makeLabel("HUD_Resources", Engine::UIAnchor::BottomLeft, { 0.0f, 0.0f },
                                     { 24.0f, 52.0f }, "Wood: 0   Ashes: 0", 0.85f, 101);
        m_HotkeyStripLabel = makeLabel(
            "HUD_Hotkeys", Engine::UIAnchor::BottomLeft, { 0.0f, 0.0f }, { 24.0f, 22.0f },
            "[1] Barricade 4W  [2] Flare 10W  [3] Thumper 15W  [4] Siphon 20W  [E] Stoke 2W", 0.68f, 101);

        // Center Overlay: Radial Drop Wheel & World/Center Interaction Prompt
        auto* wheelGo = scene->CreateGameObject("HUD_RadialWheel");
        wheelGo->GetTransform()->SetParent(go->GetTransform(), false);
        m_RadialWheel = wheelGo->AddComponent<Engine::UIRadialWheelComponent>();
        m_RadialWheel->useRectTransform = true;
        m_RadialWheel->rectTransform.Anchor = Engine::UIAnchor::Center;
        m_RadialWheel->rectTransform.Pivot = { 0.5f, 0.5f };
        m_RadialWheel->rectTransform.Size = { 320.0f, 320.0f };
        m_RadialWheel->rectTransform.ZOrder = 150;

        m_PromptLabel = makeLabel("HUD_Prompt", Engine::UIAnchor::BottomCenter, { 0.5f, 0.0f },
                                  { 0.0f, 96.0f }, "", 0.85f, 102);
    }

    [[nodiscard]] static const char* PhaseToString(NightPhase phase) noexcept {
        switch (phase) {
            case NightPhase::Dusk:          return "Dusk";
            case NightPhase::DeepNight:     return "Deep Night";
            case NightPhase::UmbralSurge:   return "Umbral Surge";
            case NightPhase::EclipseClimax: return "Eclipse Climax";
            case NightPhase::DawnBreak:     return "Dawn Break";
            default:                        return "Dusk";
        }
    }

    [[nodiscard]] static TorchComponent* ResolveTorch(Engine::GameObject* player) noexcept {
        if (!player) return nullptr;
        for (Engine::Transform* child : player->GetTransform()->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* t = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    return t;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] static WaveDirectorComponent* FindWaveDirector(Engine::Scene& scene) noexcept {
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (goPtr && !goPtr->IsPendingDestroy()) {
                if (auto* wd = goPtr->GetComponent<WaveDirectorComponent>()) {
                    return wd;
                }
            }
        }
        return nullptr;
    }

    bool m_Built = false;
    float m_PulseTime = 0.0f;
    Engine::UIPanel* m_HealthBar = nullptr;
    Engine::UILabel* m_HealthLabel = nullptr;
    Engine::UIPanel* m_TorchBar = nullptr;
    Engine::UILabel* m_TorchLabel = nullptr;
    Engine::UIPanel* m_RuneCooldownPanel = nullptr;
    Engine::UILabel* m_RuneLabel = nullptr;
    Engine::UILabel* m_ClockLabel = nullptr;
    Engine::UILabel* m_PhaseLabel = nullptr;
    Engine::UIPanel* m_XPBar = nullptr;
    Engine::UILabel* m_XPLabel = nullptr;
    Engine::UIPanel* m_BossBar = nullptr;
    Engine::UILabel* m_BossLabel = nullptr;
    Engine::UIMinimapComponent* m_Minimap = nullptr;
    std::shared_ptr<Engine::MinimapFogSystem> m_FogSystem;
    Engine::UILabel* m_ResourcesLabel = nullptr;
    Engine::UILabel* m_HotkeyStripLabel = nullptr;
    Engine::UIRadialWheelComponent* m_RadialWheel = nullptr;
    Engine::UILabel* m_PromptLabel = nullptr;
};

#endif // SANDBOX_ALPHA_HUD_CONTROLLER_H
