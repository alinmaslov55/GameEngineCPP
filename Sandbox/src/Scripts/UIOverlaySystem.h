#ifndef SANDBOX_UI_OVERLAY_SYSTEM_H
#define SANDBOX_UI_OVERLAY_SYSTEM_H

#include "Scripts/RunSessionManager.h"
#include "Scripts/UIWidgetBuilder.h"

#include <Engine/Core/Application.h>
#include <Engine/Core/Time.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Data/SaveManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/UI/UIButtonComponent.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Sandbox::UI {

/// @brief Ref-counted simulation pause stack preventing overlapping overlays from clobbering TimeScale.
class SimulationPauseStack {
public:
    static void Acquire() noexcept {
        if (s_Depth == 0) {
            s_SavedBaseTimeScale = Engine::Time::GetBaseTimeScale();
            Engine::Time::SetTimeScale(0.0f);
        }
        ++s_Depth;
    }

    static void Release() noexcept {
        if (s_Depth <= 0) {
            return;
        }
        --s_Depth;
        if (s_Depth == 0) {
            Engine::Time::SetTimeScale(s_SavedBaseTimeScale > 0.0f ? s_SavedBaseTimeScale : 1.0f);
        }
    }

    static void Reset() noexcept {
        if (s_Depth > 0) {
            Engine::Time::SetTimeScale(s_SavedBaseTimeScale > 0.0f ? s_SavedBaseTimeScale : 1.0f);
        }
        s_Depth = 0;
    }

    [[nodiscard]] static int GetDepth() noexcept { return s_Depth; }

private:
    inline static int s_Depth = 0;
    inline static float s_SavedBaseTimeScale = 1.0f;
};

/// @brief Non-Component modal UI overlay interface (0 ComponentMask128 bits).
class IUIOverlay {
public:
    virtual ~IUIOverlay() = default;
    virtual void Open(Engine::Scene& scene) = 0;
    virtual void Close(Engine::Scene* scene) = 0;
    [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
    [[nodiscard]] virtual bool PausesSimulation() const noexcept { return false; }
};

/// @brief Title & Settings menu overlay for Sanctuary Hub.
class TitleMenuOverlay final : public IUIOverlay {
public:
    std::function<void()> OnQuitRequested;

    void Open(Engine::Scene& scene) override {
        if (m_Built) {
            return;
        }
        m_Built = true;
        m_Open = true;

        m_Root = scene.CreateGameObject("TitleOverlayRoot");
        WidgetBuilder::MakePanel(*m_Root, Engine::UIAnchor::Center, { 0.0f, 0.0f }, { 520.0f, 360.0f }, 220, { 0.06f, 0.05f, 0.10f, 0.90f });
        WidgetBuilder::MakeLabel(*m_Root, "CULT OF THE LANTERN", 1.2f);

        WidgetBuilder::MakeButton(scene, "Btn_Play", m_Root, { 0.0f, 40.0f }, { 260.0f, 44.0f }, 221,
                                  { 0.24f, 0.20f, 0.30f, 0.95f }, "Play / Continue", 0.85f,
                                  [this]() { Dismiss(); });
        WidgetBuilder::MakeButton(scene, "Btn_Settings", m_Root, { 0.0f, -16.0f }, { 260.0f, 44.0f }, 221,
                                  { 0.24f, 0.20f, 0.30f, 0.95f }, "Settings", 0.85f,
                                  [this]() { ToggleSettings(); });
        WidgetBuilder::MakeButton(scene, "Btn_Quit", m_Root, { 0.0f, -72.0f }, { 260.0f, 44.0f }, 221,
                                  { 0.24f, 0.20f, 0.30f, 0.95f }, "Quit", 0.85f,
                                  [this]() {
                                      if (OnQuitRequested) {
                                          OnQuitRequested();
                                      } else {
                                          Engine::Application::Get().Close();
                                      }
                                  });

        m_SettingsPanel = scene.CreateGameObject("TitleSettingsPanel");
        m_SettingsPanel->GetTransform()->SetParent(m_Root->GetTransform(), false);
        WidgetBuilder::MakePanel(*m_SettingsPanel, Engine::UIAnchor::Center, { 0.0f, -145.0f }, { 340.0f, 70.0f }, 222, { 0.12f, 0.10f, 0.18f, 0.95f });
        m_SettingsPanel->SetActive(false);
    }

