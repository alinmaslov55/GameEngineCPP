#ifndef PHYSICS_PHYSICSLAYER_H
#define PHYSICS_PHYSICSLAYER_H

#include "Engine/Scene/EntityTag.h"
#include <cstdint>

namespace Engine {

namespace PhysicsLayer {
    constexpr uint64_t Environment   = 1ULL << 0; // Tall trees, cliffs, shrines, unvaultable walls
    constexpr uint64_t LowObstacle   = 1ULL << 1; // Fences, bone piles, barricades (vaultable when Y > 0.8m)
    constexpr uint64_t Player        = 1ULL << 2; // Hero standee / player physics body
    constexpr uint64_t Enemy         = 1ULL << 3; // Ghouls, Thralls, Wendigos, Jiangshis
    constexpr uint64_t Companion     = 1ULL << 4; // Tamed beasts, friendly escort NPCs
    constexpr uint64_t PlayerHitbox  = 1ULL << 5; // Maul swings, torch fire arcs, sickle sweeps
    constexpr uint64_t EnemyHitbox   = 1ULL << 6; // Ghoul claws, Jiangshi death grip
    constexpr uint64_t Projectile    = 1ULL << 7; // Barricade shrapnel, bone spears, fire arrows
    constexpr uint64_t Pickup        = 1ULL << 8; // Wood logs, ritual ash, soul hearts
    constexpr uint64_t Default       = 1ULL << 9; // Uncategorized default physics geometry
    constexpr uint64_t All           = ~0ULL;

    // Default layers excluded from collision mask when an entity is airborne (Y > AirborneClearanceHeight)
    constexpr uint64_t DefaultAirborneExcluded = LowObstacle | Enemy | Pickup;

    [[nodiscard]] inline uint64_t FromEntityLayer(uint32_t entityLayer) noexcept {
        uint64_t mask = 0;
        if (entityLayer & EntityLayer::Environment)  mask |= Environment;
        if (entityLayer & EntityLayer::LowObstacle)  mask |= LowObstacle;
        if (entityLayer & EntityLayer::Player)       mask |= Player;
        if (entityLayer & EntityLayer::Enemy)        mask |= Enemy;
        if (entityLayer & EntityLayer::Companion)    mask |= Companion;
        if (entityLayer & EntityLayer::PlayerHitbox) mask |= PlayerHitbox;
        if (entityLayer & EntityLayer::EnemyHitbox)  mask |= EnemyHitbox;
        if (entityLayer & EntityLayer::Projectile)   mask |= Projectile;
        if (entityLayer & EntityLayer::Pickup)       mask |= Pickup;
        if (entityLayer & EntityLayer::Default)      mask |= Default;
        return mask;
    }

    [[nodiscard]] inline uint32_t ToEntityLayer(uint64_t physicsLayer) noexcept {
        uint32_t mask = 0;
        if (physicsLayer & Environment)  mask |= EntityLayer::Environment;
        if (physicsLayer & LowObstacle)  mask |= EntityLayer::LowObstacle;
        if (physicsLayer & Player)       mask |= EntityLayer::Player;
        if (physicsLayer & Enemy)        mask |= EntityLayer::Enemy;
        if (physicsLayer & Companion)    mask |= EntityLayer::Companion;
        if (physicsLayer & PlayerHitbox) mask |= EntityLayer::PlayerHitbox;
        if (physicsLayer & EnemyHitbox)  mask |= EntityLayer::EnemyHitbox;
        if (physicsLayer & Projectile)   mask |= EntityLayer::Projectile;
        if (physicsLayer & Pickup)       mask |= EntityLayer::Pickup;
        if (physicsLayer & Default)      mask |= EntityLayer::Default;
        return mask;
    }
} // namespace PhysicsLayer

} // namespace Engine

#endif // PHYSICS_PHYSICSLAYER_H
