#include <gtest/gtest.h>
#include "Engine/AI/Ballistics.h"
#include <glm/glm.hpp>
#include <cmath>

using namespace Engine;
using namespace Engine::Ballistics;

TEST(BallisticsTest, StationaryTargetHop)
{
    glm::vec2 jumper(0.0f, 0.0f);
    glm::vec2 target(5.0f, 0.0f);
    glm::vec2 targetVel(0.0f, 0.0f);
    float maxRange = 10.0f;
    float flightDuration = 0.5f;
    float gravityY = -20.0f;

    JumpArcSolution sol = SolveInterceptHopArc(jumper, target, targetVel, maxRange, flightDuration, gravityY);

    EXPECT_NEAR(sol.LandingSpotXZ.x, 5.0f, 0.001f);
    EXPECT_NEAR(sol.LandingSpotXZ.y, 0.0f, 0.001f);
    EXPECT_NEAR(sol.FlightDuration, 0.5f, 0.001f);

    // VelocityXZ = delta / T = (5, 0) / 0.5 = (10, 0)
    EXPECT_NEAR(sol.VelocityXZ.x, 10.0f, 0.001f);
    EXPECT_NEAR(sol.VelocityXZ.y, 0.0f, 0.001f);

    // InitialVelocityY = -0.5 * (-20) * 0.5 = 5.0
    EXPECT_NEAR(sol.InitialVelocityY, 5.0f, 0.001f);
}

TEST(BallisticsTest, TargetLeadPrediction)
{
    glm::vec2 jumper(0.0f, 0.0f);
    glm::vec2 target(10.0f, 0.0f);
    glm::vec2 targetVel(4.0f, 0.0f);
    float maxRange = 25.0f;
    float flightDuration = 0.5f;
    float gravityY = -20.0f;
    float leadFactor = 0.75f;

    // Predicted: target + targetVel * leadFactor * flightDuration = (10, 0) + (4 * 0.75 * 0.5, 0) = (11.5, 0)
    JumpArcSolution sol = SolveInterceptHopArc(jumper, target, targetVel, maxRange, flightDuration, gravityY, leadFactor);

    EXPECT_NEAR(sol.LandingSpotXZ.x, 11.5f, 0.001f);
    EXPECT_NEAR(sol.LandingSpotXZ.y, 0.0f, 0.001f);
    EXPECT_NEAR(sol.VelocityXZ.x, 23.0f, 0.001f); // 11.5 / 0.5
}

TEST(BallisticsTest, MaxRangeClamping)
{
    glm::vec2 jumper(0.0f, 0.0f);
    glm::vec2 target(20.0f, 0.0f);
    glm::vec2 targetVel(0.0f, 0.0f);
    float maxRange = 8.0f;
    float flightDuration = 0.8f;
    float gravityY = -20.0f;

    JumpArcSolution sol = SolveInterceptHopArc(jumper, target, targetVel, maxRange, flightDuration, gravityY);

    EXPECT_NEAR(sol.LandingSpotXZ.x, 8.0f, 0.001f);
    EXPECT_NEAR(sol.LandingSpotXZ.y, 0.0f, 0.001f);
    EXPECT_NEAR(sol.VelocityXZ.x, 10.0f, 0.001f); // 8.0 / 0.8
}

TEST(BallisticsTest, PeakHeightFormula)
{
    float flightDuration = 0.8f;
    float gravityY = -20.0f;
    float maxHopRange = 10.0f;

    JumpArcSolution sol = SolveInterceptHopArc(glm::vec2(0.0f), glm::vec2(5.0f), glm::vec2(0.0f), maxHopRange, flightDuration, gravityY);

    // Initial Vy = -0.5 * (-20) * 0.8 = 8.0 m/s
    EXPECT_NEAR(sol.InitialVelocityY, 8.0f, 0.001f);

    // Theoretical apex y_max = -1/8 * gy * T^2 = -1/8 * (-20) * 0.64 = 1.6m
    float theoreticalPeak = -0.125f * gravityY * (flightDuration * flightDuration);
    EXPECT_NEAR(theoreticalPeak, 1.6f, 0.001f);

    // At t = T/2 = 0.4s: y(t) = v0*t + 0.5*g*t^2 = 8*0.4 + 0.5*(-20)*0.16 = 3.2 - 1.6 = 1.6m
    float tMid = flightDuration * 0.5f;
    float yMid = sol.InitialVelocityY * tMid + 0.5f * gravityY * tMid * tMid;
    EXPECT_NEAR(yMid, theoreticalPeak, 0.001f);
}