    void Close(Engine::Scene* scene) override {
        m_Open = false;
        m_SettingsOpen = false;
        if (m_Root) {
            Engine::Scene* targetScene = scene ? scene : m_Root->GetScene();
            if (targetScene) {
                targetScene->DestroyGameObject(m_Root);
            }
            m_Root = nullptr;
            m_SettingsPanel = nullptr;
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept override { return m_Open; }
    [[nodiscard]] bool IsSettingsOpen() const noexcept { return m_SettingsOpen; }

    void Dismiss() {
        m_Open = false;
        if (m_Root) {
            m_Root->SetActive(false);
        }
    }

    void ToggleSettings() {
        m_SettingsOpen = !m_SettingsOpen;
        if (m_SettingsPanel) {
            m_SettingsPanel->SetActive(m_SettingsOpen);
        }
    }

    void ApplySettings(float masterVol, float sfxVol, float musicVol, bool fullscreen, bool vsync) {
        auto& session = RunSessionManager::Get();
        session.Profile.BusVolumes[0] = std::clamp(masterVol, 0.0f, 1.0f);
        session.Profile.BusVolumes[1] = std::clamp(musicVol, 0.0f, 1.0f);
        session.Profile.BusVolumes[2] = std::clamp(sfxVol, 0.0f, 1.0f);
        session.Profile.Fullscreen = fullscreen;
        session.Profile.VSync = vsync;
        Engine::SaveManager::ApplyAudioAndVideoSettings(session.Profile);
        session.SaveProfile();
    }

private:
    bool m_Built = false;
    bool m_Open = true;
    bool m_SettingsOpen = false;
    Engine::GameObject* m_Root = nullptr;
    Engine::GameObject* m_SettingsPanel = nullptr;
};

/// @brief 3-Card Branch Draft modal overlay with ref-counted pause.
class BranchDraftOverlay final : public IUIOverlay {
public:
    std::vector<Engine::BranchDefinition> Cards;
    std::function<void(size_t)> OnCardSelected;

    void Open(Engine::Scene& scene) override {
        Close(&scene);
        m_Open = true;
        SimulationPauseStack::Acquire();

        m_Root = scene.CreateGameObject("BranchDraftOverlay");
        WidgetBuilder::MakePanel(*m_Root, Engine::UIAnchor::Center, { 0.0f, 0.0f }, { 1920.0f, 1080.0f }, 200, { 0.04f, 0.04f, 0.08f, 0.78f }, true);

        for (size_t i = 0; i < Cards.size(); ++i) {
            const auto& card = Cards[i];
            auto* btnGo = WidgetBuilder::MakeButton(
                scene,
                "BranchCard_" + std::to_string(i),
                m_Root,
                { (static_cast<float>(i) - 1.0f) * 250.0f, 0.0f },
                { 220.0f, 300.0f },
                201,
                RarityTint(card.Rarity),
                card.Name + " [" + card.Rarity + "]",
                1.0f,
                [this, i]() {
                    if (OnCardSelected) {
                        OnCardSelected(i);
                    }
                });
            m_CardButtonObjects.push_back(btnGo);
        }

        WidgetBuilder::WireHorizontalFocus(m_CardButtonObjects);
        Engine::UIButtonComponent::SetFocusedButton(nullptr);
    }

    void Close(Engine::Scene* scene) override {
        if (m_Open) {
            m_Open = false;
            SimulationPauseStack::Release();
        }
        m_CardButtonObjects.clear();
        if (m_Root) {
            Engine::Scene* targetScene = scene ? scene : m_Root->GetScene();
            if (targetScene) {
                targetScene->DestroyGameObject(m_Root);
            }
            m_Root = nullptr;
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept override { return m_Open; }
    [[nodiscard]] bool PausesSimulation() const noexcept override { return true; }
    [[nodiscard]] const std::vector<Engine::GameObject*>& GetCardButtons() const noexcept { return m_CardButtonObjects; }

private:
    [[nodiscard]] static glm::vec4 RarityTint(const std::string& rarity) noexcept {
        if (rarity == "Rare") return { 0.16f, 0.36f, 0.68f, 0.94f };
        if (rarity == "Epic") return { 0.48f, 0.18f, 0.68f, 0.94f };
        if (rarity == "Legendary") return { 0.85f, 0.62f, 0.12f, 0.96f };
        return { 0.24f, 0.24f, 0.28f, 0.92f };
    }

    bool m_Open = false;
    Engine::GameObject* m_Root = nullptr;
    std::vector<Engine::GameObject*> m_CardButtonObjects;
};

/// @brief End-of-run summary overlay with Retry / Return to Sanctuary actions.
class RunSummaryOverlay final : public IUIOverlay {
public:
    float SurvivalSec = 0.0f;
    uint32_t EnemiesSlain = 0;
    uint32_t DominoKills = 0;
    uint64_t AshesEarned = 0;
    bool Victory = false;
    std::function<void()> OnRetry;
    std::function<void()> OnReturnToSanctuary;

    void Open(Engine::Scene& scene) override {
        Close(&scene);
        m_Open = true;
        SimulationPauseStack::Acquire();

        m_Root = scene.CreateGameObject("RunSummaryOverlay");
        WidgetBuilder::MakePanel(*m_Root, Engine::UIAnchor::Center, { 0.0f, 0.0f }, { 460.0f, 320.0f }, 230, { 0.07f, 0.06f, 0.11f, 0.94f });

        char buf[192];
        const int mins = static_cast<int>(SurvivalSec) / 60;
        const int secs = static_cast<int>(SurvivalSec) % 60;
        std::snprintf(buf, sizeof(buf),
                      "%s | Time %02d:%02d | Slain %u | Domino %u | Ashes +%llu",
                      Victory ? "DAWN BREAK VICTORY" : "EXTINGUISHED",
                      mins, secs, EnemiesSlain, DominoKills, static_cast<unsigned long long>(AshesEarned));
        m_SummaryLabel = WidgetBuilder::MakeLabel(*m_Root, buf, 0.85f);

        WidgetBuilder::MakeButton(scene, "Btn_RetryRun", m_Root, { -105.0f, -90.0f }, { 180.0f, 42.0f }, 231,
                                  { 0.28f, 0.22f, 0.34f, 0.95f }, "Retry Run", 0.80f,
                                  [this]() {
                                      if (OnRetry) {
                                          OnRetry();
                                      }
                                  });
        WidgetBuilder::MakeButton(scene, "Btn_ReturnSanctuary", m_Root, { 105.0f, -90.0f }, { 180.0f, 42.0f }, 231,
                                  { 0.28f, 0.22f, 0.34f, 0.95f }, "Return to Sanctuary", 0.80f,
                                  [this]() {
                                      if (OnReturnToSanctuary) {
                                          OnReturnToSanctuary();
                                      }
                                  });
    }

    void Close(Engine::Scene* scene) override {
        if (m_Open) {
            m_Open = false;
            SimulationPauseStack::Release();
        }
        if (m_Root) {
            Engine::Scene* targetScene = scene ? scene : m_Root->GetScene();
            if (targetScene) {
                targetScene->DestroyGameObject(m_Root);
            }
            m_Root = nullptr;
            m_SummaryLabel = nullptr;
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept override { return m_Open; }
    [[nodiscard]] bool PausesSimulation() const noexcept override { return true; }
    [[nodiscard]] Engine::UILabel* GetSummaryLabel() const noexcept { return m_SummaryLabel; }

private:
    bool m_Open = false;
    Engine::GameObject* m_Root = nullptr;
    Engine::UILabel* m_SummaryLabel = nullptr;
};

} // namespace Sandbox::UI

#endif // SANDBOX_UI_OVERLAY_SYSTEM_H
