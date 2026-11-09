#ifndef PHYSICS_PHYSICSLAYER_H
#define PHYSICS_PHYSICSLAYER_H

#include "Engine/Scene/EntityTag.h"
#include <cstdint>

namespace Engine {

namespace PhysicsLayer {
    // Aligned 1:1 with EntityLayer bit indices (1 << 0 .. 1 << 9) for zero-branch bit-casting
    constexpr uint64_t Default       = 1ULL << 0; // Uncategorized default physics geometry
    constexpr uint64_t Environment   = 1ULL << 1; // Tall trees, cliffs, shrines, unvaultable walls
    constexpr uint64_t LowObstacle   = 1ULL << 2; // Fences, bone piles, barricades (vaultable when Y > 0.8m)
    constexpr uint64_t Player        = 1ULL << 3; // Hero standee / player physics body
    constexpr uint64_t Enemy         = 1ULL << 4; // Ghouls, Thralls, Wendigos, Jiangshis
    constexpr uint64_t Companion     = 1ULL << 5; // Tamed beasts, friendly escort NPCs
    constexpr uint64_t PlayerHitbox  = 1ULL << 6; // Maul swings, torch fire arcs, sickle sweeps
    constexpr uint64_t EnemyHitbox   = 1ULL << 7; // Ghoul claws, Jiangshi death grip
    constexpr uint64_t Projectile    = 1ULL << 8; // Barricade shrapnel, bone spears, fire arrows
    constexpr uint64_t Pickup        = 1ULL << 9; // Wood logs, ritual ash, soul hearts
    constexpr uint64_t All           = ~0ULL;

    // Default layers excluded from collision mask when an entity is airborne (Y > AirborneClearanceHeight)
    constexpr uint64_t DefaultAirborneExcluded = LowObstacle | Enemy | Pickup;

    [[nodiscard]] inline constexpr uint64_t FromEntityLayer(uint32_t entityLayer) noexcept {
        return static_cast<uint64_t>(entityLayer);
    }

    [[nodiscard]] inline constexpr uint32_t ToEntityLayer(uint64_t physicsLayer) noexcept {
        return static_cast<uint32_t>(physicsLayer & 0xFFFFFFFFULL);
    }
} // namespace PhysicsLayer

} // namespace Engine

#endif // PHYSICS_PHYSICSLAYER_H
