#ifndef SANDBOX_STATUS_EFFECTS_COMPONENT_H
#define SANDBOX_STATUS_EFFECTS_COMPONENT_H

#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace Engine {
class GameObject;
}

struct DamageEvent {
    float Amount = 0.0f;
    Engine::GameObject* Source = nullptr;
    glm::vec2 ImpulseXZ{ 0.0f };
    float HitStopSec = 0.0f;
    float StunDuration = 0.0f;
    bool IsFire = false;
    bool IsRadiant = false;
    bool BypassInvuln = false;
};

class StatusEffectsComponent final : public Engine::Component {
public:
    float InvulnTimer = 0.0f;
    float BurnTimer = 0.0f;
    float BurnDPS = 0.0f;
    float BlindTimer = 0.0f;
    float StunTimer = 0.0f;
    float BurnSpreadRadius = 1.0f;
    float BurnSpreadInterval = 0.15f;

    void ResetAll() noexcept {
        InvulnTimer = 0.0f;
        BurnTimer = 0.0f;
        BurnDPS = 0.0f;
        BlindTimer = 0.0f;
        StunTimer = 0.0f;
        m_PendingBurnDamage = 0.0f;
    }

    void OnPoolAcquire() override { ResetAll(); }
    void OnPoolRelease() override { ResetAll(); }

    void SetInvulnerable(float durationSec) noexcept {
        if (std::isfinite(durationSec) && durationSec > 0.0f) {
            InvulnTimer = std::max(InvulnTimer, durationSec);
        }
    }

    void ApplyBurn(float durationSec, float dps) noexcept {
        if (std::isfinite(durationSec) && durationSec > 0.0f && std::isfinite(dps) && dps > 0.0f) {
            BurnTimer = std::max(BurnTimer, durationSec);
            BurnDPS = std::max(BurnDPS, dps);
        }
    }

    void ApplyBlind(float durationSec) noexcept {
        if (std::isfinite(durationSec) && durationSec > 0.0f) {
            BlindTimer = std::max(BlindTimer, durationSec);
        }
    }

    void ApplyStun(float durationSec) noexcept {
        if (std::isfinite(durationSec) && durationSec > 0.0f) {
            StunTimer = std::max(StunTimer, durationSec);
        }
    }

    [[nodiscard]] bool IsInvulnerable() const noexcept { return InvulnTimer > 0.0f; }
    [[nodiscard]] bool IsBurning() const noexcept { return BurnTimer > 0.0f && BurnDPS > 0.0f; }
    [[nodiscard]] bool IsBlinded() const noexcept { return BlindTimer > 0.0f; }
    [[nodiscard]] bool IsStunned() const noexcept { return StunTimer > 0.0f; }

    [[nodiscard]] float ConsumePendingBurnDamage() noexcept {
        const float dmg = m_PendingBurnDamage;
        m_PendingBurnDamage = 0.0f;
        return dmg;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (InvulnTimer > 0.0f) {
            InvulnTimer = std::max(0.0f, InvulnTimer - deltaTime);
        }
        if (BlindTimer > 0.0f) {
            BlindTimer = std::max(0.0f, BlindTimer - deltaTime);
        }
        if (StunTimer > 0.0f) {
            StunTimer = std::max(0.0f, StunTimer - deltaTime);
        }
        if (BurnTimer > 0.0f) {
            const float activeDt = std::min(deltaTime, BurnTimer);
            BurnTimer = std::max(0.0f, BurnTimer - deltaTime);
            if (BurnDPS > 0.0f) {
                m_PendingBurnDamage += BurnDPS * activeDt;
            }
            if (BurnTimer <= 0.0f) {
                BurnDPS = 0.0f;
                m_SpreadTimer = 0.0f;
            } else {
                m_SpreadTimer += deltaTime;
                if (m_SpreadTimer >= BurnSpreadInterval) {
                    m_SpreadTimer = 0.0f;
                    if (auto* go = GetGameObject()) {
                        if (auto* scene = go->GetScene()) {
                            const glm::vec3 pos = go->GetTransform()->GetWorldPosition();
                            scene->IgniteAtXZ({ pos.x, pos.z }, BurnSpreadRadius);
                        }
                    }
                }
            }
        }
    }

private:
    float m_PendingBurnDamage = 0.0f;
    float m_SpreadTimer = 0.0f;
};

#endif // SANDBOX_STATUS_EFFECTS_COMPONENT_H
