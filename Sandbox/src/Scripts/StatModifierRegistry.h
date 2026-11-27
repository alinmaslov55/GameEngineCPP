#ifndef SANDBOX_STAT_MODIFIER_REGISTRY_H
#define SANDBOX_STAT_MODIFIER_REGISTRY_H

#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WeaponController.h"

#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace Sandbox {

struct ModifierApplyContext {
    Engine::GameObject& Player;
    PlayerStatsComponent* Stats = nullptr;
    HealthComponent* Health = nullptr;
    TorchComponent* Torch = nullptr;
    WeaponController* Weapon = nullptr;
};

/// @brief Shared dispatch registry for all roots.json and branches.json StatModifier keys.
class StatModifierRegistry {
public:
    using ApplyFn = void (*)(const ModifierApplyContext&, float);

    [[nodiscard]] static TorchComponent* FindTorch(Engine::GameObject& player) noexcept {
        if (auto* direct = player.GetComponent<TorchComponent>()) {
            return direct;
        }
        auto* transform = player.GetTransform();
        if (!transform) {
            return nullptr;
        }
        for (Engine::Transform* child : transform->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* torch = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    return torch;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] static bool IsSupported(std::string_view key) noexcept {
        const auto& table = GetTable();
        return table.find(key) != table.end();
    }

    static bool Apply(Engine::GameObject& player, std::string_view key, float value) {
        if (!std::isfinite(value)) {
            return false;
        }
        const auto& table = GetTable();
        const auto it = table.find(key);
        if (it == table.end()) {
            return false;
        }
        ModifierApplyContext ctx{
            player,
            player.GetComponent<PlayerStatsComponent>(),
            player.GetComponent<HealthComponent>(),
            FindTorch(player),
            player.GetComponent<WeaponController>()
        };
        it->second(ctx, value);
        return true;
    }

private:
    [[nodiscard]] static const std::unordered_map<std::string_view, ApplyFn>& GetTable() {
        static const std::unordered_map<std::string_view, ApplyFn> kTable = {
            { "MaxHealth", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) {
                    c.Stats->MaxHP += v;
                    c.Stats->Heal(v);
                }
                if (c.Health) {
                    c.Health->MaxHP += v;
                    c.Health->CurrentHP = std::min(c.Health->MaxHP, c.Health->CurrentHP + v);
                }
            }},
            { "BaseDamage", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusDamage += v;
            }},
            { "DamageReduction", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusDamageReduction = std::clamp(c.Stats->BonusDamageReduction + v, 0.0f, 0.90f);
                if (c.Health) c.Health->DamageReduction = std::clamp(c.Health->DamageReduction + v, 0.0f, 0.90f);
            }},
            { "MoveSpeed", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->MoveSpeedMultiplier += v * 0.1f;
            }},
            { "AttackRate", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AttackSpeedMultiplier += v;
            }},
            { "KnockbackImpulse", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusKnockbackImpulse += v;
            }},
            { "KnockbackResistance", [](const ModifierApplyContext& c, float v) {
                if (c.Health) c.Health->KnockbackResistance = std::min(1.0f, c.Health->KnockbackResistance + v);
            }},
            { "WoodHarvestRate", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->WoodHarvestMultiplier += v;
            }},
            { "OreHarvestRate", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->OreHarvestMultiplier += v;
            }},
            { "RepairEfficiency", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RepairEfficiencyMultiplier += v;
            }},
            { "MaxTorchFuel", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MaxFuel += v;
                    c.Torch->Fuel = c.Torch->MaxFuel;
                }
            }},
            { "MaxFuel", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MaxFuel += v;
                    c.Torch->Fuel = c.Torch->MaxFuel;
                }
            }},
            { "FuelDepletionRate", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->BurnRateMultiplier = std::max(0.25f, c.Torch->BurnRateMultiplier + v);
            }},
            { "SlowBurnRate", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->BurnRateMultiplier = std::max(0.25f, c.Torch->BurnRateMultiplier - v);
            }},
            { "VisionRadius", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MinRadius += v;
                    c.Torch->MaxRadius += v;
                }
            }},
            { "AshAttractionRange", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshAttractionRange += v;
            }},
            { "AshGainMultiplier", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshGainMultiplier += v;
            }},
            { "RuneCooldownReduction", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RuneCooldownMultiplier = std::max(0.25f, c.Stats->RuneCooldownMultiplier - v);
            }},
            { "AuraDamage", [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->AuraDamage += v;
            }},
            { "SecondChanceRevive", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->ReviveCharges += static_cast<int>(std::round(v));
            }},
            { "StartingWood", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AddWood(static_cast<int>(std::round(v)));
            }},
            { "BurnDamageOnHit", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BurnDamageOnHit += v;
            }},
            { "SwingArcDeg", [](const ModifierApplyContext& c, float v) {
                if (c.Weapon) c.Weapon->AddSwingArcBonus(v);
            }},
            { "ReflectDamageRatio", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->ReflectDamageRatio += v;
            }},
            { "FuelOnKill", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->FuelOnKill += v;
            }},
            { "RunePowerMultiplier", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RunePowerMultiplier += v;
            }},
            { "DeployableHealthBonus", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->DeployableHealthBonus += v;
            }},
            { "SpeedOnKill", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->SpeedOnKillBonus += v;
            }},
            { "StackingAttackSpeed", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->StackingAttackSpeedBonus += v;
            }},
            { "FireWaveDamage", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->FireWaveDamage += v;
            }},
            { "BlindDuration", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->SolarFlareBlindSec += v;
            }},
            { "HealOnBurnKill", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->HealOnBurnKill += v;
            }},
            { "HyperArmorDuration", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->HyperArmorDuration += v;
            }},
            { "BonusRuneCharges", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusRuneCharges += static_cast<int>(std::round(v));
            }},
            { "AshFrenzyBoost", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshFrenzyBoost += v;
                if (c.Torch) c.Torch->AshFrenzySpeedBonus += v;
            }},
            { "QuakeRadius", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->QuakeRadius = std::max(c.Stats->QuakeRadius, v);
            }},
            { "BeaconRegenRate", [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BeaconRegenRate += v;
            }},
        };
        return kTable;
    }
};

} // namespace Sandbox

#endif // SANDBOX_STAT_MODIFIER_REGISTRY_H
