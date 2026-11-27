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
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace Sandbox {

enum class StatModifierKey : uint16_t {
    MaxHealth = 0,
    BaseDamage,
    DamageReduction,
    MoveSpeed,
    AttackRate,
    KnockbackImpulse,
    KnockbackResistance,
    WoodHarvestRate,
    OreHarvestRate,
    RepairEfficiency,
    MaxTorchFuel,
    MaxFuel,
    FuelDepletionRate,
    SlowBurnRate,
    VisionRadius,
    AshAttractionRange,
    AshGainMultiplier,
    RuneCooldownReduction,
    AuraDamage,
    SecondChanceRevive,
    StartingWood,
    BurnDamageOnHit,
    SwingArcDeg,
    ReflectDamageRatio,
    FuelOnKill,
    RunePowerMultiplier,
    DeployableHealthBonus,
    SpeedOnKill,
    StackingAttackSpeed,
    FireWaveDamage,
    BlindDuration,
    HealOnBurnKill,
    HyperArmorDuration,
    BonusRuneCharges,
    AshFrenzyBoost,
    QuakeRadius,
    BeaconRegenRate
};

struct ModifierApplyContext {
    Engine::GameObject& Player;
    PlayerStatsComponent* Stats = nullptr;
    HealthComponent* Health = nullptr;
    TorchComponent* Torch = nullptr;
    WeaponController* Weapon = nullptr;
    StatLayer Layer = StatLayer::Run;
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

    [[nodiscard]] static bool TryParseKey(std::string_view name, StatModifierKey& outKey) noexcept {
        const auto& keyMap = GetKeyMap();
        const auto it = keyMap.find(name);
        if (it == keyMap.end()) {
            return false;
        }
        outKey = it->second;
        return true;
    }

    [[nodiscard]] static bool IsSupported(std::string_view key) noexcept {
        StatModifierKey parsed{};
        return TryParseKey(key, parsed);
    }

    static bool Apply(Engine::GameObject& player,
                      StatModifierKey key,
                      float value,
                      StatLayer layer = StatLayer::Run) {
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
            player.GetComponent<WeaponController>(),
            layer
        };
        it->second(ctx, value);
        return true;
    }

    static bool Apply(Engine::GameObject& player,
                      std::string_view key,
                      float value,
                      StatLayer layer = StatLayer::Run) {
        StatModifierKey parsed{};
        if (!TryParseKey(key, parsed)) {
            return false;
        }
        return Apply(player, parsed, value, layer);
    }

private:
    [[nodiscard]] static const std::unordered_map<std::string_view, StatModifierKey>& GetKeyMap() {
        static const std::unordered_map<std::string_view, StatModifierKey> kKeys = {
            { "MaxHealth", StatModifierKey::MaxHealth },
            { "BaseDamage", StatModifierKey::BaseDamage },
            { "DamageReduction", StatModifierKey::DamageReduction },
            { "MoveSpeed", StatModifierKey::MoveSpeed },
            { "AttackRate", StatModifierKey::AttackRate },
            { "KnockbackImpulse", StatModifierKey::KnockbackImpulse },
            { "KnockbackResistance", StatModifierKey::KnockbackResistance },
            { "WoodHarvestRate", StatModifierKey::WoodHarvestRate },
            { "OreHarvestRate", StatModifierKey::OreHarvestRate },
            { "RepairEfficiency", StatModifierKey::RepairEfficiency },
            { "MaxTorchFuel", StatModifierKey::MaxTorchFuel },
            { "MaxFuel", StatModifierKey::MaxFuel },
            { "FuelDepletionRate", StatModifierKey::FuelDepletionRate },
            { "SlowBurnRate", StatModifierKey::SlowBurnRate },
            { "VisionRadius", StatModifierKey::VisionRadius },
            { "AshAttractionRange", StatModifierKey::AshAttractionRange },
            { "AshGainMultiplier", StatModifierKey::AshGainMultiplier },
            { "RuneCooldownReduction", StatModifierKey::RuneCooldownReduction },
            { "AuraDamage", StatModifierKey::AuraDamage },
            { "SecondChanceRevive", StatModifierKey::SecondChanceRevive },
            { "StartingWood", StatModifierKey::StartingWood },
            { "BurnDamageOnHit", StatModifierKey::BurnDamageOnHit },
            { "SwingArcDeg", StatModifierKey::SwingArcDeg },
            { "ReflectDamageRatio", StatModifierKey::ReflectDamageRatio },
            { "FuelOnKill", StatModifierKey::FuelOnKill },
            { "RunePowerMultiplier", StatModifierKey::RunePowerMultiplier },
            { "DeployableHealthBonus", StatModifierKey::DeployableHealthBonus },
            { "SpeedOnKill", StatModifierKey::SpeedOnKill },
            { "StackingAttackSpeed", StatModifierKey::StackingAttackSpeed },
            { "FireWaveDamage", StatModifierKey::FireWaveDamage },
            { "BlindDuration", StatModifierKey::BlindDuration },
            { "HealOnBurnKill", StatModifierKey::HealOnBurnKill },
            { "HyperArmorDuration", StatModifierKey::HyperArmorDuration },
            { "BonusRuneCharges", StatModifierKey::BonusRuneCharges },
            { "AshFrenzyBoost", StatModifierKey::AshFrenzyBoost },
            { "QuakeRadius", StatModifierKey::QuakeRadius },
            { "BeaconRegenRate", StatModifierKey::BeaconRegenRate },
        };
        return kKeys;
    }

    [[nodiscard]] static const std::unordered_map<StatModifierKey, ApplyFn>& GetTable() {
        static const std::unordered_map<StatModifierKey, ApplyFn> kTable = {
            { StatModifierKey::MaxHealth, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) {
                    c.Stats->MaxHP += v;
                    c.Stats->Heal(v);
                }
                if (c.Health) {
                    c.Health->MaxHP += v;
                    c.Health->CurrentHP = std::min(c.Health->MaxHP, c.Health->CurrentHP + v);
                }
            }},
            { StatModifierKey::BaseDamage, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusDamage += v;
            }},
            { StatModifierKey::DamageReduction, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusDamageReduction = std::clamp(c.Stats->BonusDamageReduction + v, 0.0f, 0.90f);
                if (c.Health) c.Health->DamageReduction = std::clamp(c.Health->DamageReduction + v, 0.0f, 0.90f);
            }},
            { StatModifierKey::MoveSpeed, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->MoveSpeed.AddLayer(c.Layer, v * 0.1f);
            }},
            { StatModifierKey::AttackRate, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AttackSpeed.AddLayer(c.Layer, v);
            }},
            { StatModifierKey::KnockbackImpulse, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusKnockbackImpulse += v;
            }},
            { StatModifierKey::KnockbackResistance, [](const ModifierApplyContext& c, float v) {
                if (c.Health) c.Health->KnockbackResistance = std::min(1.0f, c.Health->KnockbackResistance + v);
            }},
            { StatModifierKey::WoodHarvestRate, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->WoodHarvestMultiplier += v;
            }},
            { StatModifierKey::OreHarvestRate, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->OreHarvestMultiplier += v;
            }},
            { StatModifierKey::RepairEfficiency, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RepairEfficiencyMultiplier += v;
            }},
            { StatModifierKey::MaxTorchFuel, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MaxFuel += v;
                    c.Torch->Fuel = c.Torch->MaxFuel;
                }
            }},
            { StatModifierKey::MaxFuel, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MaxFuel += v;
                    c.Torch->Fuel = c.Torch->MaxFuel;
                }
            }},
            { StatModifierKey::FuelDepletionRate, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->BurnRateMultiplier = std::max(0.25f, c.Torch->BurnRateMultiplier + v);
            }},
            { StatModifierKey::SlowBurnRate, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->BurnRateMultiplier = std::max(0.25f, c.Torch->BurnRateMultiplier - v);
            }},
            { StatModifierKey::VisionRadius, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) {
                    c.Torch->MinRadius += v;
                    c.Torch->MaxRadius += v;
                }
            }},
            { StatModifierKey::AshAttractionRange, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshAttractionRange += v;
            }},
            { StatModifierKey::AshGainMultiplier, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshGainMultiplier += v;
            }},
            { StatModifierKey::RuneCooldownReduction, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RuneCooldownMultiplier = std::max(0.25f, c.Stats->RuneCooldownMultiplier - v);
            }},
            { StatModifierKey::AuraDamage, [](const ModifierApplyContext& c, float v) {
                if (c.Torch) c.Torch->AuraDamage += v;
            }},
            { StatModifierKey::SecondChanceRevive, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->ReviveCharges += static_cast<int>(std::round(v));
            }},
            { StatModifierKey::StartingWood, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AddWood(static_cast<int>(std::round(v)));
            }},
            { StatModifierKey::BurnDamageOnHit, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BurnDamageOnHit += v;
            }},
            { StatModifierKey::SwingArcDeg, [](const ModifierApplyContext& c, float v) {
                if (c.Weapon) c.Weapon->AddSwingArcBonus(v);
            }},
            { StatModifierKey::ReflectDamageRatio, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->ReflectDamageRatio += v;
            }},
            { StatModifierKey::FuelOnKill, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->FuelOnKill += v;
            }},
            { StatModifierKey::RunePowerMultiplier, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->RunePowerMultiplier += v;
            }},
            { StatModifierKey::DeployableHealthBonus, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->DeployableHealthBonus += v;
            }},
            { StatModifierKey::SpeedOnKill, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->SpeedOnKillBonus += v;
            }},
            { StatModifierKey::StackingAttackSpeed, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->StackingAttackSpeedBonus += v;
            }},
            { StatModifierKey::FireWaveDamage, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->FireWaveDamage += v;
            }},
            { StatModifierKey::BlindDuration, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->SolarFlareBlindSec += v;
            }},
            { StatModifierKey::HealOnBurnKill, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->HealOnBurnKill += v;
            }},
            { StatModifierKey::HyperArmorDuration, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->HyperArmorDuration += v;
            }},
            { StatModifierKey::BonusRuneCharges, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BonusRuneCharges += static_cast<int>(std::round(v));
            }},
            { StatModifierKey::AshFrenzyBoost, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->AshFrenzyBoost += v;
                if (c.Torch) c.Torch->AshFrenzySpeedBonus += v;
            }},
            { StatModifierKey::QuakeRadius, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->QuakeRadius = std::max(c.Stats->QuakeRadius, v);
            }},
            { StatModifierKey::BeaconRegenRate, [](const ModifierApplyContext& c, float v) {
                if (c.Stats) c.Stats->BeaconRegenRate += v;
            }},
        };
        return kTable;
    }
};

} // namespace Sandbox

#endif // SANDBOX_STAT_MODIFIER_REGISTRY_H
