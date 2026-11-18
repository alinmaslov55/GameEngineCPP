#ifndef SCENE_SQUASHSTRETCHCOMPONENT_H
#define SCENE_SQUASHSTRETCHCOMPONENT_H

#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

class SquashStretchComponent final : public Component {
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

    void Reset() noexcept {
        m_VerticalDeform = 0.0f;
        m_VerticalDeformVelocity = 0.0f;
        m_TiltAngle = 0.0f;
        m_TiltVelocity = 0.0f;
        m_WalkPhase = 0.0f;
        m_WalkBlend = 0.0f;
        m_InitializedTracking = false;
        m_LastPosition = glm::vec3{ 0.0f };
        m_WasGrounded = true;
        m_LastAirVerticalSpeed = 0.0f;
    }

    void OnPoolAcquire() override { Reset(); }
    void OnPoolRelease() override { Reset(); }

    void Update(float deltaTime) override {
        if (!Enabled || deltaTime <= 0.0f) {
            return;
        }

        const float clampedDt = std::min(deltaTime, 0.05f);

        GameObject* owner = GetGameObject();
        Transform* transform = owner ? owner->GetTransform() : nullptr;
        Rigidbody2D* rb = owner ? owner->GetComponent<Rigidbody2D>() : nullptr;

        if (transform) {
            const float groundY = rb ? rb->GroundLevelY : 0.0f;
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

#endif // SCENE_SQUASHSTRETCHCOMPONENT_H
