#ifndef SANDBOX_COMBAT_DAMAGE_HELPER_H
#define SANDBOX_COMBAT_DAMAGE_HELPER_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/GameObject.h>
#include <glm/glm.hpp>

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

/// @brief Applies impact damage through HealthComponent (preferred) or legacy CombatTargetComponent.
inline bool ApplyImpactDamage(Engine::GameObject* target,
                              Engine::GameObject* source,
                              float amount,
                              float stunDuration = 0.0f,
                              const glm::vec2& impulseXZ = { 0.0f, 0.0f },
                              bool flashOnFallback = true) {
    if (!target || target->IsPendingDestroy() || amount <= 0.0f) {
        return false;
    }

    if (auto* health = target->GetComponent<HealthComponent>()) {
        DamageEvent evt;
        evt.Amount = amount;
        evt.Source = source;
        evt.StunDuration = stunDuration;
        evt.ImpulseXZ = impulseXZ;
        return health->ApplyDamage(evt);
    }

    if (auto* combatTarget = target->GetComponent<CombatTargetComponent>()) {
        combatTarget->ApplyDamage(amount, stunDuration, Engine::SpriteRenderer::FlashMode::PureWhite);
        if (glm::dot(impulseXZ, impulseXZ) > 1e-6f) {
            if (auto* rb = target->GetComponent<Engine::Rigidbody2D>()) {
                rb->ApplyImpulseXZ(impulseXZ);
            }
        }
        return true;
    }

    if (flashOnFallback) {
        if (auto* sr = target->GetComponent<Engine::SpriteRenderer>()) {
            sr->SetFlashMode(Engine::SpriteRenderer::FlashMode::PureWhite);
            sr->SetFlashAmount(1.0f);
        }
    }
    return false;
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
