#ifndef SANDBOX_ALPHARUNSCENE_H
#define SANDBOX_ALPHARUNSCENE_H

#include <Engine/Scene/Scene.h>
#include <cstdint>
#include <memory>

namespace Sandbox {

/// @brief Factory for the GAM-05 Alpha Survival Arena procedural biome clearing.
class AlphaRunScene {
public:
    static constexpr float kOriginSafeRadiusMeters = 8.0f;
    static constexpr float kNightMoonIntensity = 0.25f;

    /// @brief Creates a seeded procedural survival arena with 8.0m Sanctuary Cobble clearing,
    /// central Sanctuary Brazier, nighttime indigo moonlight, and Player with 360° shadow-casting Torch.
    static std::shared_ptr<Engine::Scene> Create(uint64_t seed = 42ULL);

    /// @brief Creates the Sanctuary Clearing Hub (Title Overlay, Weapon Rack, Rune Pedestal,
    /// Root Altar, Northern Umbral Gate, and walkable safe clearing).
    static std::shared_ptr<Engine::Scene> CreateSanctuaryHubScene();

    /// @brief Attaches runtime gameplay scripts (PlayerController, WeaponController, CameraComponent,
    /// ArenaStreamingDriver) and initializes GameplayData when entering Play Mode.
    static void BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene);
};

} // namespace Sandbox

#endif // SANDBOX_ALPHARUNSCENE_H
