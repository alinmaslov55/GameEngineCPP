#ifndef SCENE_FOLIAGEWINDCOMPONENTS_H
#define SCENE_FOLIAGEWINDCOMPONENTS_H

#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

// ============================================================================
// WindComponent: Global 2.5D Diorama Wind Field & Interactive Foliage Parting
// ============================================================================
class WindComponent : public Component {
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

// ============================================================================
// SquashStretchComponent: Physics-Safe Secondary Spring Motion for Standees
// ============================================================================
class SquashStretchComponent : public Component {
public:
    bool Enabled = true;

    // Damped harmonic spring constants
    float Stiffness = 175.0f;
    float Damping = 14.5f;
    float MaxSquash = 0.32f;
    float MaxStretch = 0.38f;

    // Locomotion waddle & jump/land secondary impulses
    bool EnableWalkHop = true;
    float WalkBobFrequency = 11.5f;
    float WalkSquashAmount = 0.075f;
    float WalkTiltDegrees = 4.8f;
    float JumpLaunchStretch = 4.20f;
    float LandingSquashImpact = 0.85f;

    void TriggerImpulse(float verticalScaleVelocity, float tiltImpulseDegrees = 0.0f) {
        ENGINE_CORE_TRACE("SquashStretchComponent: TriggerImpulse(vertical={:.2f}, tilt={:.2f})",
                          verticalScaleVelocity, tiltImpulseDegrees);
        m_VerticalDeformVelocity += verticalScaleVelocity;
        m_TiltVelocity += tiltImpulseDegrees;
    }

    void Update(float deltaTime) override {
        if (!Enabled || deltaTime <= 0.0f) {
            return;
        }

        const float clampedDt = std::min(deltaTime, 0.05f);

        GameObject* owner = GetGameObject();
        Transform* transform = owner ? owner->GetTransform() : nullptr;
        BoxCollider2D* collider = owner ? owner->GetComponent<BoxCollider2D>() : nullptr;

        if (transform) {
            const float groundY = (collider && collider->isDynamic) ? collider->groundLevel : 0.0f;
            const bool grounded = (transform->position.y <= groundY + 1e-3f);

            if (!m_InitializedTracking) {
                m_LastPosition = transform->position;
                m_WasGrounded = grounded;
                m_InitializedTracking = true;
            } else {
                const glm::vec3 frameDelta = transform->position - m_LastPosition;
                const float invDt = 1.0f / std::max(clampedDt, 1e-4f);
                const float speedXZ = glm::length(glm::vec2(frameDelta.x, frameDelta.z)) * invDt;
                const float verticalSpeed = frameDelta.y * invDt;

                // Auto-detect jump launch (grounded -> airborne moving upward)
                if (m_WasGrounded && !grounded && verticalSpeed > 0.25f) {
                    if (m_VerticalDeformVelocity < JumpLaunchStretch * 0.5f) {
                        ENGINE_CORE_DEBUG("SquashStretchComponent: Jump launch detected on '{}' (verticalSpeed={:.2f})",
                                          owner ? owner->GetName() : "Unnamed", verticalSpeed);
                        TriggerImpulse(JumpLaunchStretch);
                    }
                }

                if (!grounded) {
                    m_LastAirVerticalSpeed = std::min(m_LastAirVerticalSpeed, verticalSpeed);
                }

                // Auto-detect landing touchdown (airborne -> grounded)
                if (!m_WasGrounded && grounded) {
                    const float impactSpeed = std::max(std::abs(m_LastAirVerticalSpeed), 3.0f);
                    const float squashVel = -std::clamp(impactSpeed * LandingSquashImpact, 2.20f, 5.60f);
                    ENGINE_CORE_DEBUG("SquashStretchComponent: Landing touchdown detected on '{}' (impactSpeed={:.2f}, squashVel={:.2f})",
                                      owner ? owner->GetName() : "Unnamed", impactSpeed, squashVel);
                    m_VerticalDeform = std::min(m_VerticalDeform, 0.0f);
                    m_VerticalDeformVelocity = std::min(m_VerticalDeformVelocity, 0.0f) + squashVel;
                    m_LastAirVerticalSpeed = 0.0f;
                }

                const bool walkingOnGround = grounded && (speedXZ > 0.15f) && EnableWalkHop;
                const float targetWalkBlend = walkingOnGround ? 1.0f : 0.0f;
                m_WalkBlend += (targetWalkBlend - m_WalkBlend) * std::min(1.0f, clampedDt * 14.0f);
                if (m_WalkBlend > 0.01f) {
                    m_WalkPhase += clampedDt * WalkBobFrequency;
                }

                m_LastPosition = transform->position;
                m_WasGrounded = grounded;
            }
        }

        // Substep spring integration at <= 1/120s for unconditional stability across high stiffness values
        const int substeps = std::max(1, static_cast<int>(std::ceil(clampedDt * 120.0f)));
        const float stepDt = clampedDt / static_cast<float>(substeps);
        const float safeStiffness = std::max(1.0f, Stiffness);
        const float safeDamping = std::max(0.0f, Damping);

        for (int i = 0; i < substeps; ++i) {
            const float verticalAccel = -safeStiffness * m_VerticalDeform - safeDamping * m_VerticalDeformVelocity;
            m_VerticalDeformVelocity += verticalAccel * stepDt;
            m_VerticalDeform += m_VerticalDeformVelocity * stepDt;

            const float tiltAccel = -(safeStiffness * 0.85f) * m_TiltAngle - safeDamping * m_TiltVelocity;
            m_TiltVelocity += tiltAccel * stepDt;
            m_TiltAngle += m_TiltVelocity * stepDt;
        }

        m_VerticalDeform = std::clamp(m_VerticalDeform, -MaxSquash * 1.25f, MaxStretch * 1.25f);
    }

    // Volume-preserving 2.5D visual scale multiplier (sx * sqrt(sy) == 1.0)
    glm::vec2 GetScaleMultiplier() const {
        if (!Enabled) {
            return { 1.0f, 1.0f };
        }
        const float walkBob = EnableWalkHop
            ? (std::sin(m_WalkPhase * 2.0f) * WalkSquashAmount * m_WalkBlend)
            : 0.0f;
        const float minSy = std::max(0.25f, 1.0f - std::clamp(MaxSquash, 0.0f, 0.70f));
        const float maxSy = 1.0f + std::clamp(MaxStretch, 0.0f, 1.0f);
        const float sy = std::clamp(1.0f + m_VerticalDeform + walkBob, minSy, maxSy);
        const float sx = 1.0f / std::sqrt(sy);
        return { sx, sy };
    }

    float GetVisualTiltDegrees() const {
        if (!Enabled) {
            return 0.0f;
        }
        const float walkWaddle = EnableWalkHop
            ? (std::sin(m_WalkPhase) * WalkTiltDegrees * m_WalkBlend)
            : 0.0f;
        return m_TiltAngle + walkWaddle;
    }

private:
    float m_VerticalDeform = 0.0f;
    float m_VerticalDeformVelocity = 0.0f;
    float m_TiltAngle = 0.0f;
    float m_TiltVelocity = 0.0f;

    float m_WalkPhase = 0.0f;
    float m_WalkBlend = 0.0f;

    bool m_InitializedTracking = false;
    glm::vec3 m_LastPosition{ 0.0f };
    bool m_WasGrounded = true;
    float m_LastAirVerticalSpeed = 0.0f;
};

} // namespace Engine

#endif // SCENE_FOLIAGEWINDCOMPONENTS_H
