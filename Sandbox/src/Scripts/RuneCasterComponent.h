#ifndef SANDBOX_RUNE_CASTER_COMPONENT_H
#define SANDBOX_RUNE_CASTER_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/RuneStrategies.h"
#include "Scripts/TorchComponent.h"

#include <Engine/Core/Input.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

class RuneCasterComponent final : public Engine::Component {
public:
    void Awake() override {
        Engine::Input::BindAction("CastRune", {
            .PrimaryKey = SDL_SCANCODE_Q,
            .SecondaryKey = SDL_SCANCODE_UNKNOWN,
            .MouseButton = SDL_BUTTON_RIGHT,
            .PadButton = SDL_GAMEPAD_BUTTON_INVALID,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER
        });

        EquipRune(m_EquippedRune.Id.empty() ? "rune_ignite" : m_EquippedRune.Id);
    }

    bool EquipRune(const std::string& runeId) {
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Runes().Find(runeId)) {
                m_EquippedRune = *def;
                return true;
            }
        }
        return Sandbox::RuneRegistry::TryGetFallbackDefinition(runeId, m_EquippedRune);
    }

    [[nodiscard]] const Engine::RuneDefinition& GetEquippedRune() const noexcept { return m_EquippedRune; }
    [[nodiscard]] float GetCooldownRemaining() const noexcept { return m_CooldownTimer; }

    bool TryCastRune() {
        if (m_CooldownTimer > 0.0f) {
            return false;
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* torch = ResolveTorch();
        if (!go || !scene || !torch) {
            return false;
        }
        if (torch->Fuel < m_EquippedRune.FuelCost) {
            return false;
        }

        float cooldownMult = 1.0f;
        if (auto* pStats = go->GetComponent<PlayerStatsComponent>()) {
            if (std::isfinite(pStats->RuneCooldownMultiplier) && pStats->RuneCooldownMultiplier > 0.0f) {
                cooldownMult = pStats->RuneCooldownMultiplier;
            }
        }
        torch->Fuel = std::max(0.0f, torch->Fuel - m_EquippedRune.FuelCost);
        m_CooldownTimer = m_EquippedRune.CooldownSec * cooldownMult;

        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        if (const auto* strategy = Sandbox::RuneRegistry::FindStrategy(m_EquippedRune.Id)) {
            const Sandbox::RuneCastContext ctx{ *scene, *go, m_EquippedRune, origin };
            strategy->Cast(ctx);
        }
        Engine::SoundPlayParams runeParams;
        runeParams.Bus = Engine::AudioBus::SFX;
        runeParams.Volume = 0.95f;
        runeParams.SpatializedXZ = true;
        runeParams.WorldPos = origin;
        Sandbox::PlaySfxCue("assets/audio/sfx_rune_cast.wav", runeParams);
        return true;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (m_CooldownTimer > 0.0f) {
            m_CooldownTimer = std::max(0.0f, m_CooldownTimer - deltaTime);
        }
        if (Engine::Input::IsActionJustPressed("CastRune")) {
            TryCastRune();
        }
    }

private:
    [[nodiscard]] TorchComponent* ResolveTorch() const noexcept {
        auto* go = GetGameObject();
        if (!go) {
            return nullptr;
        }
        if (auto* selfTorch = go->GetComponent<TorchComponent>()) {
            return selfTorch;
        }
        for (Engine::Transform* child : go->GetTransform()->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* childTorch = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    return childTorch;
                }
            }
        }
        return nullptr;
    }

    Engine::RuneDefinition m_EquippedRune{ "rune_ignite", "Rune of Ignition", "", 15.0f, 4.0f, 5.0f, 3.5f, "" };
    float m_CooldownTimer = 0.0f;
};

#endif // SANDBOX_RUNE_CASTER_COMPONENT_H
