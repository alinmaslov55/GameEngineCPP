#include "Engine/AI/Ballistics.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine::Ballistics {

JumpArcSolution SolveInterceptHopArc(
    glm::vec2 jumperXZ, glm::vec2 targetXZ, glm::vec2 targetVelXZ,
    float maxHopRange, float flightDuration, float gravityY, float leadFactor) noexcept {
    JumpArcSolution solution{};

    // Enforce valid positive flight time and downward gravity
    const float T = std::max(1e-4f, flightDuration);
    const float g_y = (gravityY < 0.0f) ? gravityY : -22.0f;

    // 1. Predict target position at intercept time: p_pred = p_target + lambda * v_target * T
    const glm::vec2 predictedTarget = targetXZ + targetVelXZ * (leadFactor * T);

    // 2. Compute hop vector and clamp to maxHopRange:
    // p_land = p_jumper + clampLength(p_pred - p_jumper, R_max)
    const glm::vec2 jumpVec = predictedTarget - jumperXZ;
    const float jumpDist = glm::length(jumpVec);

    glm::vec2 landingSpot = predictedTarget;
    if (maxHopRange > 0.0f && jumpDist > maxHopRange) {
        landingSpot = jumperXZ + (jumpVec / jumpDist) * maxHopRange;
    }

    // 3. Horizontal velocity: v_xz = (p_land - p_jumper) / T
    solution.VelocityXZ = (landingSpot - jumperXZ) / T;
    solution.LandingSpotXZ = landingSpot;
    solution.FlightDuration = T;

    // 4. Vertical launch velocity: v_y,0 = -0.5 * g_y * T
    solution.InitialVelocityY = -0.5f * g_y * T;

    // 5. Peak height: y_max = -0.125 * g_y * T^2
    solution.PeakHeight = -0.125f * g_y * (T * T);

    return solution;
}

} // namespace Engine::Ballistics
