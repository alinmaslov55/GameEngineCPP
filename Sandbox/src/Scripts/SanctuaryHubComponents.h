#ifndef SANDBOX_SANCTUARY_HUB_COMPONENTS_H
#define SANDBOX_SANCTUARY_HUB_COMPONENTS_H

#include "Scripts/InteractableBehavior.h"
#include "Scripts/RunSessionManager.h"
#include "Scripts/UIOverlaySystem.h"

#include <Engine/Core/Input.h>
#include <Engine/Data/SaveManager.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>

enum class SanctuaryStationKind : uint8_t {
    WeaponRack = 0,
    RunePedestal = 1,
    RootAltar = 2,
    UmbralGate = 3,
    TitleOverlay = 4
};

class SanctuaryStationComponent;

namespace Sandbox {

struct NearestInteractableResult {
    SanctuaryStationComponent* Station = nullptr;
    const char* PromptText = "";
    float DistanceSq = std::numeric_limits<float>::max();
};

class PlayerInteractionSensor {
public:
    [[nodiscard]] static NearestInteractableResult FindNearest(Engine::Scene& scene,
                                                               const glm::vec2& playerPos,
                                                               Engine::GameObject* player = nullptr);
};

} // namespace Sandbox

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
        EnsureBehavior();
    }

    void Start() override {
        if (Kind == SanctuaryStationKind::TitleOverlay) {
            EnsureTitleOpened();
        } else {
            EnsureBehavior();
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_TitleOverlay.IsOpen(); }
    [[nodiscard]] bool IsSettingsOpen() const noexcept { return m_TitleOverlay.IsSettingsOpen(); }
    [[nodiscard]] bool IsPlayerInRange() const noexcept { return m_PlayerInRange; }

    [[nodiscard]] const char* GetPromptText() {
        EnsureBehavior();
        return m_Behavior ? m_Behavior->GetPromptText() : "";
    }

    [[nodiscard]] bool CanInteract(Engine::Scene& scene, Engine::GameObject& player) {
        if (Kind == SanctuaryStationKind::TitleOverlay) {
            return false;
        }
        EnsureBehavior();
        return m_Behavior ? m_Behavior->CanInteract(scene, player) : false;
    }

    void DismissTitle() {
        m_TitleOverlay.Dismiss();
    }

    void ToggleSettings() {
        m_TitleOverlay.ToggleSettings();
    }

    void ApplySettings(float masterVol, float sfxVol, float musicVol, bool fullscreen, bool vsync) {
        m_TitleOverlay.ApplySettings(masterVol, sfxVol, musicVol, fullscreen, vsync);
    }

    bool Interact() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* player = scene ? scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player) : nullptr;
        if (!scene || !player || Kind == SanctuaryStationKind::TitleOverlay) {
            return false;
        }
        EnsureBehavior();
        return m_Behavior ? m_Behavior->Interact(*scene, *player) : false;
    }

    void Update(float /*deltaTime*/) override {
        if (Kind == SanctuaryStationKind::TitleOverlay) {
            EnsureTitleOpened();
            return;
        }

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* player = scene ? scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player) : nullptr;
        if (!go || !scene || !player) {
            m_PlayerInRange = false;
            return;
        }

        const glm::vec3 playerPos3 = player->GetTransform()->GetWorldPosition();
        const auto nearest = Sandbox::PlayerInteractionSensor::FindNearest(
            *scene, glm::vec2(playerPos3.x, playerPos3.y), player);
        m_PlayerInRange = (nearest.Station == this);

        if (m_PlayerInRange && Engine::Input::IsActionJustPressed("Interact")) {
            Interact();
        }
    }

    void OnDestroy() override {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        m_TitleOverlay.Close(scene);
        m_TitleBuilt = false;
    }

private:
    void EnsureBehavior() {
        if (m_Behavior && m_BuiltKind == Kind) {
            return;
        }
        m_BuiltKind = Kind;
        switch (Kind) {
        case SanctuaryStationKind::WeaponRack:
            m_Behavior = std::make_unique<Sandbox::WeaponRackBehavior>();
            break;
        case SanctuaryStationKind::RunePedestal:
            m_Behavior = std::make_unique<Sandbox::RunePedestalBehavior>();
            break;
        case SanctuaryStationKind::RootAltar:
            m_Behavior = std::make_unique<Sandbox::RootAltarBehavior>(&ActiveRootId);
            break;
        case SanctuaryStationKind::UmbralGate:
            m_Behavior = std::make_unique<Sandbox::UmbralGateBehavior>();
            break;
        case SanctuaryStationKind::TitleOverlay:
            m_Behavior.reset();
            break;
        }
    }

    void EnsureTitleOpened() {
        if (m_TitleBuilt) {
            return;
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }
        m_TitleBuilt = true;
        m_TitleOverlay.OnQuitRequested = OnQuitRequested;
        m_TitleOverlay.Open(*scene);
    }

    SanctuaryStationKind m_BuiltKind = SanctuaryStationKind::WeaponRack;
    std::unique_ptr<Sandbox::IInteractableBehavior> m_Behavior;
    Sandbox::UI::TitleMenuOverlay m_TitleOverlay;
    bool m_TitleBuilt = false;
    bool m_PlayerInRange = false;
};

using TitleOverlayComponent = SanctuaryStationComponent;

namespace Sandbox {

inline NearestInteractableResult PlayerInteractionSensor::FindNearest(Engine::Scene& scene,
                                                                      const glm::vec2& playerPos,
                                                                      Engine::GameObject* player) {
    NearestInteractableResult best;
    if (!player) {
        player = scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player);
    }

    for (const auto& obj : scene.GetGameObjects()) {
        if (!obj || !obj->IsActive()) {
            continue;
        }
        auto* station = obj->GetComponent<SanctuaryStationComponent>();
        if (!station || !station->IsEnabled() || station->Kind == SanctuaryStationKind::TitleOverlay) {
            continue;
        }
        if (player && !station->CanInteract(scene, *player)) {
            continue;
        }
        const glm::vec3 pos3 = obj->GetTransform()->GetWorldPosition();
        const glm::vec2 diff(pos3.x - playerPos.x, pos3.y - playerPos.y);
        const float distSq = glm::dot(diff, diff);
        const float radiusSq = station->InteractRadius * station->InteractRadius;
        if (distSq <= radiusSq && distSq < best.DistanceSq) {
            best.DistanceSq = distSq;
            best.Station = station;
            best.PromptText = station->GetPromptText();
        }
    }
    return best;
}

} // namespace Sandbox

#endif // SANDBOX_SANCTUARY_HUB_COMPONENTS_H
