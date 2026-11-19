#ifndef DATA_GAMEPLAYDEFINITIONS_H
#define DATA_GAMEPLAYDEFINITIONS_H

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

namespace JsonHelper {

template<typename T>
inline T Get(const nlohmann::json& j, const char* key, const T& defaultVal) {
    if (j.contains(key) && !j[key].is_null()) {
        try {
            return j[key].get<T>();
        } catch (...) {
            return defaultVal;
        }
    }
    return defaultVal;
}

} // namespace JsonHelper

// 1. Permanent Sanctuary Hub Progression Roots (16 Nodes)
struct RootDefinition {
    std::string Id;
    std::string Name;
    std::string Description;
    std::string Category = "Physical"; // "Physical" or "Spiritual"
    uint32_t MaxRank = 5;
    uint32_t BaseCostAshes = 100;
    float CostMultiplier = 1.5f;
    std::string StatModifier;
    float ModifierPerRank = 0.1f;
    std::string Icon;
};

inline bool ParseRootDefinition(const nlohmann::json& j, RootDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Description = JsonHelper::Get(j, "Description", std::string{});
    out.Category = JsonHelper::Get(j, "Category", std::string("Physical"));
    out.MaxRank = JsonHelper::Get(j, "MaxRank", 5u);
    out.BaseCostAshes = JsonHelper::Get(j, "BaseCostAshes", 100u);
    out.CostMultiplier = JsonHelper::Get(j, "CostMultiplier", 1.5f);
    out.StatModifier = JsonHelper::Get(j, "StatModifier", std::string{});
    out.ModifierPerRank = JsonHelper::Get(j, "ModifierPerRank", 0.1f);
    out.Icon = JsonHelper::Get(j, "Icon", std::string{});
    return true;
}

// 2. In-Run Shrine Buffs (24 Branches)
struct BranchDefinition {
    std::string Id;
    std::string Name;
    std::string Description;
    std::string Rarity = "Common"; // "Common", "Rare", "Epic", "Legendary"
    std::vector<std::string> Tags;
    std::string StatModifier;
    float Value = 0.0f;
    std::string Icon;
};

inline bool ParseBranchDefinition(const nlohmann::json& j, BranchDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Description = JsonHelper::Get(j, "Description", std::string{});
    out.Rarity = JsonHelper::Get(j, "Rarity", std::string("Common"));
    out.StatModifier = JsonHelper::Get(j, "StatModifier", std::string{});
    out.Value = JsonHelper::Get(j, "Value", 0.0f);
    out.Icon = JsonHelper::Get(j, "Icon", std::string{});

    out.Tags.clear();
    if (j.contains("Tags") && j["Tags"].is_array()) {
        for (const auto& tag : j["Tags"]) {
            if (tag.is_string()) {
                out.Tags.push_back(tag.get<std::string>());
            }
        }
    }
    return true;
}

// 3. Active Spells & Runes (4 Runes)
struct RuneDefinition {
    std::string Id;
    std::string Name;
    std::string Description;
    float FuelCost = 10.0f;
    float CooldownSec = 5.0f;
    float Radius = 6.0f;
    float DurationSec = 3.0f;
    std::string Icon;
};

inline bool ParseRuneDefinition(const nlohmann::json& j, RuneDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Description = JsonHelper::Get(j, "Description", std::string{});
    out.FuelCost = JsonHelper::Get(j, "FuelCost", 10.0f);
    out.CooldownSec = JsonHelper::Get(j, "CooldownSec", 5.0f);
    out.Radius = JsonHelper::Get(j, "Radius", 6.0f);
    out.DurationSec = JsonHelper::Get(j, "DurationSec", 3.0f);
    out.Icon = JsonHelper::Get(j, "Icon", std::string{});
    return true;
}

// 4. Weapons & Tiers (9 Entries: Maul, Halberd, Sickle x 3 tiers)
struct WeaponDefinition {
    std::string Id;
    std::string WeaponType = "Maul"; // "Maul", "Halberd", "Sickle"
    uint32_t Tier = 1;
    std::string Name;
    float Damage = 25.0f;
    float SwingArcDeg = 120.0f;
    float Reach = 2.5f;
    float KnockbackImpulse = 18.0f;
    float AttackRate = 1.2f;
    std::string Sprite;
};

inline bool ParseWeaponDefinition(const nlohmann::json& j, WeaponDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.WeaponType = JsonHelper::Get(j, "WeaponType", std::string("Maul"));
    out.Tier = JsonHelper::Get(j, "Tier", 1u);
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Damage = JsonHelper::Get(j, "Damage", 25.0f);
    out.SwingArcDeg = JsonHelper::Get(j, "SwingArcDeg", 120.0f);
    out.Reach = JsonHelper::Get(j, "Reach", 2.5f);
    out.KnockbackImpulse = JsonHelper::Get(j, "KnockbackImpulse", 18.0f);
    out.AttackRate = JsonHelper::Get(j, "AttackRate", 1.2f);
    out.Sprite = JsonHelper::Get(j, "Sprite", std::string{});
    return true;
}

// 5. Deployables & Drops (4 Structures)
struct DropDefinition {
    std::string Id;
    std::string Name;
    std::string Description;
    uint32_t WoodCost = 4;
    float BuildTimeSec = 0.5f;
    float Health = 100.0f;
    std::string PrefabPath;
    std::string Icon;
};

inline bool ParseDropDefinition(const nlohmann::json& j, DropDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Description = JsonHelper::Get(j, "Description", std::string{});
    out.WoodCost = JsonHelper::Get(j, "WoodCost", 4u);
    out.BuildTimeSec = JsonHelper::Get(j, "BuildTimeSec", 0.5f);
    out.Health = JsonHelper::Get(j, "Health", 100.0f);
    out.PrefabPath = JsonHelper::Get(j, "PrefabPath", std::string{});
    out.Icon = JsonHelper::Get(j, "Icon", std::string{});
    return true;
}

// 6. Enemy Archetypes (5 Mobs)
struct EnemyDefinition {
    std::string Id;
    std::string Name;
    std::string Archetype = "Swarm"; // "Swarm", "Jumper", "EliteStalker"
    float Health = 40.0f;
    float MoveSpeed = 3.5f;
    float Damage = 10.0f;
    float Mass = 30.0f;
    float LightVulnerability = 1.0f;
    std::string SpriteSheet;
};

inline bool ParseEnemyDefinition(const nlohmann::json& j, EnemyDefinition& out) {
    if (!j.is_object() || !j.contains("Id") || !j["Id"].is_string()) {
        return false;
    }
    out.Id = j["Id"].get<std::string>();
    if (out.Id.empty()) {
        return false;
    }
    out.Name = JsonHelper::Get(j, "Name", out.Id);
    out.Archetype = JsonHelper::Get(j, "Archetype", std::string("Swarm"));
    out.Health = JsonHelper::Get(j, "Health", 40.0f);
    out.MoveSpeed = JsonHelper::Get(j, "MoveSpeed", 3.5f);
    out.Damage = JsonHelper::Get(j, "Damage", 10.0f);
    out.Mass = JsonHelper::Get(j, "Mass", 30.0f);
    out.LightVulnerability = JsonHelper::Get(j, "LightVulnerability", 1.0f);
    out.SpriteSheet = JsonHelper::Get(j, "SpriteSheet", std::string{});
    return true;
}

} // namespace Engine

#endif // DATA_GAMEPLAYDEFINITIONS_H
