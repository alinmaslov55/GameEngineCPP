#ifndef AI_BALLISTICS_H
#define AI_BALLISTICS_H

#include <glm/vec2.hpp>

namespace Engine::Ballistics {

struct JumpArcSolution {
    glm::vec2 VelocityXZ{ 0.0f };
    float InitialVelocityY = 0.0f;
    glm::vec2 LandingSpotXZ{ 0.0f };
    float FlightDuration = 0.65f;
    float PeakHeight = 0.0f;
};

/// @brief Closed-form 2.5D ballistic trajectory intercept solver
/// @param jumperXZ Initial position of the leaping entity on XZ
/// @param targetXZ Current position of the target entity on XZ
/// @param targetVelXZ Current velocity of the target entity on XZ
/// @param maxHopRange Maximum hop radius (m) to clamp leap destination
/// @param flightDuration Duration of the flight phase in seconds (T)
/// @param gravityY Downward vertical acceleration (m/s^2, < 0, e.g. -22.0f)
/// @param leadFactor Target prediction lead scalar (default: 0.75f)
/// @return Solved horizontal velocity XZ, vertical initial launch velocity Y, and landing spot
[[nodiscard]] JumpArcSolution SolveInterceptHopArc(
    glm::vec2 jumperXZ, glm::vec2 targetXZ, glm::vec2 targetVelXZ,
    float maxHopRange, float flightDuration, float gravityY, float leadFactor = 0.75f) noexcept;

} // namespace Engine::Ballistics

#endif // AI_BALLISTICS_H
