#ifndef SANDBOX_INTERACTABLE_BEHAVIOR_H
#define SANDBOX_INTERACTABLE_BEHAVIOR_H

#include "Scripts/RuneCasterComponent.h"
#include "Scripts/RunSessionManager.h"
#include "Scripts/RunTransitionService.h"
#include "Scripts/WeaponController.h"

#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Transform.h>

#include <array>
#include <limits>
#include <string>

namespace Sandbox {

/// @brief Non-Component strategy interface for world interactables (0 ComponentMask128 bits).
class IInteractableBehavior {
public:
    virtual ~IInteractableBehavior() = default;
    [[nodiscard]] virtual const char* GetPromptText() const noexcept = 0;
    [[nodiscard]] virtual bool CanInteract(Engine::Scene& /*scene*/, Engine::GameObject& /*player*/) const {
        return true;
    }
    virtual bool Interact(Engine::Scene& scene, Engine::GameObject& player) = 0;
};

class WeaponRackBehavior final : public IInteractableBehavior {
public:
    [[nodiscard]] const char* GetPromptText() const noexcept override {
        return "[E] Swap Weapon";
    }

    bool Interact(Engine::Scene& /*scene*/, Engine::GameObject& player) override {
        static constexpr std::array<const char*, 3> kWeapons = {
            "weapon_maul_t1", "weapon_halberd_t1", "weapon_sickle_t1"
        };
        auto& session = RunSessionManager::Get();
        m_Index = (m_Index + 1u) % kWeapons.size();
        session.SelectedWeaponId = kWeapons[m_Index];
        if (auto* wc = player.GetComponent<WeaponController>()) {
            wc->EquipWeapon(session.SelectedWeaponId);
        }
        return true;
    }

private:
    size_t m_Index = 0;
};

class RunePedestalBehavior final : public IInteractableBehavior {
public:
    [[nodiscard]] const char* GetPromptText() const noexcept override {
        return "[E] Swap Rune";
    }

    bool Interact(Engine::Scene& /*scene*/, Engine::GameObject& player) override {
        static constexpr std::array<const char*, 4> kRunes = {
            "rune_ignite", "rune_chronos", "rune_shockwave", "rune_consecrate"
        };
        auto& session = RunSessionManager::Get();
        m_Index = (m_Index + 1u) % kRunes.size();
        session.SelectedRuneId = kRunes[m_Index];
        if (auto* rc = player.GetComponent<RuneCasterComponent>()) {
            rc->EquipRune(session.SelectedRuneId);
        }
        return true;
    }

private:
    size_t m_Index = 0;
};

class RootAltarBehavior final : public IInteractableBehavior {
public:
    explicit RootAltarBehavior(std::string* activeRootId) : m_ActiveRootId(activeRootId) {}

    [[nodiscard]] const char* GetPromptText() const noexcept override {
        return "[E] Upgrade Root";
    }

    bool Interact(Engine::Scene& /*scene*/, Engine::GameObject& /*player*/) override {
        if (!m_ActiveRootId) {
            return false;
        }
        return RunSessionManager::Get().PurchaseRootRank(*m_ActiveRootId);
    }

private:
    std::string* m_ActiveRootId = nullptr;
};

class UmbralGateBehavior final : public IInteractableBehavior {
public:
    [[nodiscard]] const char* GetPromptText() const noexcept override {
        return "[E] Enter the Dark";
    }

    bool Interact(Engine::Scene& /*scene*/, Engine::GameObject& /*player*/) override {
        return RunTransitionService::StartNewRun(0.45f);
    }
};

} // namespace Sandbox

#endif // SANDBOX_INTERACTABLE_BEHAVIOR_H
