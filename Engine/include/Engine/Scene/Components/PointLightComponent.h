#ifndef SCENE_POINTLIGHTCOMPONENT_H
#define SCENE_POINTLIGHTCOMPONENT_H

#include "Engine/Scene/Component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

enum class LightPriority : uint8_t {
    Low_VFX = 0,           // Fire sparks, transient flashes
    Normal_World = 1,      // Will-o'-the-Wisps, Umbral Shrines, Tar Firewall segments
    High_Tactical = 2,     // Flare Pegs, Ignited Dead Braziers
    Critical_HeroTorch = 3 // Player's Lifeline Torch (always slot 0 when CastShadows = true)
};

enum class LightSemantic : uint8_t {
    SafeLifeline = 0,   // Hero Torch, Flare Peg, Ignited Dead Brazier (prevents Umbral death)
    NeutralShrine = 1,  // Faint Shrine ambiance (visible, not lethal-safe unless specified)
    DeceptiveHazard = 2 // Will-o'-the-Wisp false campfire lure!
};

class PointLightComponent final : public Component {
public:
    glm::vec3 Color = { 1.0f, 0.62f, 0.24f };
    float Intensity = 1.75f;
    float Radius = 4.5f;
    float Falloff = 1.8f;
    float HeightOffset = 0.85f; // Elevates light source above Y=0 entity anchor

    LightPriority Priority = LightPriority::Normal_World;
    LightSemantic Semantic = LightSemantic::NeutralShrine;
    bool CastShadows = false;
    float ShadowSoftness = 1.6f;
    float ShadowBias = 0.0025f;

    bool Flicker = true;
    float FlickerSpeed = 7.5f;
    float FlickerAmount = 0.14f;

    /// @brief Computes smoothstep radial attenuation matching Batch2D.glsl / TerrainSplatmap.glsl.
    [[nodiscard]] static float EvaluateSmoothstepAttenuation(float distance, float radius, float falloff) noexcept {
        if (!std::isfinite(distance) || !std::isfinite(radius) || !std::isfinite(falloff) || radius <= 0.0f) {
            return 0.0f;
        }
        const float safeRadius = std::max(0.05f, radius);
        if (distance >= safeRadius) {
            return 0.0f;
        }
        const float safeDist = std::max(0.0f, distance);
        const float x = std::clamp(1.0f - (safeDist / safeRadius), 0.0f, 1.0f);
        const float smooth = x * x * (3.0f - 2.0f * x);
        const float safeFalloff = std::max(0.25f, falloff);
        return std::pow(smooth, safeFalloff);
    }

    /// @brief Evaluates 2D XZ smoothstep attenuation from a light origin to an XZ sample point.
    [[nodiscard]] static float EvaluateAttenuationXZ(glm::vec2 lightXZ,
                                                     glm::vec2 queryXZ,
                                                     float radius,
                                                     float falloff) noexcept {
        const glm::vec2 delta = queryXZ - lightXZ;
        return EvaluateSmoothstepAttenuation(std::sqrt(glm::dot(delta, delta)), radius, falloff);
    }

    float EvaluateEffectiveIntensity(float timeSeconds, uint32_t phaseSeed = 0u) const {
        const float base = std::max(0.0f, Intensity);
        if (!Flicker || FlickerAmount <= 0.0f) {
            return base;
        }
        const float phase = static_cast<float>(phaseSeed % 997u) * 0.173f;
        const float t = timeSeconds * std::max(0.1f, FlickerSpeed) + phase;
        const float wave = 0.62f * std::sin(t) + 0.38f * std::sin(t * 2.37f + 1.3f);
        const float factor = std::clamp(1.0f + wave * std::clamp(FlickerAmount, 0.0f, 0.75f), 0.1f, 2.0f);
        return base * factor;
    }
};

} // namespace Engine

#endif // SCENE_POINTLIGHTCOMPONENT_H
