#include <gtest/gtest.h>
#include "Engine/Scene/LightComponents.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Renderer/ShadowMap.h"
#include <cmath>

namespace {

TEST(LightingMathTest, DirectionalBasisTexelSnappingAndPointLightFlicker) {
    Engine::DirectionalLightComponent dirLight;

    // 1. Verify unit-length downward sun direction & orthogonal horizontal standee shadow basis across 360° azimuths
    const float testAzimuths[] = { -180.0f, -90.0f, -42.0f, 0.0f, 65.0f, 135.0f };
    for (float azim : testAzimuths) {
        dirLight.ElevationDegrees = 40.0f;
        dirLight.AzimuthDegrees = azim;

        const glm::vec3 sunDir = dirLight.GetLightDirection();
        EXPECT_NEAR(glm::length(sunDir), 1.0f, 1e-5f);
        EXPECT_LT(sunDir.y, -0.5f); // Points downward onto Y=0 diorama floor

        const glm::vec3 sunRightXZ = dirLight.GetSunRightXZ();
        EXPECT_NEAR(glm::length(sunRightXZ), 1.0f, 1e-5f);
        EXPECT_NEAR(sunRightXZ.y, 0.0f, 1e-6f);
        // Standee shadow plane right-axis must be strictly perpendicular to horizontal sun rays
        EXPECT_NEAR(glm::dot(sunRightXZ, glm::vec3(sunDir.x, 0.0f, sunDir.z)), 0.0f, 1e-5f);
    }

    // 2. Verify texel-snapped LightSpaceMatrix eliminates sub-texel shadow edge shimmer during camera pan:
    //    For ANY camera pan, shadow-map UV coordinates of a static world point may only shift by exact
    //    integer texel increments (1 / shadowRes), and micro-pans inside a texel cell shift by 0 texels.
    dirLight.ElevationDegrees = 42.0f;
    dirLight.AzimuthDegrees = -38.0f;
    const float orthoRadius = 15.0f;
    const uint32_t shadowRes = 2048u;
    const float texelSizeWorld = (2.0f * orthoRadius) / static_cast<float>(shadowRes);

    // Pick a focus point guaranteed to sit near the center of its light-space texel cell
    const glm::vec3 snappedOriginFocus = { 0.0f, 0.0f, 0.0f };
    const glm::vec3 cellCenterFocus = snappedOriginFocus + dirLight.GetSunRightXZ() * (0.5f * texelSizeWorld);
    const glm::vec3 microPannedFocus = cellCenterFocus + dirLight.GetSunRightXZ() * (0.15f * texelSizeWorld);

    const glm::mat4 matA = dirLight.ComputeLightSpaceMatrix(cellCenterFocus, orthoRadius, shadowRes);
    const glm::mat4 matB = dirLight.ComputeLightSpaceMatrix(microPannedFocus, orthoRadius, shadowRes);
    const glm::vec4 clipA = matA * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
    const glm::vec4 clipB = matB * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
    EXPECT_NEAR(clipA.x, clipB.x, 1e-5f);
    EXPECT_NEAR(clipA.y, clipB.y, 1e-5f);

    // Arbitrary fractional world pans must always quantize to exact integer texel steps in shadow UV [0, 1]
    const glm::vec3 arbitraryPans[] = {
        {  0.371f, 0.0f,  0.829f },
        { -1.413f, 0.0f,  0.617f },
        {  2.104f, 0.0f, -1.732f }
    };
    const glm::vec2 uvBase = glm::vec2(clipA.x, clipA.y) * 0.5f + 0.5f;
    for (const auto& panOffset : arbitraryPans) {
        const glm::mat4 matPanned = dirLight.ComputeLightSpaceMatrix(cellCenterFocus + panOffset, orthoRadius, shadowRes);
        const glm::vec4 clipPanned = matPanned * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
        const glm::vec2 uvPanned = glm::vec2(clipPanned.x, clipPanned.y) * 0.5f + 0.5f;
        const glm::vec2 texelShift = (uvPanned - uvBase) * static_cast<float>(shadowRes);
        EXPECT_NEAR(texelShift.x, std::round(texelShift.x), 1e-3f);
        EXPECT_NEAR(texelShift.y, std::round(texelShift.y), 1e-3f);
    }

    // 3. PointLightComponent steady vs flickering evaluation & phase decorrelation
    Engine::PointLightComponent brazierLight;
    brazierLight.Intensity = 2.0f;
    brazierLight.Flicker = false;
    EXPECT_NEAR(brazierLight.EvaluateEffectiveIntensity(1.25f, 42u), 2.0f, 1e-6f);

    brazierLight.Flicker = true;
    brazierLight.FlickerSpeed = 8.0f;
    brazierLight.FlickerAmount = 0.20f;
    const float t0 = brazierLight.EvaluateEffectiveIntensity(0.15f, 101u);
    const float t1 = brazierLight.EvaluateEffectiveIntensity(0.55f, 101u);
    const float seedB = brazierLight.EvaluateEffectiveIntensity(0.15f, 202u);
    EXPECT_NE(t0, t1);
    EXPECT_NE(t0, seedB); // Distinct entities do not pulse in robotic lockstep
    EXPECT_GE(t0, 2.0f * 0.80f - 1e-4f);
    EXPECT_LE(t0, 2.0f * 1.20f + 1e-4f);

    // 4. Headless ShadowMap lifecycle safety (no OpenGL context active in unit test runner)
    Engine::ShadowMap::Init(2048);
    EXPECT_FALSE(Engine::ShadowMap::IsValid());
    EXPECT_EQ(Engine::ShadowMap::GetResolution(), 2048u);
    Engine::ShadowMap::Shutdown();
}

TEST(SceneLightingDataTest, DefaultsInitialization) {
    Engine::SceneLightingData data;
    EXPECT_FALSE(data.LightingActive);
    EXPECT_FALSE(data.ShadowsEnabled);
    EXPECT_EQ(data.PointLightCount, 0u);
    EXPECT_FLOAT_EQ(data.AmbientIntensity, 1.0f);
}

} // namespace

