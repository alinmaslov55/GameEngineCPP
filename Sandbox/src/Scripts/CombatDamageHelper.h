#ifndef SANDBOX_COMBAT_DAMAGE_HELPER_H
#define SANDBOX_COMBAT_DAMAGE_HELPER_H

#include "Scripts/CombatTypes.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/StatusEffectsComponent.h"

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/GameObject.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Sandbox {

/// @brief Safely normalizes an XZ direction vector, returning fallback if near-zero or non-finite.
[[nodiscard]] inline glm::vec2 SafeNormalizeXZ(const glm::vec2& v,
                                               const glm::vec2& fallback = { 1.0f, 0.0f }) noexcept {
    const float lenSq = glm::dot(v, v);
    if (!std::isfinite(lenSq) || lenSq <= 1e-8f) {
        return fallback;
    }
    return v / std::sqrt(lenSq);
}

struct CombatDamage {
    /// @brief Unified damage & status facade targeting HealthComponent (and syncing PlayerStatsComponent).
    static bool Apply(Engine::GameObject* target,
                      const DamageEvent& evt,
                      const StatusApplication& status = {},
                      bool flashOnFallback = false) {
        if (!target || target->IsPendingDestroy()) {
            return false;
        }

        bool applied = false;
        if (auto* health = target->GetComponent<HealthComponent>()) {
            applied = health->ApplyDamage(evt);
            if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
                stats->HP = health->CurrentHP;
                stats->MaxHP = health->MaxHP;
            }
        } else {
            if (flashOnFallback) {
                if (auto* sr = target->GetComponent<Engine::SpriteRenderer>()) {
                    sr->SetFlashMode(Engine::SpriteRenderer::FlashMode::PureWhite);
                    sr->SetFlashAmount(1.0f);
                }
            }
            if (glm::dot(evt.ImpulseXZ, evt.ImpulseXZ) > 1e-6f) {
                if (auto* rb = target->GetComponent<Engine::Rigidbody2D>()) {
                    rb->ApplyImpulseXZ(evt.ImpulseXZ);
                }
            }
        }

        if (auto* statusComp = target->GetComponent<StatusEffectsComponent>()) {
            if (status.BurnDurationSec > 0.0f && status.BurnDPS > 0.0f) {
                statusComp->ApplyBurn(status.BurnDurationSec, status.BurnDPS);
            }
            if (status.BlindDurationSec > 0.0f) {
                statusComp->ApplyBlind(status.BlindDurationSec);
            }
            if (status.StunDurationSec > 0.0f) {
                statusComp->ApplyStun(status.StunDurationSec);
            }
        }

        return applied;
    }
};

/// @brief Applies impact damage through CombatDamage::Apply.
inline bool ApplyImpactDamage(Engine::GameObject* target,
                              Engine::GameObject* source,
                              float amount,
                              float stunDuration = 0.0f,
                              const glm::vec2& impulseXZ = { 0.0f, 0.0f },
                              bool flashOnFallback = true) {
    if (!target || target->IsPendingDestroy() || amount <= 0.0f) {
        return false;
    }
    DamageEvent evt;
    evt.Amount = amount;
    evt.Source = source;
    evt.StunDuration = stunDuration;
    evt.ImpulseXZ = impulseXZ;
    return CombatDamage::Apply(target, evt, {}, flashOnFallback);
}

/// @brief Applies damage through HealthComponent (syncing PlayerStatsComponent if present) or falls back to PlayerStatsComponent.
inline bool ApplyPlayerOrTargetDamage(Engine::GameObject* target,
                                        Engine::GameObject* source,
                                        float amount,
                                        bool bypassInvuln = false) {
    if (!target || target->IsPendingDestroy() || !std::isfinite(amount) || amount <= 0.0f) {
        return false;
    }

    if (auto* health = target->GetComponent<HealthComponent>()) {
        DamageEvent evt;
        evt.Amount = amount;
        evt.Source = source;
        evt.BypassInvuln = bypassInvuln;
        const bool applied = health->ApplyDamage(evt);
        if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
            stats->HP = health->CurrentHP;
            stats->MaxHP = health->MaxHP;
        }
        return applied;
    }

    if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
        stats->TakeDamage(amount);
        return true;
    }
    return false;
}

/// @brief Heals through HealthComponent (syncing PlayerStatsComponent if present) or falls back to PlayerStatsComponent.
inline void HealPlayerOrTarget(Engine::GameObject* target, float amount) {
    if (!target || target->IsPendingDestroy() || !std::isfinite(amount) || amount <= 0.0f) {
        return;
    }
    if (auto* health = target->GetComponent<HealthComponent>()) {
        health->CurrentHP = std::min(health->MaxHP, health->CurrentHP + amount);
        if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
            stats->HP = health->CurrentHP;
            stats->MaxHP = health->MaxHP;
        }
        return;
    }
    if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
        stats->Heal(amount);
    }
}

} // namespace Sandbox

#endif // SANDBOX_COMBAT_DAMAGE_HELPER_H
