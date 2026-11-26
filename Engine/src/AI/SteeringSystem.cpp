#include "Engine/AI/SteeringSystem.h"
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine {

namespace {

// Compute static 12-slot polar unit vectors (every 30 degrees = pi / 6)
std::array<glm::vec2, SteeringSystem::SlotCount> BuildSlotDirections() noexcept {
    std::array<glm::vec2, SteeringSystem::SlotCount> dirs{};
    for (size_t i = 0; i < SteeringSystem::SlotCount; ++i) {
        const float angleRad = static_cast<float>(i) * (std::numbers::pi_v<float> / 6.0f);
        dirs[i] = glm::vec2(std::cos(angleRad), std::sin(angleRad));
    }
    return dirs;
}

} // namespace

const std::array<glm::vec2, SteeringSystem::SlotCount>& SteeringSystem::GetSlotDirections() noexcept {
    static const auto s_Directions = BuildSlotDirections();
    return s_Directions;
}

glm::vec2 SteeringSystem::ComputeContextSteeringXZ(
    const Scene& scene,
    const GameObject* agent,
    const glm::vec2& targetXZ,
    std::span<const glm::vec2> neighborPositionsXZ,
    const SteeringParams& params) {
    glm::vec2 agentPosXZ{ 0.0f };
    float agentRadius = 0.35f;

    if (agent && agent->GetTransform()) {
        const glm::vec3 wp = agent->GetTransform()->GetWorldPosition();
        agentPosXZ = { wp.x, wp.z };
        if (const auto* cc = agent->GetComponent<CircleCollider2D>()) {
            const glm::vec3 scale = agent->GetTransform()->GetWorldScale();
            agentRadius = cc->Radius * std::max(std::abs(scale.x), std::abs(scale.z));
        }
    }

    return ComputeContextSteeringXZ(scene, agentPosXZ, agentRadius, targetXZ,
                                    neighborPositionsXZ, params, agent);
}

glm::vec2 SteeringSystem::ComputeContextSteeringXZ(
    const Scene& scene,
    const glm::vec2& agentPositionXZ,
    float agentRadius,
    const glm::vec2& targetXZ,
    std::span<const glm::vec2> neighborPositionsXZ,
    const SteeringParams& params,
    const GameObject* agentEntity) {
    return EvaluateContextMap(scene, agentPositionXZ, agentRadius, targetXZ,
                              neighborPositionsXZ, params, agentEntity,
                              nullptr, nullptr, nullptr);
}

glm::vec2 SteeringSystem::EvaluateContextMap(
    const Scene& scene,
    const glm::vec2& agentPositionXZ,
    float agentRadius,
    const glm::vec2& targetXZ,
    std::span<const glm::vec2> neighborPositionsXZ,
    const SteeringParams& params,
    const GameObject* agentEntity,
    std::array<float, SlotCount>* outInterest,
    std::array<float, SlotCount>* outDanger,
    std::array<float, SlotCount>* outContext) {
    if (!std::isfinite(agentPositionXZ.x) || !std::isfinite(agentPositionXZ.y) ||
        !std::isfinite(targetXZ.x) || !std::isfinite(targetXZ.y) ||
        !std::isfinite(params.MaxSpeed) || params.MaxSpeed <= 0.0f) {
        if (outInterest) outInterest->fill(0.0f);
        if (outDanger) outDanger->fill(0.0f);
        if (outContext) outContext->fill(0.0f);
        return glm::vec2(0.0f);
    }
    if (!std::isfinite(agentRadius) || agentRadius < 0.0f) {
        agentRadius = 0.35f;
    }

    auto sanitizeNonNeg = [](float v, float fallback = 0.0f) noexcept -> float {
        return (std::isfinite(v) && v >= 0.0f) ? v : fallback;
    };
    const float safeOrbitRadius = sanitizeNonNeg(params.TargetOrbitRadius, 0.0f);
    const float safeArrivalRadius = sanitizeNonNeg(params.ArrivalRadius, 0.0f);
    const float safeAvoidanceWeight = sanitizeNonNeg(params.AvoidanceWeight, 0.0f);
    const float safeFireAvoidanceWeight = sanitizeNonNeg(params.FireAvoidanceWeight, 0.0f);
    const float safeSeparationRadius = sanitizeNonNeg(params.SeparationRadius, 0.0f);
    const float safeSeparationWeight = sanitizeNonNeg(params.SeparationWeight, 0.0f);
    const float whiskerLen = std::isfinite(params.ObstacleWhiskerLength)
        ? std::clamp(params.ObstacleWhiskerLength, 0.1f, 50.0f)
        : 2.0f;

    const auto& dirs = GetSlotDirections();

    std::array<float, SlotCount> interest{};
    std::array<float, SlotCount> danger{};
    std::array<float, SlotCount> context{};

    const glm::vec2 toTarget = targetXZ - agentPositionXZ;
    const float distToTarget = glm::length(toTarget);

    // 1. Populate Interest Map
    if (safeOrbitRadius <= 0.0f) {
        // Direct seek towards target
        glm::vec2 desiredDir{ 0.0f };
        if (distToTarget > 1e-4f) {
            desiredDir = toTarget / distToTarget;
        }
        const float arrivalFactor = (safeArrivalRadius > 0.0f)
            ? std::clamp(distToTarget / safeArrivalRadius, 0.25f, 1.0f)
            : 1.0f;
        for (size_t i = 0; i < SlotCount; ++i) {
            interest[i] = std::max(0.0f, glm::dot(dirs[i], desiredDir)) * arrivalFactor;
        }
    } else {
        // Wisp Orbiting behavior: tangent to circle with radial correction
        const float R = safeOrbitRadius;
        glm::vec2 fromTarget = agentPositionXZ - targetXZ;
        float distFromTarget = glm::length(fromTarget);
        if (distFromTarget < 1e-4f) {
            fromTarget = glm::vec2(1.0f, 0.0f);
            distFromTarget = 1.0f;
        }
        const glm::vec2 radialUnit = fromTarget / distFromTarget;
        // Tangent vector
        const glm::vec2 tangent = params.OrbitClockwise
            ? glm::vec2(radialUnit.y, -radialUnit.x)
            : glm::vec2(-radialUnit.y, radialUnit.x);

        // Radial correction: if outside R, pull towards target; if inside R, push away
        const float radiusError = R - distFromTarget;
        const glm::vec2 radialCorrection = (radiusError >= 0.0f) ? radialUnit : -radialUnit;
        const float correctionStrength = std::clamp(std::abs(radiusError) / R, 0.0f, 2.0f);

        glm::vec2 desiredDir = tangent + radialCorrection * (correctionStrength * 1.25f);
        const float lenDesired = glm::length(desiredDir);
        if (lenDesired > 1e-4f) {
            desiredDir /= lenDesired;
        } else {
            desiredDir = tangent;
        }

        for (size_t i = 0; i < SlotCount; ++i) {
            interest[i] = std::max(0.0f, glm::dot(dirs[i], desiredDir));
        }
    }

    // 2. Populate Danger Map
    const uint64_t mask = (params.ObstacleCollisionMask != 0)
        ? params.ObstacleCollisionMask
        : (PhysicsLayer::Environment | PhysicsLayer::LowObstacle);

    // 2a. Static colliders & obstacles (PHY-05 raycasts)
    const float startOffset = std::max(0.0f, agentRadius) + 0.05f;
    for (size_t i = 0; i < SlotCount; ++i) {
        const glm::vec2 startPos = agentPositionXZ + dirs[i] * startOffset;
        const glm::vec2 endPos = agentPositionXZ + dirs[i] * (startOffset + whiskerLen);

        RaycastHitXZ hit;
        if (PhysicsQueries::RaycastClosestXZ(scene, startPos, endPos, mask, hit)) {
            if (agentEntity && hit.Entity == agentEntity) {
                continue;
            }
            const float hitDist = hit.Fraction * whiskerLen;
            const float closeness = std::clamp(1.0f - (hitDist / whiskerLen), 0.0f, 1.0f);
            const float obsDanger = closeness * safeAvoidanceWeight;

            danger[i] += obsDanger;
            // Diffuse to adjacent slots so agent avoids grazing boundaries
            const size_t prevIdx = (i + SlotCount - 1) % SlotCount;
            const size_t nextIdx = (i + 1) % SlotCount;
            danger[prevIdx] += 0.5f * obsDanger;
            danger[nextIdx] += 0.5f * obsDanger;
        }
    }

    // 2b. Burning Tar Cells - sample every <= 0.45m so 0.5m cells are never skipped
    if (safeFireAvoidanceWeight > 0.0f) {
        constexpr float kFireProbeStep = 0.45f;
        const float probeStart = std::max(0.0f, agentRadius);
        const float probeEnd = probeStart + whiskerLen;
        for (size_t i = 0; i < SlotCount; ++i) {
            bool hitsFire = false;
            for (float d = probeStart; d <= probeEnd + 1e-4f; d += kFireProbeStep) {
                const glm::vec2 probe = agentPositionXZ + dirs[i] * std::min(d, probeEnd);
                if (scene.IsBurningAtXZ(probe.x, probe.y)) {
                    hitsFire = true;
                    break;
                }
            }
            if (hitsFire) {
                const float fireDanger = safeFireAvoidanceWeight;
                danger[i] += fireDanger;
                const size_t prevIdx = (i + SlotCount - 1) % SlotCount;
                const size_t nextIdx = (i + 1) % SlotCount;
                danger[prevIdx] += 0.5f * fireDanger;
                danger[nextIdx] += 0.5f * fireDanger;
            }
        }
    }

    // 2c. Neighbor Separation
    if (safeSeparationRadius > 0.0f && safeSeparationWeight > 0.0f) {
        for (const auto& nbPos : neighborPositionsXZ) {
            if (!std::isfinite(nbPos.x) || !std::isfinite(nbPos.y)) {
                continue;
            }
            const glm::vec2 toNeighbor = nbPos - agentPositionXZ;
            const float nbDist = glm::length(toNeighbor);
            if (nbDist > 1e-4f && nbDist < safeSeparationRadius) {
                const float sepRatio = 1.0f - (nbDist / safeSeparationRadius);
                const float sepWeight = (sepRatio * 1.5f + sepRatio * sepRatio * 2.5f) * safeSeparationWeight;
                const glm::vec2 dirToNeighbor = toNeighbor / nbDist;

                for (size_t i = 0; i < SlotCount; ++i) {
                    const float dotVal = glm::dot(dirs[i], dirToNeighbor);
                    if (dotVal > 0.0f) {
                        danger[i] += sepWeight * dotVal;
                    }
                }
            }
        }
    }

    // 3. Mask Context
    size_t bestSlot = 0;
    float maxContext = -1.0f;

    for (size_t i = 0; i < SlotCount; ++i) {
        context[i] = std::max(0.0f, interest[i] - danger[i]);
        if (context[i] > maxContext) {
            maxContext = context[i];
            bestSlot = i;
        }
    }

    // Copy diagnostic maps if requested
    if (outInterest) *outInterest = interest;
    if (outDanger) *outDanger = danger;
    if (outContext) *outContext = context;

    // 4. Resolve Steering Direction
    glm::vec2 steerDir{ 0.0f };
    const bool escapingDanger = (maxContext <= 1e-4f);

    if (escapingDanger) {
        if (params.TargetOrbitRadius <= 0.0f && distToTarget <= 1e-4f) {
            bool hasDanger = false;
            for (float d : danger) {
                if (d > 1e-4f) {
                    hasDanger = true;
                    break;
                }
            }
            if (!hasDanger) {
                return glm::vec2(0.0f);
            }
        }
        // Fallback: All interest slots are blocked by danger.
        // Pick the slot with minimum danger to escape or hold.
        size_t minDangerSlot = 0;
        float minDanger = danger[0];
        for (size_t i = 1; i < SlotCount; ++i) {
            if (danger[i] < minDanger) {
                minDanger = danger[i];
                minDangerSlot = i;
            }
        }
        if (minDanger >= 20.0f) {
            // Completely trapped
            return glm::vec2(0.0f);
        }
        steerDir = dirs[minDangerSlot];
    } else {
        // Blend adjacent positive slots in the winning cluster around bestSlot
        const size_t prevIdx = (bestSlot + SlotCount - 1) % SlotCount;
        const size_t nextIdx = (bestSlot + 1) % SlotCount;

        glm::vec2 blended = dirs[bestSlot] * context[bestSlot];
        if (context[prevIdx] > 0.0f) {
            blended += dirs[prevIdx] * context[prevIdx];
        }
        if (context[nextIdx] > 0.0f) {
            blended += dirs[nextIdx] * context[nextIdx];
        }

        const float len = glm::length(blended);
        if (len > 1e-4f) {
            steerDir = blended / len;
        } else {
            steerDir = dirs[bestSlot];
        }
    }

    // 5. Speed & Arrival Damping (skip arrival damping when escaping danger so cornered agents move at full speed)
    float speed = params.MaxSpeed;
    if (!escapingDanger && safeOrbitRadius <= 0.0f && safeArrivalRadius > 0.0f) {
        if (distToTarget < safeArrivalRadius) {
            const float t = std::clamp(distToTarget / safeArrivalRadius, 0.0f, 1.0f);
            speed *= t;
        }
    }

    return steerDir * speed;
}

} // namespace Engine
