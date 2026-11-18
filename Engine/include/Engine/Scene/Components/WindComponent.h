#ifndef SCENE_WINDCOMPONENT_H
#define SCENE_WINDCOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

class WindComponent final : public Component {
public:
    enum class Preset {
        GentleMeadowBreeze = 0,
        AutumnGusts = 1,
        RitualStorm = 2,
        StillAir = 3
    };

    bool Enabled = true;
    float DirectionDegrees = 25.0f;    // Wind travel direction on the XZ ground plane
    float Strength = 0.16f;            // Base horizontal top-edge sway amplitude (meters)
    float Speed = 2.15f;               // Primary wind oscillation angular speed (rad/s)
    float Gustiness = 0.55f;           // [0.0, 1.0] low-frequency gust envelope modulation
    float GustFrequency = 0.42f;       // Secondary gust envelope angular speed (rad/s)
    float SpatialWaveScale = 0.55f;    // Phase radians per meter across the clearing

    // Interactive Foliage Parting when dynamic colliders (Player / NPCs) brush past
    bool InteractiveRustle = true;
    float PlayerInteractionRadius = 1.25f;
    float PlayerPushStrength = 0.34f;

    void ApplyPreset(Preset preset) {
        const char* presetName = "Unknown";
        switch (preset) {
            case Preset::GentleMeadowBreeze:
                presetName = "GentleMeadowBreeze";
                Enabled = true;
                DirectionDegrees = 25.0f;
                Strength = 0.16f;
                Speed = 2.15f;
                Gustiness = 0.55f;
                GustFrequency = 0.42f;
                SpatialWaveScale = 0.55f;
                InteractiveRustle = true;
                PlayerInteractionRadius = 1.25f;
                PlayerPushStrength = 0.34f;
                break;

            case Preset::AutumnGusts:
                presetName = "AutumnGusts";
                Enabled = true;
                DirectionDegrees = -35.0f;
                Strength = 0.28f;
                Speed = 3.10f;
                Gustiness = 0.82f;
                GustFrequency = 0.68f;
                SpatialWaveScale = 0.72f;
                InteractiveRustle = true;
                PlayerInteractionRadius = 1.35f;
                PlayerPushStrength = 0.42f;
                break;

            case Preset::RitualStorm:
                presetName = "RitualStorm";
                Enabled = true;
                DirectionDegrees = -58.0f;
                Strength = 0.44f;
                Speed = 4.35f;
                Gustiness = 0.92f;
                GustFrequency = 0.95f;
                SpatialWaveScale = 0.88f;
                InteractiveRustle = true;
                PlayerInteractionRadius = 1.45f;
                PlayerPushStrength = 0.48f;
                break;

            case Preset::StillAir:
                presetName = "StillAir";
                Enabled = false;
                Strength = 0.0f;
                Gustiness = 0.0f;
                break;
        }
        ENGINE_CORE_DEBUG("WindComponent: Applied preset '{}' (Dir={:.1f} deg, Strength={:.2f}, Speed={:.2f})",
                          presetName, DirectionDegrees, Strength, Speed);
    }

    glm::vec2 GetWindDirectionXZ() const {
        const float rad = glm::radians(DirectionDegrees);
        return { std::cos(rad), std::sin(rad) };
    }

    float ComputeSpatialPhase(const glm::vec3& anchorPos, float phaseOffset = 0.0f) const {
        const glm::vec2 dirXZ = GetWindDirectionXZ();
        const glm::vec2 crossXZ = { -dirXZ.y, dirXZ.x };
        const glm::vec2 posXZ = { anchorPos.x, anchorPos.z };
        const float alongWind = glm::dot(posXZ, dirXZ) * SpatialWaveScale;
        const float crossDecorrelation = glm::dot(posXZ, crossXZ) * (SpatialWaveScale * 0.31f);
        return alongWind + crossDecorrelation + phaseOffset;
    }

    // Exact CPU mirror of the GLSL 410 vertex-shader top-anchored wind formula
    float EvaluateSwayDisplacement(float timeSeconds,
                                   float topWeight,
                                   float spatialPhase,
                                   float proximityBend = 0.0f) const {
        if (topWeight <= 0.0001f) {
            return 0.0f;
        }

        float sway = 0.0f;
        if (Enabled) {
            const float primary = std::sin(timeSeconds * Speed + spatialPhase);
            const float detail  = 0.32f * std::sin(timeSeconds * (Speed * 2.17f) + spatialPhase * 1.73f + 1.1f);
            const float gustOsc = 0.55f + 0.45f * std::sin(timeSeconds * GustFrequency + spatialPhase * 0.37f);
            const float gustEnv = glm::mix(1.0f, gustOsc, std::clamp(Gustiness, 0.0f, 1.0f));
            sway = (primary + detail) * Strength * gustEnv * topWeight;
        }
        return sway + proximityBend;
    }

    // Evaluates signed top-edge bend (along swayRightAxis) when a dynamic entity brushes past foliage
    float EvaluateProximityBend(const glm::vec3& standeeAnchor,
                                const glm::vec3& interactorPos,
                                const glm::vec3& swayRightAxis,
                                float standeeSwayWeight,
                                float timeSeconds = 0.0f,
                                float interactorSpeedXZ = 0.0f) const {
        if (!InteractiveRustle || standeeSwayWeight <= 0.0001f || PlayerInteractionRadius <= 0.05f) {
            return 0.0f;
        }

        const float verticalGap = std::abs(interactorPos.y - standeeAnchor.y);
        if (verticalGap >= 1.35f) {
            return 0.0f;
        }
        const float verticalFactor = 1.0f - std::clamp(verticalGap / 1.35f, 0.0f, 1.0f);

        const glm::vec2 deltaXZ = { standeeAnchor.x - interactorPos.x, standeeAnchor.z - interactorPos.z };
        const float dist = glm::length(deltaXZ);
        if (dist >= PlayerInteractionRadius) {
            return 0.0f;
        }

        const float t = 1.0f - (dist / PlayerInteractionRadius);
        const float smoothFalloff = t * t * (3.0f - 2.0f * t);

        glm::vec2 awayDirXZ = { 1.0f, 0.0f };
        if (dist > 1e-4f) {
            awayDirXZ = deltaXZ / dist;
        }

        float sideProjection = awayDirXZ.x * swayRightAxis.x + awayDirXZ.y * swayRightAxis.z;
        // Ensure dead-center forward approaches still part gracefully to one side
        if (std::abs(sideProjection) < 0.25f) {
            const float fallbackSign = (std::sin(standeeAnchor.x * 3.7f + standeeAnchor.z * 2.3f) >= 0.0f) ? 1.0f : -1.0f;
            sideProjection = glm::mix(sideProjection, fallbackSign * 0.45f, smoothFalloff);
        }

        const float motionFactor = std::min(1.0f, interactorSpeedXZ * 0.35f);
        const float rustleFlutter = 1.0f + 0.24f * motionFactor *
            std::sin(timeSeconds * 18.5f + standeeAnchor.x * 3.1f + standeeAnchor.z * 2.7f);

        const float weightClamp = std::clamp(standeeSwayWeight, 0.25f, 1.35f);
        return sideProjection * smoothFalloff * verticalFactor * PlayerPushStrength * weightClamp * rustleFlutter;
    }
};

} // namespace Engine

#endif // SCENE_WINDCOMPONENT_H
