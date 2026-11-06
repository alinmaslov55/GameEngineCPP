#ifndef SCENE_ENTITY_TAG_H
#define SCENE_ENTITY_TAG_H

#include <cstdint>
#include <string_view>

namespace Engine {

enum class EntityTag : uint16_t {
    Untagged = 0,
    Player,
    Enemy,
    Companion,
    Foliage,
    HarvestableTree,
    DestructibleProp,
    TacticalStructure,
    SafeZoneBrazier,
    Shrine,
    ResourcePickup,
    Projectile
};

namespace EntityLayer {
    constexpr uint32_t Default       = 1U << 0;
    constexpr uint32_t Environment   = 1U << 1;
    constexpr uint32_t LowObstacle   = 1U << 2;
    constexpr uint32_t Player        = 1U << 3;
    constexpr uint32_t Enemy         = 1U << 4;
    constexpr uint32_t Companion     = 1U << 5;
    constexpr uint32_t PlayerHitbox  = 1U << 6;
    constexpr uint32_t EnemyHitbox   = 1U << 7;
    constexpr uint32_t Projectile    = 1U << 8;
    constexpr uint32_t Pickup        = 1U << 9;
    constexpr uint32_t All           = 0xFFFFFFFFU;
}

inline const char* EntityTagToString(EntityTag tag) noexcept {
    switch (tag) {
        case EntityTag::Player:            return "Player";
        case EntityTag::Enemy:             return "Enemy";
        case EntityTag::Companion:         return "Companion";
        case EntityTag::Foliage:           return "Foliage";
        case EntityTag::HarvestableTree:   return "HarvestableTree";
        case EntityTag::DestructibleProp:  return "DestructibleProp";
        case EntityTag::TacticalStructure: return "TacticalStructure";
        case EntityTag::SafeZoneBrazier:   return "SafeZoneBrazier";
        case EntityTag::Shrine:            return "Shrine";
        case EntityTag::ResourcePickup:    return "ResourcePickup";
        case EntityTag::Projectile:        return "Projectile";
        case EntityTag::Untagged:
        default:                           return "Untagged";
    }
}

inline EntityTag StringToEntityTag(std::string_view str) noexcept {
    if (str == "Player")            return EntityTag::Player;
    if (str == "Enemy")             return EntityTag::Enemy;
    if (str == "Companion")         return EntityTag::Companion;
    if (str == "Foliage")           return EntityTag::Foliage;
    if (str == "HarvestableTree")   return EntityTag::HarvestableTree;
    if (str == "DestructibleProp")  return EntityTag::DestructibleProp;
    if (str == "TacticalStructure") return EntityTag::TacticalStructure;
    if (str == "SafeZoneBrazier")   return EntityTag::SafeZoneBrazier;
    if (str == "Shrine")            return EntityTag::Shrine;
    if (str == "ResourcePickup")    return EntityTag::ResourcePickup;
    if (str == "Projectile")        return EntityTag::Projectile;
    return EntityTag::Untagged;
}

} // namespace Engine

#endif // SCENE_ENTITY_TAG_H
