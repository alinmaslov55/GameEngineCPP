#ifndef SANDBOX_WEAPON_CONTROLLER_H
#define SANDBOX_WEAPON_CONTROLLER_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerController.h"

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
        const float frenzyMult = 1.0f + 0.05f * static_cast<float>(m_FrenzyStacks);
        const float effectiveRate = std::max(0.25f, m_Weapon.AttackRate * frenzyMult);
        const float cycleSec = 1.0f / effectiveRate;

        float windupFrac = 0.25f;
        if (m_Weapon.WeaponType == "Maul") {
            windupFrac = 0.28f;
        } else if (m_Weapon.WeaponType == "Halberd") {
            windupFrac = 0.20f;
        } else if (m_Weapon.WeaponType == "Sickle") {
            windupFrac = 0.15f;
        }

        m_WindupDuration = cycleSec * windupFrac;
        m_RecoveryDuration = std::max(0.05f, cycleSec * (1.0f - windupFrac));
        m_Phase = SwingPhase::Windup;
        m_PhaseTimer = m_WindupDuration;
        m_AttackBuffered = false;

        if (auto* go = GetGameObject()) {
            if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                ss->TriggerImpulse(-1.6f, 6.0f);
            }
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
        const float aimLen = glm::length(aimXZ);
        if (std::isfinite(aimLen) && aimLen > 1e-4f) {
            aimXZ /= aimLen;
        } else {
            aimXZ = { 1.0f, 0.0f };
        }

        m_HitScratch.clear();
        const uint64_t hitMask = Engine::PhysicsLayer::Enemy |
                                 Engine::PhysicsLayer::Environment |
                                 Engine::PhysicsLayer::LowObstacle;
        Engine::PhysicsQueries::OverlapArcXZ(
            *scene, originXZ, aimXZ, m_Weapon.Reach, m_Weapon.SwingArcDeg * 0.5f, hitMask, m_HitScratch);

        const bool isMaul = (m_Weapon.WeaponType == "Maul");
        const bool isHalberd = (m_Weapon.WeaponType == "Halberd");
        const bool isSickle = (m_Weapon.WeaponType == "Sickle");

        for (Engine::GameObject* targetGo : m_HitScratch) {
            if (!targetGo || targetGo == go || targetGo->IsPendingDestroy()) {
                continue;
            }
            ++m_LastHitCount;

            glm::vec2 pushDir = aimXZ;
            if (auto* t = targetGo->GetTransform()) {
                const glm::vec3 twp = t->GetWorldPosition();
                const glm::vec2 delta{ twp.x - originXZ.x, twp.z - originXZ.y };
                const float dLen = glm::length(delta);
                if (std::isfinite(dLen) && dLen > 1e-4f) {
                    pushDir = delta / dLen;
                }
            }

            const float stunSec = isHalberd ? 0.35f : (isMaul ? 0.25f : 0.10f);
            if (auto* health = targetGo->GetComponent<HealthComponent>()) {
                DamageEvent evt;
                evt.Amount = m_Weapon.Damage;
                evt.Source = go;
                evt.ImpulseXZ = pushDir * m_Weapon.KnockbackImpulse;
                evt.StunDuration = stunSec;
                evt.IsFire = isSickle;
                health->ApplyDamage(evt);
                if (isSickle) {
                    if (auto* status = targetGo->GetComponent<StatusEffectsComponent>()) {
                        status->ApplyBurn(2.5f, 4.0f);
                    }
                }
                if (isHalberd) {
                    if (auto* targetRb = targetGo->GetComponent<Engine::Rigidbody2D>();
                        targetRb && targetRb->VerticalVelocity > 0.0f) {
                        targetRb->VerticalVelocity = 0.0f;
                    }
                }
            } else {
                if (auto* target = targetGo->GetComponent<CombatTargetComponent>()) {
                    target->ApplyDamage(m_Weapon.Damage, stunSec, Engine::SpriteRenderer::FlashMode::PureWhite);
                    if (isSickle) {
                        target->ApplyBurn(2.5f, 4.0f);
                    }
                } else if (auto* sr = targetGo->GetComponent<Engine::SpriteRenderer>()) {
                    sr->SetFlashMode(Engine::SpriteRenderer::FlashMode::PureWhite);
                    sr->SetFlashAmount(1.0f);
                }

                if (auto* targetRb = targetGo->GetComponent<Engine::Rigidbody2D>()) {
                    targetRb->ApplyImpulseXZ(pushDir * m_Weapon.KnockbackImpulse);
                    if (isHalberd && targetRb->VerticalVelocity > 0.0f) {
                        // Interrupt hopping enemies (e.g. Jiangshi) mid-air
                        targetRb->VerticalVelocity = 0.0f;
                    }
                }
            }

            if (auto* bowling = targetGo->GetComponent<KineticBowlingComponent>()) {
                if (m_Weapon.KnockbackImpulse >= 12.0f) {
                    bowling->Launch(0.6f);
                }
            }
        }

        if (m_LastHitCount > 0u) {
            if (isSickle) {
                m_FrenzyStacks = std::min(5, m_FrenzyStacks + 1);
                m_FrenzyDecayTimer = 3.0f;
            }

            const float hitStopSec = isMaul ? 0.045f : (isHalberd ? 0.030f : 0.015f);
            const float trauma = isMaul ? 0.38f : (isHalberd ? 0.24f : 0.12f);
            Engine::Time::TriggerHitStop(hitStopSec, 0.02f);

            if (auto* pc = go->GetComponent<PlayerController>()) {
                pc->TriggerImpactFeedback(trauma, aimXZ * (trauma * 0.6f));
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
