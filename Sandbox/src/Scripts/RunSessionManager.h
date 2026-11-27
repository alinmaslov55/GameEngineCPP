#ifndef SANDBOX_RUN_SESSION_MANAGER_H
#define SANDBOX_RUN_SESSION_MANAGER_H

#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/RuneCasterComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WeaponController.h"

#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Data/SaveManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

class RunSessionManager {
public:
    static RunSessionManager& Get() noexcept {
        static RunSessionManager s_Instance;
        return s_Instance;
    }

    Engine::PlayerProfileData Profile;
    uint32_t SlotIndex = 0;
    std::string SelectedWeaponId = "rough_iron_maul";
    std::string SelectedRuneId = "rune_ignite";
    uint64_t NextRunSeed = 42ULL;

    // Active-run telemetry
    uint32_t EnemiesSlain = 0;
    uint32_t DominoKills = 0;
    float LastSurvivalSeconds = 0.0f;
    uint64_t LastAshesEarned = 0;
    bool LastVictory = false;

    void ResetRunTelemetry() noexcept {
        EnemiesSlain = 0;
        DominoKills = 0;
        LastSurvivalSeconds = 0.0f;
        LastAshesEarned = 0;
        LastVictory = false;
    }

    bool LoadOrCreateProfile(uint32_t slot = 0) {
        SlotIndex = slot;
        Engine::PlayerProfileData loaded;
        if (Engine::SaveManager::LoadProfile(slot, loaded)) {
            Profile = std::move(loaded);
            Engine::SaveManager::ApplyAudioAndVideoSettings(Profile);
            return true;
        }
        Profile = Engine::PlayerProfileData{};
        Engine::SaveManager::SaveProfileAtomic(slot, Profile);
        return false;
    }

    bool SaveProfile() const {
        return Engine::SaveManager::SaveProfileAtomic(SlotIndex, Profile);
    }

    [[nodiscard]] uint32_t GetRootRank(const std::string& rootId) const noexcept {
        const auto it = Profile.UnlockedRootRanks.find(rootId);
        return (it != Profile.UnlockedRootRanks.end()) ? it->second : 0u;
    }

    [[nodiscard]] static uint64_t ComputeRootNextRankCost(const Engine::RootDefinition& def, uint32_t currentRank) noexcept {
        const double mult = std::pow(static_cast<double>(std::max(1.0f, def.CostMultiplier)),
                                     static_cast<double>(currentRank));
        return static_cast<uint64_t>(std::llround(static_cast<double>(def.BaseCostAshes) * mult));
    }

    bool PurchaseRootRank(const std::string& rootId) {
        Engine::RootDefinition def;
        if (!ResolveRootDef(rootId, def)) {
            return false;
        }
        const uint32_t rank = GetRootRank(rootId);
        if (rank >= std::max(1u, def.MaxRank)) {
            return false;
        }
        const uint64_t cost = ComputeRootNextRankCost(def, rank);
        if (Profile.BankedAshes < cost) {
            return false;
        }
        Profile.BankedAshes -= cost;
        Profile.UnlockedRootRanks[rootId] = rank + 1u;
        SaveProfile();
        return true;
    }

    void ApplyRootUpgradesToPlayer(Engine::GameObject& player) const {
        auto* stats = player.GetComponent<PlayerStatsComponent>();
        auto* health = player.GetComponent<HealthComponent>();
        auto* weapon = player.GetComponent<WeaponController>();
        auto* runeCaster = player.GetComponent<RuneCasterComponent>();

        TorchComponent* torch = nullptr;
        for (Engine::Transform* child : player.GetTransform()->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* t = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    torch = t;
                    break;
                }
            }
        }

        if (weapon && !SelectedWeaponId.empty()) {
            weapon->EquipWeapon(SelectedWeaponId);
        }
        if (runeCaster && !SelectedRuneId.empty()) {
            runeCaster->EquipRune(SelectedRuneId);
        }

        for (const auto& [rootId, rank] : Profile.UnlockedRootRanks) {
            if (rank == 0u) {
                continue;
            }
            Engine::RootDefinition def;
            if (!ResolveRootDef(rootId, def)) {
                continue;
            }
            const float bonus = def.ModifierPerRank * static_cast<float>(rank);
            if (def.StatModifier == "MaxHealth" && stats) {
                stats->MaxHP += bonus;
                stats->HP += bonus;
            } else if (def.StatModifier == "DamageReduction" && health) {
                health->DamageReduction = std::clamp(health->DamageReduction + bonus, 0.0f, 0.90f);
            } else if (def.StatModifier == "MoveSpeed" && stats) {
                stats->MoveSpeedMultiplier += bonus * 0.1f;
            } else if (def.StatModifier == "AttackRate" && stats) {
                stats->AttackSpeedMultiplier += bonus;
            } else if (def.StatModifier == "SlowBurnRate" && torch) {
                torch->BurnRateMultiplier = std::max(0.25f, torch->BurnRateMultiplier - bonus);
            } else if (def.StatModifier == "AuraDamage" && torch) {
                torch->AuraDamage += bonus;
            } else if (def.StatModifier == "AshAttractionRange" && stats) {
                stats->AshAttractionRange += bonus;
            } else if (def.StatModifier == "FuelOnKill" && stats) {
                stats->FuelOnKill += bonus;
            } else if (def.StatModifier == "MaxFuel" && torch) {
                torch->MaxFuel += bonus;
                torch->Fuel = torch->MaxFuel;
            } else if (def.StatModifier == "StartingWood" && stats) {
                stats->AddWood(static_cast<int>(std::round(bonus)));
            }
        }
    }

    void BankRunSummary(float survivalSeconds, uint32_t enemiesSlain, uint32_t dominoKills, uint64_t ashesEarned, bool victory) {
        LastSurvivalSeconds = std::max(0.0f, survivalSeconds);
        EnemiesSlain = enemiesSlain;
        DominoKills = dominoKills;
        LastAshesEarned = ashesEarned;
        LastVictory = victory;

        Profile.TotalRuns += 1u;
        Profile.BankedAshes += ashesEarned;
        const uint32_t secInt = static_cast<uint32_t>(std::floor(LastSurvivalSeconds));
        Profile.BestSurvivalSeconds = std::max(Profile.BestSurvivalSeconds, secInt);
        SaveProfile();
    }

private:
    RunSessionManager() = default;

    static bool ResolveRootDef(const std::string& rootId, Engine::RootDefinition& outDef) {
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Roots().Find(rootId)) {
                outDef = *def;
                return true;
            }
        }
        if (rootId == "root_iron_sinew") {
            outDef = { "root_iron_sinew", "Iron Sinew", "", "Physical", 5, 100, 1.5f, "MaxHealth", 20.0f, "" };
            return true;
        }
        if (rootId == "root_swift_stride") {
            outDef = { "root_swift_stride", "Swift Stride", "", "Physical", 5, 100, 1.5f, "MoveSpeed", 0.25f, "" };
            return true;
        }
        if (rootId == "root_stone_skin") {
            outDef = { "root_stone_skin", "Stone Skin", "", "Physical", 5, 150, 1.6f, "DamageReduction", 0.04f, "" };
            return true;
        }
        return false;
    }
};

#endif // SANDBOX_RUN_SESSION_MANAGER_H
