#include <gtest/gtest.h>
#include "Engine/Math/Isometric.h"

namespace {

TEST(IsometricMathTest, OriginMapsToOrigin) {
    const glm::vec2 screen = Engine::Isometric::WorldToScreen({0.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(screen.x, 0.0f);
    EXPECT_FLOAT_EQ(screen.y, 0.0f);

    const glm::vec3 world = Engine::Isometric::ScreenToWorld(screen);
    EXPECT_FLOAT_EQ(world.x, 0.0f);
    EXPECT_FLOAT_EQ(world.y, 0.0f);
    EXPECT_FLOAT_EQ(world.z, 0.0f);
}

TEST(IsometricMathTest, GroundPlaneRoundTripPreservesCoordinates) {
    const glm::vec3 samples[] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {4.25f, 0.0f, 7.75f},
        {-3.5f, 0.0f, 2.25f},
        {-6.125f, 0.0f, -9.875f}
    };

    for (const auto& original : samples) {
        const glm::vec2 screen = Engine::Isometric::WorldToScreen(original);
        const glm::vec3 roundTripped = Engine::Isometric::ScreenToWorld(screen);
        EXPECT_NEAR(roundTripped.x, original.x, 1e-5f);
        EXPECT_FLOAT_EQ(roundTripped.y, 0.0f);
        EXPECT_NEAR(roundTripped.z, original.z, 1e-5f);
    }
}

TEST(IsometricMathTest, VerticalElevationOffsetsScreenY) {
    const glm::vec2 ground = Engine::Isometric::WorldToScreen({2.0f, 0.0f, 3.0f});
    const glm::vec2 elevated = Engine::Isometric::WorldToScreen({2.0f, 1.75f, 3.0f});

    EXPECT_FLOAT_EQ(elevated.x, ground.x);
    EXPECT_NEAR(elevated.y - ground.y, 1.75f, 1e-5f);
}

} // namespace

