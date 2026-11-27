#ifndef SANDBOX_SANCTUARY_HUB_COMPONENTS_H
#define SANDBOX_SANCTUARY_HUB_COMPONENTS_H

#include "Scene/AlphaRunScene.h"
#include "Scripts/RuneCasterComponent.h"
#include "Scripts/RunSessionManager.h"
#include "Scripts/WeaponController.h"

#include <Engine/Core/Application.h>
#include <Engine/Core/Input.h>
#include <Engine/Data/SaveManager.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Transform.h>
#include <Engine/UI/UIButtonComponent.h>
#include <Engine/UI/UICanvas.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

enum class SanctuaryStationKind : uint8_t {
    WeaponRack = 0,
    RunePedestal = 1,
    RootAltar = 2,
    UmbralGate = 3,
    TitleOverlay = 4
};

class SanctuaryStationComponent final : public Engine::Component {
public:
    SanctuaryStationKind Kind = SanctuaryStationKind::WeaponRack;
    float InteractRadius = 2.4f;
    std::string ActiveRootId = "root_iron_sinew";
    std::function<void()> OnQuitRequested;

    void Awake() override {
        Engine::Input::BindAction("Interact", {
            .PrimaryKey = SDL_SCANCODE_E,
            .SecondaryKey = SDL_SCANCODE_F,
            .MouseButton = 0,
            .PadButton = SDL_GAMEPAD_BUTTON_WEST,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID
        });
    }

