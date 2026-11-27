#ifndef SANDBOX_WEAPON_CONTROLLER_H
#define SANDBOX_WEAPON_CONTROLLER_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/WeaponArchetypes.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/Time.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Physics/PhysicsQueries.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

class WeaponController final : public Engine::Component {
public:
    enum class SwingPhase : uint8_t {
        Idle = 0,
        Windup,
        ActiveCleave,
        Recovery
    };

    std::string InitialWeaponId = "weapon_maul_t1";

    void Awake() override {
        Engine::Input::BindAction("Attack", {
            .PrimaryKey = SDL_SCANCODE_J,
            .SecondaryKey = SDL_SCANCODE_UNKNOWN,
            .MouseButton = SDL_BUTTON_LEFT,
            .PadButton = SDL_GAMEPAD_BUTTON_WEST,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID
        });
        EquipWeapon(InitialWeaponId);
    }

    bool EquipWeapon(std::string_view weaponId) {
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::FindWeapon(weaponId)) {
                m_Weapon = *def;
                m_FrenzyStacks = 0;
                return true;
            }
        }
        // Fallback defaults matching weapons.json weapon_maul_t1 if registry isn't loaded
        m_Weapon = Engine::WeaponDefinition{};
        m_Weapon.Id = std::string(weaponId);
        m_Weapon.WeaponType = "Maul";
        m_Weapon.Tier = 1;
        m_Weapon.Name = "Rough Iron Maul";
        m_Weapon.Damage = 35.0f;
        m_Weapon.SwingArcDeg = 130.0f;
        m_Weapon.Reach = 2.2f;
        m_Weapon.KnockbackImpulse = 22.0f;
        m_Weapon.AttackRate = 0.9f;
        return false;
    }

    [[nodiscard]] const Engine::WeaponDefinition& GetEquippedWeapon() const noexcept { return m_Weapon; }
    [[nodiscard]] SwingPhase GetSwingPhase() const noexcept { return m_Phase; }
    [[nodiscard]] bool IsAttackBuffered() const noexcept { return m_AttackBuffered; }
    [[nodiscard]] uint32_t GetLastHitCount() const noexcept { return m_LastHitCount; }
    [[nodiscard]] int GetFrenzyStacks() const noexcept { return m_FrenzyStacks; }

    void AddSwingArcBonus(float extraDeg) noexcept {
        if (std::isfinite(extraDeg) && extraDeg > 0.0f) {
            m_Weapon.SwingArcDeg = std::min(360.0f, m_Weapon.SwingArcDeg + extraDeg);
        }
    }

    void SetAimOverrideXZ(const glm::vec2& aimXZ) noexcept {
        m_HasAimOverride = true;
        m_AimOverrideXZ = aimXZ;
    }
    void ClearAimOverrideXZ() noexcept { m_HasAimOverride = false; }

    bool RequestAttack() {
        if (m_Phase == SwingPhase::Idle) {
            StartSwing();
            return true;
        }
        m_AttackBuffered = true;
        return false;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }

        if (Engine::Input::IsActionJustPressed("Attack")) {
            RequestAttack();
        }

        if (m_FrenzyDecayTimer > 0.0f) {
            m_FrenzyDecayTimer = std::max(0.0f, m_FrenzyDecayTimer - deltaTime);
            if (m_FrenzyDecayTimer <= 0.0f) {
                m_FrenzyStacks = 0;
            }
        }

        if (m_Phase == SwingPhase::Idle) {
            return;
        }

        m_PhaseTimer -= deltaTime;
        if (m_Phase == SwingPhase::Windup && m_PhaseTimer <= 0.0f) {
            m_Phase = SwingPhase::ActiveCleave;
            ExecuteActiveCleave();
            m_Phase = SwingPhase::Recovery;
            m_PhaseTimer = m_RecoveryDuration;
        } else if (m_Phase == SwingPhase::Recovery && m_PhaseTimer <= 0.0f) {
            m_Phase = SwingPhase::Idle;
            m_PhaseTimer = 0.0f;
            if (m_AttackBuffered) {
                m_AttackBuffered = false;
                StartSwing();
            }
        }
    }

