#ifndef SCENE_LIGHTCOMPONENTS_H
#define SCENE_LIGHTCOMPONENTS_H

#include "Engine/Scene/Component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

class DirectionalLightComponent final : public Component {
public:
    float ElevationDegrees = 42.0f; // [10, 85] degrees above horizon
    float AzimuthDegrees = -38.0f;  // [-180, 180] horizontal sun direction

    glm::vec3 SunColor = { 1.0f, 0.92f, 0.76f };
    float SunIntensity = 1.05f;

    // Cool indigo-violet ambient sky/shadow fill (signature Cult of the Lamb shade)
    glm::vec3 AmbientColor = { 0.38f, 0.43f, 0.62f };
    float AmbientIntensity = 0.62f;

    bool CastShadows = true;
    float ShadowSoftness = 1.35f;
    float ShadowBias = 0.0018f;

    // Unit vector pointing FROM the sun TOWARD the ground scene
    glm::vec3 GetLightDirection() const {
        const float elevRad = glm::radians(std::clamp(ElevationDegrees, 5.0f, 89.0f));
        const float azimRad = glm::radians(AzimuthDegrees);
        const float cosElev = std::cos(elevRad);

        const glm::vec3 dir = {
            -std::sin(azimRad) * cosElev,
            -std::sin(elevRad),
            -std::cos(azimRad) * cosElev
        };
        return glm::normalize(dir);
    }

    // Unit horizontal vector perpendicular to the sun's ground direction so 2.5D cutout
    // standees face the sun un-foreshortened during Pass 0 across all 360° azimuths
    glm::vec3 GetSunRightXZ() const {
        const glm::vec3 dir = GetLightDirection();
        const glm::vec3 horiz = { dir.x, 0.0f, dir.z };
        const float lenSq = glm::dot(horiz, horiz);
        if (lenSq < 1e-6f) {
            return { 1.0f, 0.0f, 0.0f };
        }
        const glm::vec3 forwardXZ = horiz / std::sqrt(lenSq);
        return glm::normalize(glm::cross(forwardXZ, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    // Builds an orthographic Light ViewProjection matrix snapped to shadow-map texel increments
    // in light space so panning or following the player never shimmers shadow silhouette edges
    glm::mat4 ComputeLightSpaceMatrix(const glm::vec3& focusPoint,
                                      float orthoRadius,
                                      uint32_t shadowMapResolution) const {
        const float safeRadius = std::max(2.0f, orthoRadius);
        const uint32_t safeRes = std::max(64u, shadowMapResolution);
        const glm::vec3 lightDir = GetLightDirection();

        const glm::vec3 upHint = (std::abs(lightDir.y) > 0.98f)
            ? glm::vec3(0.0f, 0.0f, -1.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);

        // Construct stabilized light basis around the origin first, then snap focusPoint in light XY
        const glm::mat4 basisView = glm::lookAt(-lightDir * 40.0f, glm::vec3(0.0f), upHint);
        glm::vec4 lightSpaceFocus = basisView * glm::vec4(focusPoint, 1.0f);

        const float worldUnitsPerTexel = (2.0f * safeRadius) / static_cast<float>(safeRes);
        lightSpaceFocus.x = std::floor(lightSpaceFocus.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        lightSpaceFocus.y = std::floor(lightSpaceFocus.y / worldUnitsPerTexel) * worldUnitsPerTexel;

        const glm::vec3 snappedWorldFocus = glm::vec3(glm::inverse(basisView) * lightSpaceFocus);
        const glm::mat4 lightView = glm::lookAt(snappedWorldFocus - lightDir * 40.0f, snappedWorldFocus, upHint);
        const glm::mat4 lightProj = glm::ortho(-safeRadius, safeRadius, -safeRadius, safeRadius, 1.0f, 90.0f);
        return lightProj * lightView;
    }
};

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
        if (radius <= 0.0f) {
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

#endif // SCENE_LIGHTCOMPONENTS_H