TEST(BallisticsTest, RoadmapVerification_KinematicIntegration42Steps)
{
    // Roadmap specification:
    // Solve a 0.70s hop intercepting a target moving at (3.0, 0.0) m/s,
    // integrate PHY-01 kinematics for 42 fixed steps (1/60s),
    // and assert touchdown position matches LandingSpotXZ within 0.02m.

    const glm::vec2 jumper(2.0f, 3.0f);
    const glm::vec2 target(5.0f, 3.0f);
    const glm::vec2 targetVel(3.0f, 0.0f);
    const float maxHopRange = 15.0f;
    const float flightDuration = 0.70f;
    const float gravityY = -22.0f;
    const float leadFactor = 0.75f;

    JumpArcSolution sol = SolveInterceptHopArc(jumper, target, targetVel, maxHopRange, flightDuration, gravityY, leadFactor);

    // Expected predicted landing spot:
    // delta = target + targetVel * leadFactor * T - jumper
    // delta = (5, 3) + (3 * 0.75 * 0.70, 0) - (2, 3) = (5 + 1.575 - 2, 0) = (4.575, 0)
    // LandingSpotXZ = jumper + delta = (6.575, 3.0)
    EXPECT_NEAR(sol.LandingSpotXZ.x, 6.575f, 0.001f);
    EXPECT_NEAR(sol.LandingSpotXZ.y, 3.0f, 0.001f);

    // Initial vertical velocity:
    // v_y0 = -0.5 * (-22.0) * 0.70 = 7.7 m/s
    EXPECT_NEAR(sol.InitialVelocityY, 7.7f, 0.001f);

    // Simulate 42 steps at dt = 1/60s (42 * (1/60) = 0.70s exact!)
    constexpr int totalSteps = 42;
    constexpr float dt = 1.0f / 60.0f;

    glm::vec2 posXZ = jumper;
    float posY = 0.0f;
    glm::vec2 velXZ = sol.VelocityXZ;
    float velY = sol.InitialVelocityY;

    // PHY-01 semi-implicit / symplectic Euler or standard kinematic integration:
    // With LinearDamping = 0 during flight (airborne leap mode), velXZ remains constant.
    for (int step = 0; step < totalSteps; ++step)
    {
        velY += gravityY * dt;
        posXZ += velXZ * dt;
        posY += velY * dt;
    }

    // Assert touchdown position matches LandingSpotXZ within 0.02m
    float distToTargetLanding = glm::distance(posXZ, sol.LandingSpotXZ);
    EXPECT_LT(distToTargetLanding, 0.02f);
    EXPECT_NEAR(posXZ.x, sol.LandingSpotXZ.x, 0.02f);
    EXPECT_NEAR(posXZ.y, sol.LandingSpotXZ.y, 0.02f);

    // Height after exactly 42 steps (0.70s) should return to ground (0.0m) within kinematic Euler margin
    EXPECT_NEAR(posY, 0.0f, 0.15f);
}

TEST(BallisticsTest, NonPositiveRangeOrFlightDurationReturnsZeroVelocityAtJumper)
{
    const glm::vec2 jumper(4.0f, -7.0f);
    const glm::vec2 target(20.0f, 10.0f);

    JumpArcSolution solZeroBoth = SolveInterceptHopArc(jumper, target, { 2.0f, 0.0f }, 0.0f, 0.0f, -22.0f);
    EXPECT_FLOAT_EQ(solZeroBoth.VelocityXZ.x, 0.0f);
    EXPECT_FLOAT_EQ(solZeroBoth.VelocityXZ.y, 0.0f);
    EXPECT_FLOAT_EQ(solZeroBoth.InitialVelocityY, 0.0f);
    EXPECT_FLOAT_EQ(solZeroBoth.FlightDuration, 0.0f);
    EXPECT_FLOAT_EQ(solZeroBoth.LandingSpotXZ.x, jumper.x);
    EXPECT_FLOAT_EQ(solZeroBoth.LandingSpotXZ.y, jumper.y);

    JumpArcSolution solNegRange = SolveInterceptHopArc(jumper, target, { 0.0f, 0.0f }, -5.0f, 0.5f, -22.0f);
    EXPECT_FLOAT_EQ(solNegRange.VelocityXZ.x, 0.0f);
    EXPECT_FLOAT_EQ(solNegRange.VelocityXZ.y, 0.0f);
    EXPECT_FLOAT_EQ(solNegRange.LandingSpotXZ.x, jumper.x);
    EXPECT_FLOAT_EQ(solNegRange.LandingSpotXZ.y, jumper.y);

    JumpArcSolution solZeroDuration = SolveInterceptHopArc(jumper, target, { 0.0f, 0.0f }, 10.0f, 0.0f, -22.0f);
    EXPECT_FLOAT_EQ(solZeroDuration.VelocityXZ.x, 0.0f);
    EXPECT_FLOAT_EQ(solZeroDuration.VelocityXZ.y, 0.0f);
    EXPECT_FLOAT_EQ(solZeroDuration.LandingSpotXZ.x, jumper.x);
    EXPECT_FLOAT_EQ(solZeroDuration.LandingSpotXZ.y, jumper.y);
}
