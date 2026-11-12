#ifndef SCENE_SURFACESAMPLE_H
#define SCENE_SURFACESAMPLE_H

#include "Engine/Scene/WorldBiomeGenerator.h"
#include <cstdint>

namespace Engine {

/// @brief Surface properties sampled at a continuous 3D world (X, Z) coordinate (TER-04).
struct SurfaceSample {
    BiomeType DominantBiome = BiomeType::AshBarrens;
    float LinearDamping = 4.5f;       // Chalk = 1.1f (icy slide!), Ash = 4.5f, Grass = 5.5f, Tar = 9.0f, Cobble = 4.8f
    float MaxSpeedMultiplier = 1.0f;  // Tar = 0.70f (30% slow), Cobble = 1.08f (+8% road sprint), Chalk = 0.95f
    float Flammability = 0.0f;        // Tar layer weight normalized in [0.0 .. 1.0]
    bool IsBurning = false;           // True if currently active in the Tar/Fire Cellular Automaton
};

namespace SurfacePhysics {
    // Per-layer baseline linear damping values (layers 0..7)
    inline constexpr float LayerDamping[8] = {
        5.5f, // 0: Grass (Blackwood Thickets)
        4.5f, // 1: Ash (Ash Barrens)
        9.0f, // 2: Tar (Tar Bogs - thick sludge)
        1.1f, // 3: Chalk (Chalk Highlands - icy slip)
        4.8f, // 4: Cobblestone (Sanctuary Cobble)
        4.2f, // 5: Scorched Ash (Burnt ground)
        4.5f, // 6: Spare
        4.5f  // 7: Spare
    };

    // Per-layer speed multipliers (layers 0..7)
    inline constexpr float LayerSpeedMultiplier[8] = {
        0.92f, // 0: Grass
        1.00f, // 1: Ash
        0.70f, // 2: Tar (-30% slow)
        0.95f, // 3: Chalk
        1.08f, // 4: Cobblestone (+8% road sprint)
        1.00f, // 5: Scorched Ash
        1.00f, // 6: Spare
        1.00f  // 7: Spare
    };

    [[nodiscard]] constexpr BiomeType LayerToBiome(uint32_t layerIndex) noexcept {
        switch (layerIndex) {
            case 0: return BiomeType::BlackwoodThickets;
            case 1: return BiomeType::AshBarrens;
            case 2: return BiomeType::TarBogs;
            case 3: return BiomeType::ChalkHighlands;
            case 4: return BiomeType::SanctuaryCobble;
            case 5: return BiomeType::AshBarrens; // Scorched Ash
            default: return BiomeType::AshBarrens;
        }
    }
} // namespace SurfacePhysics

} // namespace Engine

#endif // SCENE_SURFACESAMPLE_H
