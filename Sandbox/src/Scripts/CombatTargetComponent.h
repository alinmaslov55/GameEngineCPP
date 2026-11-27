#ifndef SANDBOX_COMBAT_TARGET_COMPONENT_H
#define SANDBOX_COMBAT_TARGET_COMPONENT_H

#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <functional>

class CombatTargetComponent final : public Engine::Component {
public:
    float MaxHealth = 100.0f;
    float Health = 100.0f;
    float StunTimer = 0.0f;
    float BurnTimer = 0.0f;
    float BurnDPS = 0.0f;
    float FlashTimer = 0.0f;
    bool IsDead = false;
    bool DestroyOnDeath = false;

    std::function<void(float amount)> OnDamaged;
    std::function<void()> OnKilled;

    void ResetHealth(float maxHp) noexcept {
        MaxHealth = std::max(1.0f, maxHp);
        Health = MaxHealth;
        StunTimer = 0.0f;
        BurnTimer = 0.0f;
        BurnDPS = 0.0f;
        FlashTimer = 0.0f;
        IsDead = false;
    }

    bool ApplyDamage(float amount,
                     float stunDuration = 0.0f,
                     Engine::SpriteRenderer::FlashMode flashMode = Engine::SpriteRenderer::FlashMode::PureWhite) {
        if (IsDead || !std::isfinite(amount) || amount <= 0.0f) {
            return false;
        }

        Health = std::max(0.0f, Health - amount);
        if (std::isfinite(stunDuration) && stunDuration > 0.0f) {
            StunTimer = std::max(StunTimer, stunDuration);
        }

        if (auto* go = GetGameObject()) {
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                sr->SetFlashMode(flashMode);
                sr->SetFlashAmount(1.0f);
                FlashTimer = 0.12f;
            }
            if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                ss->TriggerImpulse(-2.2f, 8.0f);
            }
        }

        if (OnDamaged) {
            OnDamaged(amount);
        }

        if (Health <= 0.0f && !IsDead) {
            IsDead = true;
            if (OnKilled) {
                OnKilled();
            }
            if (DestroyOnDeath) {
                if (auto* go = GetGameObject()) {
                    if (auto* scene = go->GetScene()) {
                        scene->DestroyGameObject(go);
                    }
                }
            }
        }
        return true;
    }

    void ApplyBurn(float durationSec, float dps) noexcept {
        if (!std::isfinite(durationSec) || durationSec <= 0.0f || !std::isfinite(dps) || dps <= 0.0f) {
            return;
        }
        BurnTimer = std::max(BurnTimer, durationSec);
        BurnDPS = std::max(BurnDPS, dps);
    }

    [[nodiscard]] bool IsAlive() const noexcept { return !IsDead && Health > 0.0f; }
    [[nodiscard]] bool IsStunned() const noexcept { return StunTimer > 0.0f; }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }

        if (StunTimer > 0.0f) {
            StunTimer = std::max(0.0f, StunTimer - deltaTime);
        }

        if (FlashTimer > 0.0f) {
            FlashTimer = std::max(0.0f, FlashTimer - deltaTime);
            if (FlashTimer <= 0.0f) {
                if (auto* go = GetGameObject()) {
                    if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                        sr->SetFlashAmount(0.0f);
                    }
                }
            }
        }

        if (BurnTimer > 0.0f && !IsDead) {
            const float tickDt = std::min(deltaTime, BurnTimer);
            BurnTimer = std::max(0.0f, BurnTimer - deltaTime);
            if (BurnDPS > 0.0f) {
                ApplyDamage(BurnDPS * tickDt, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
            }
        }
    }
};

#endif // SANDBOX_COMBAT_TARGET_COMPONENT_H
