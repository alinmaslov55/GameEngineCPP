#ifndef SANDBOX_PLAYER_STATS_COMPONENT_H
#define SANDBOX_PLAYER_STATS_COMPONENT_H

#include <Engine/Scene/Component.h>

#include <algorithm>
#include <cmath>

class PlayerStatsComponent final : public Engine::Component {
public:
    float MaxHP = 100.0f;
    float HP = 100.0f;
    int Wood = 0;
    int XP = 0;
    int RunAshes = 0;
    float AshAttractionRange = 5.5f;
    float MoveSpeedMultiplier = 1.0f;
    float AttackSpeedMultiplier = 1.0f;

    // GAM-12 Branch Synergy Stats
    float BurnDamageOnHit = 0.0f;
    float FuelOnKill = 0.0f;
    float HealOnBurnKill = 0.0f;
    float SolarFlareBlindSec = 0.0f;
    float QuakeRadius = 0.0f;
    float BeaconRegenRate = 0.0f;

    void AddWood(int amount) noexcept {
        if (amount > 0) {
            Wood += amount;
        }
    }

    void AddXP(int amount) noexcept {
        if (amount > 0) {
            XP += amount;
        }
    }

    void AddRunAshes(int amount) noexcept {
        if (amount > 0) {
            RunAshes += amount;
        }
    }

    void Heal(float amount) noexcept {
        if (std::isfinite(amount) && amount > 0.0f) {
            HP = std::min(MaxHP, HP + amount);
        }
    }

    void TakeDamage(float amount) noexcept {
        if (std::isfinite(amount) && amount > 0.0f) {
            HP = std::max(0.0f, HP - amount);
        }
    }
};

#endif // SANDBOX_PLAYER_STATS_COMPONENT_H