    void Start() override {
        if (Kind == SanctuaryStationKind::TitleOverlay) {
            BuildTitleUI();
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_Open; }
    [[nodiscard]] bool IsSettingsOpen() const noexcept { return m_SettingsOpen; }
    [[nodiscard]] bool IsPlayerInRange() const noexcept { return m_PlayerInRange; }

    void DismissTitle() {
        m_Open = false;
        m_SettingsOpen = false;
        if (m_Root) {
            m_Root->SetActive(false);
        }
        if (m_SettingsPanel) {
            m_SettingsPanel->SetActive(false);
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

    bool Interact() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* player = scene ? scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player) : nullptr;
        if (!scene || !player) {
            return false;
        }

        auto& session = RunSessionManager::Get();
        switch (Kind) {
            case SanctuaryStationKind::WeaponRack: {
                static constexpr std::array<const char*, 3> kWeapons = {
                    "rough_iron_maul", "ashwood_halberd", "culling_sickle"
                };
                m_WeaponIndex = (m_WeaponIndex + 1u) % kWeapons.size();
                session.SelectedWeaponId = kWeapons[m_WeaponIndex];
                if (auto* wc = player->GetComponent<WeaponController>()) {
                    wc->EquipWeapon(session.SelectedWeaponId);
                }
                return true;
            }
            case SanctuaryStationKind::RunePedestal: {
                static constexpr std::array<const char*, 4> kRunes = {
                    "rune_ignite", "rune_chronos", "rune_shockwave", "rune_consecrate"
                };
                m_RuneIndex = (m_RuneIndex + 1u) % kRunes.size();
                session.SelectedRuneId = kRunes[m_RuneIndex];
                if (auto* rc = player->GetComponent<RuneCasterComponent>()) {
                    rc->EquipRune(session.SelectedRuneId);
                }
                return true;
            }
            case SanctuaryStationKind::RootAltar:
                return session.PurchaseRootRank(ActiveRootId);

            case SanctuaryStationKind::UmbralGate: {
                session.ResetRunTelemetry();
                const uint64_t runSeed = session.NextRunSeed++;
                Engine::SceneManager::TransitionToScene([runSeed]() {
                    auto runScene = Sandbox::AlphaRunScene::Create(runSeed);
                    Sandbox::AlphaRunScene::BindRuntimeGameplayScripts(runScene);
                    return runScene;
                }, 0.45f);
                return true;
            }
            case SanctuaryStationKind::TitleOverlay:
                DismissTitle();
                return true;
        }
        return false;
    }

    void Update(float /*deltaTime*/) override {
        if (Kind == SanctuaryStationKind::TitleOverlay) {
            if (!m_Built) {
                BuildTitleUI();
            }
            return;
        }

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* player = scene ? scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player) : nullptr;
        if (!go || !player) {
            m_PlayerInRange = false;
            return;
        }

        const glm::vec3 sPos = go->GetTransform()->GetWorldPosition();
        const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
        const float dx = pPos.x - sPos.x;
        const float dz = pPos.z - sPos.z;
        m_PlayerInRange = ((dx * dx + dz * dz) <= (InteractRadius * InteractRadius));

        if (m_PlayerInRange && Engine::Input::IsActionJustPressed("Interact")) {
            Interact();
        }
    }

private:
    void BuildTitleUI() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene || m_Built) {
            return;
        }
        m_Built = true;

        m_Root = scene->CreateGameObject("TitleOverlayRoot");
        auto* bg = m_Root->AddComponent<Engine::UIPanel>();
        bg->useRectTransform = true;
        bg->rectTransform.Anchor = Engine::UIAnchor::Center;
        bg->rectTransform.Pivot = { 0.5f, 0.5f };
        bg->rectTransform.Size = { 520.0f, 360.0f };
        bg->rectTransform.ZOrder = 220;
        bg->color = { 0.06f, 0.05f, 0.10f, 0.90f };

        auto* title = m_Root->AddComponent<Engine::UILabel>();
        title->text = "CULT OF THE LANTERN";
        title->scale = 1.2f;

        auto makeBtn = [&](const char* name, float yOffset, const std::string& label, std::function<void()> onClick) {
            auto* bGo = scene->CreateGameObject(name);
            bGo->GetTransform()->SetParent(m_Root->GetTransform(), false);
            auto* p = bGo->AddComponent<Engine::UIPanel>();
            p->useRectTransform = true;
            p->rectTransform.Anchor = Engine::UIAnchor::Center;
            p->rectTransform.Pivot = { 0.5f, 0.5f };
            p->rectTransform.AnchoredOffset = { 0.0f, yOffset };
            p->rectTransform.Size = { 260.0f, 44.0f };
            p->rectTransform.ZOrder = 221;
            p->color = { 0.24f, 0.20f, 0.30f, 0.95f };
            auto* l = bGo->AddComponent<Engine::UILabel>();
            l->text = label;
            l->scale = 0.85f;
            auto* btn = bGo->AddComponent<Engine::UIButtonComponent>();
            btn->OnClick = std::move(onClick);
            return bGo;
        };

        makeBtn("Btn_Play", 40.0f, "Play / Continue", [this]() { DismissTitle(); });
        makeBtn("Btn_Settings", -16.0f, "Settings", [this]() { ToggleSettings(); });
        makeBtn("Btn_Quit", -72.0f, "Quit", [this]() {
            if (OnQuitRequested) {
                OnQuitRequested();
            } else {
                Engine::Application::Get().Close();
            }
        });

        m_SettingsPanel = scene->CreateGameObject("TitleSettingsPanel");
        m_SettingsPanel->GetTransform()->SetParent(m_Root->GetTransform(), false);
        auto* sp = m_SettingsPanel->AddComponent<Engine::UIPanel>();
        sp->useRectTransform = true;
        sp->rectTransform.Anchor = Engine::UIAnchor::Center;
        sp->rectTransform.Pivot = { 0.5f, 0.5f };
        sp->rectTransform.AnchoredOffset = { 0.0f, -145.0f };
        sp->rectTransform.Size = { 340.0f, 70.0f };
        sp->rectTransform.ZOrder = 222;
        sp->color = { 0.12f, 0.10f, 0.18f, 0.95f };
        m_SettingsPanel->SetActive(false);
    }

    bool m_Built = false;
    bool m_Open = true;
    bool m_SettingsOpen = false;
    bool m_PlayerInRange = false;
    size_t m_WeaponIndex = 0;
    size_t m_RuneIndex = 0;
    Engine::GameObject* m_Root = nullptr;
    Engine::GameObject* m_SettingsPanel = nullptr;
};

using TitleOverlayComponent = SanctuaryStationComponent;

#endif // SANDBOX_SANCTUARY_HUB_COMPONENTS_H
