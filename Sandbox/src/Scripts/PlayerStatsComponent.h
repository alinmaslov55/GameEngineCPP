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
    float FrenzySpeedMultiplier = 1.0f;

    [[nodiscard]] float GetEffectiveMoveSpeedMultiplier() const noexcept {
        const float base = (std::isfinite(MoveSpeedMultiplier) && MoveSpeedMultiplier > 0.0f) ? MoveSpeedMultiplier : 1.0f;
        const float frenzy = (std::isfinite(FrenzySpeedMultiplier) && FrenzySpeedMultiplier > 0.0f) ? FrenzySpeedMultiplier : 1.0f;
        return base * frenzy;
    }

    [[nodiscard]] float GetEffectiveAttackSpeedMultiplier() const noexcept {
        const float base = (std::isfinite(AttackSpeedMultiplier) && AttackSpeedMultiplier > 0.0f) ? AttackSpeedMultiplier : 1.0f;
        const float frenzy = (std::isfinite(FrenzySpeedMultiplier) && FrenzySpeedMultiplier > 0.0f) ? FrenzySpeedMultiplier : 1.0f;
        return base * frenzy;
    }

    // GAM-12 Branch & Root Synergy Stats
    float BonusDamage = 0.0f;
    float BonusDamageReduction = 0.0f;
    float BonusKnockbackImpulse = 0.0f;
    float WoodHarvestMultiplier = 1.0f;
    float OreHarvestMultiplier = 1.0f;
    float RepairEfficiencyMultiplier = 1.0f;
    float AshGainMultiplier = 1.0f;
    float RuneCooldownMultiplier = 1.0f;
    float RunePowerMultiplier = 1.0f;
    float ReflectDamageRatio = 0.0f;
    float DeployableHealthBonus = 0.0f;
    float SpeedOnKillBonus = 0.0f;
    float StackingAttackSpeedBonus = 0.0f;
    float FireWaveDamage = 0.0f;
    float HyperArmorDuration = 0.0f;
    float AshFrenzyBoost = 0.0f;
    int ReviveCharges = 0;
    int BonusRuneCharges = 0;
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