private:
    void StartSwing() {
        float statsAttackMult = 1.0f;
        if (auto* go = GetGameObject()) {
            if (auto* stats = go->GetComponent<PlayerStatsComponent>()) {
                const float effectiveAtk = stats->GetEffectiveAttackSpeedMultiplier();
                if (std::isfinite(effectiveAtk) && effectiveAtk > 0.0f) {
                    statsAttackMult = effectiveAtk;
                }
            }
        }
        const float frenzyMult = (1.0f + 0.05f * static_cast<float>(m_FrenzyStacks)) * statsAttackMult;
        const float effectiveRate = std::max(0.25f, m_Weapon.AttackRate * frenzyMult);
        const float cycleSec = 1.0f / effectiveRate;

        const auto& cfg = Sandbox::WeaponArchetypeRegistry::Resolve(m_Weapon.WeaponType).GetConfig();
        const float windupFrac = cfg.WindupFraction;

        m_WindupDuration = cycleSec * windupFrac;
        m_RecoveryDuration = std::max(0.05f, cycleSec * (1.0f - windupFrac));
        m_Phase = SwingPhase::Windup;
        m_PhaseTimer = m_WindupDuration;
        m_AttackBuffered = false;

        if (auto* go = GetGameObject()) {
            if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                ss->TriggerImpulse(-1.6f, 6.0f);
            }
            Engine::SoundPlayParams swingParams;
            swingParams.Bus = Engine::AudioBus::SFX;
            swingParams.Volume = 0.85f;
            swingParams.PitchMin = 0.92f;
            swingParams.PitchMax = 1.08f;
            swingParams.SpatializedXZ = true;
            swingParams.WorldPos = go->GetTransform()->GetWorldPosition();
            Sandbox::PlaySfxCue(cfg.SwingSfxPath, swingParams);
        }
    }

    void ExecuteActiveCleave() {
        m_LastHitCount = 0;
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* transform = go ? go->GetTransform() : nullptr;
        if (!go || !scene || !transform) {
            return;
        }

        const glm::vec3 worldPos = transform->GetWorldPosition();
        const glm::vec2 originXZ{ worldPos.x, worldPos.z };
        glm::vec2 aimXZ{ 1.0f, 0.0f };
        if (m_HasAimOverride) {
            aimXZ = m_AimOverrideXZ;
        } else if (auto* pc = go->GetComponent<PlayerController>()) {
            aimXZ = pc->GetAimDirectionXZ();
        }
        aimXZ = Sandbox::SafeNormalizeXZ(aimXZ);

        m_HitScratch.clear();
        const uint64_t hitMask = Engine::PhysicsLayer::Enemy |
                                 Engine::PhysicsLayer::Environment |
                                 Engine::PhysicsLayer::LowObstacle;
        Engine::PhysicsQueries::OverlapArcXZ(
            *scene, originXZ, aimXZ, m_Weapon.Reach, m_Weapon.SwingArcDeg * 0.5f, hitMask, m_HitScratch);

        const auto& behavior = Sandbox::WeaponArchetypeRegistry::Resolve(m_Weapon.WeaponType);
        const auto& cfg = behavior.GetConfig();

        for (Engine::GameObject* targetGo : m_HitScratch) {
            if (!targetGo || targetGo == go || targetGo->IsPendingDestroy()) {
                continue;
            }
            ++m_LastHitCount;

            glm::vec2 pushDir = aimXZ;
            if (auto* t = targetGo->GetTransform()) {
                const glm::vec3 twp = t->GetWorldPosition();
                pushDir = Sandbox::SafeNormalizeXZ({ twp.x - originXZ.x, twp.z - originXZ.y }, aimXZ);
            }

            float totalDamage = m_Weapon.Damage;
            float totalKnockback = m_Weapon.KnockbackImpulse;
            float burnDPS = cfg.InnateBurnDPS;
            if (auto* pStats = go->GetComponent<PlayerStatsComponent>()) {
                totalDamage += pStats->BonusDamage;
                totalKnockback += pStats->BonusKnockbackImpulse;
                if (pStats->BurnDamageOnHit > 0.0f) {
                    burnDPS = std::max(burnDPS, pStats->BurnDamageOnHit);
                }
            }
            DamageEvent evt;
            evt.Amount = totalDamage;
            evt.Source = go;
            evt.ImpulseXZ = pushDir * totalKnockback;
            evt.StunDuration = cfg.StunSec;
            evt.IsFire = cfg.IsFire;
            StatusApplication status;
            if (burnDPS > 0.0f) {
                status.BurnDurationSec = 2.5f;
                status.BurnDPS = burnDPS;
            }
            Sandbox::CombatDamage::Apply(targetGo, evt, status, true);
            behavior.OnTargetHit(*targetGo);

            if (auto* bowling = targetGo->GetComponent<KineticBowlingComponent>()) {
                if (m_Weapon.KnockbackImpulse >= 12.0f) {
                    bowling->Launch(0.6f);
                }
            }
        }

        if (m_LastHitCount > 0u) {
            behavior.OnSwingConnected(m_FrenzyStacks, m_FrenzyDecayTimer);
            Engine::Time::TriggerHitStop(cfg.HitStopSec, 0.02f);

            if (auto* pc = go->GetComponent<PlayerController>()) {
                pc->TriggerImpactFeedback(cfg.Trauma, aimXZ * (cfg.Trauma * 0.6f));
            }
            if (auto* audio = go->GetComponent<Engine::AudioSource>()) {
                audio->Play();
            }
        }
    }

    Engine::WeaponDefinition m_Weapon;
    SwingPhase m_Phase = SwingPhase::Idle;
    float m_PhaseTimer = 0.0f;
    float m_WindupDuration = 0.15f;
    float m_RecoveryDuration = 0.35f;
    bool m_AttackBuffered = false;
    uint32_t m_LastHitCount = 0;
    int m_FrenzyStacks = 0;
    float m_FrenzyDecayTimer = 0.0f;
    bool m_HasAimOverride = false;
    glm::vec2 m_AimOverrideXZ{ 1.0f, 0.0f };
    std::vector<Engine::GameObject*> m_HitScratch;
};

#endif // SANDBOX_WEAPON_CONTROLLER_H
