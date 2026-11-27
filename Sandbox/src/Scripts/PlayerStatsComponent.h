#ifndef SANDBOX_PLAYER_STATS_COMPONENT_H
#define SANDBOX_PLAYER_STATS_COMPONENT_H

#include <Engine/Scene/Component.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class StatLayer : uint8_t {
    Permanent = 0,
    Run = 1,
    Transient = 2
};

struct StatAccumulator {
    float Base = 1.0f;
    float PermanentAdditive = 0.0f;
    float RunAdditive = 0.0f;
    float TransientMultiplier = 1.0f;

    void AddLayer(StatLayer layer, float delta) noexcept {
        if (!std::isfinite(delta)) {
            return;
        }
        if (layer == StatLayer::Permanent) {
            PermanentAdditive += delta;
        } else if (layer == StatLayer::Run) {
            RunAdditive += delta;
        }
    }

    void SetTransientMultiplier(float mult) noexcept {
        TransientMultiplier = (std::isfinite(mult) && mult > 0.0f) ? mult : 1.0f;
    }

    [[nodiscard]] float Evaluate(float minValue = 0.01f) const noexcept {
        const float sum = Base + PermanentAdditive + RunAdditive;
        const float mult = (std::isfinite(TransientMultiplier) && TransientMultiplier > 0.0f) ? TransientMultiplier : 1.0f;
        return std::max(minValue, sum * mult);
    }
};

class PlayerStatsComponent final : public Engine::Component {
public:
    float MaxHP = 100.0f;
    float HP = 100.0f;
    int Wood = 0;
    int XP = 0;
    int RunAshes = 0;
    float AshAttractionRange = 5.5f;
    StatAccumulator MoveSpeed{ 1.0f };
    StatAccumulator AttackSpeed{ 1.0f };
    float MoveSpeedMultiplier = 1.0f;
    float AttackSpeedMultiplier = 1.0f;

    [[nodiscard]] float GetEffectiveMoveSpeedMultiplier() const noexcept {
        const float legacy = (std::isfinite(MoveSpeedMultiplier) && MoveSpeedMultiplier > 0.0f) ? MoveSpeedMultiplier : 1.0f;
        return MoveSpeed.Evaluate() * legacy;
    }

    [[nodiscard]] float GetEffectiveAttackSpeedMultiplier() const noexcept {
        const float legacy = (std::isfinite(AttackSpeedMultiplier) && AttackSpeedMultiplier > 0.0f) ? AttackSpeedMultiplier : 1.0f;
        return AttackSpeed.Evaluate() * legacy;
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
