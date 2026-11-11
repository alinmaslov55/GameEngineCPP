#include <gtest/gtest.h>
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/TorchShadowMap.h"
#include <cmath>
#include <string>

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
    EXPECT_EQ(Engine::SceneLightingData::MaxPointLights, 32u);
    EXPECT_FLOAT_EQ(data.AmbientIntensity, 1.0f);
}

TEST(SceneLightingPipelineTest, PrioritySortingCaps32AndPlacesHeroTorchFirst) {
    Engine::Scene scene("PrioritySortTestScene");
    Engine::SceneRenderer renderer;
    renderer.OnViewportResize(scene, 1920u, 1080u);

    const glm::vec3 camFocal = renderer.GetCamera().GetFocalPoint();

    // Populate indices 0..38 with mixed Low_VFX / Normal_World / High_Tactical lights inside the frustum
    for (int i = 0; i < 39; ++i) {
        auto* go = scene.CreateGameObject("FillerLight_" + std::to_string(i));
        const float offsetX = static_cast<float>((i % 5) - 2) * 0.7f;
        const float offsetZ = static_cast<float>((i / 5) - 3) * 0.6f;
        go->GetTransform()->position = { camFocal.x + offsetX, 0.0f, camFocal.z + offsetZ };

        auto* pl = go->AddComponent<Engine::PointLightComponent>();
        pl->Radius = 3.5f;
        pl->Flicker = false;
        pl->Intensity = 1.0f + static_cast<float>(i) * 0.01f;
        pl->CastShadows = false;
        if (i < 20) {
            pl->Priority = Engine::LightPriority::Low_VFX;
        } else if (i < 30) {
            pl->Priority = Engine::LightPriority::Normal_World;
        } else {
            pl->Priority = Engine::LightPriority::High_Tactical;
        }
    }

    // Place Critical Hero Torch at entity index 39 (worst entity-order position)
    auto* hero = scene.CreateGameObject("HeroTorch");
    hero->GetTransform()->position = { camFocal.x + 1.5f, 0.0f, camFocal.z };
    auto* heroTorch = hero->AddComponent<Engine::PointLightComponent>();
    heroTorch->Priority = Engine::LightPriority::Critical_HeroTorch;
    heroTorch->CastShadows = true;
    heroTorch->Color = { 1.0f, 0.42f, 0.12f };
    heroTorch->Intensity = 2.5f;
    heroTorch->Radius = 7.75f;
    heroTorch->Falloff = 1.65f;
    heroTorch->ShadowSoftness = 1.8f;
    heroTorch->ShadowBias = 0.002f;
    heroTorch->Flicker = false;

    const Engine::SceneRenderContext ctx = renderer.GatherRenderContext(scene);

    ASSERT_EQ(ctx.lighting.PointLightCount, 32u);
    const auto& slot0 = ctx.lighting.PointLights[0];
    EXPECT_FLOAT_EQ(slot0.PositionAndRadius.w, 7.75f);
    EXPECT_FLOAT_EQ(slot0.ColorAndIntensity.r, 1.0f);
    EXPECT_FLOAT_EQ(slot0.ColorAndIntensity.g, 0.42f);
    EXPECT_FLOAT_EQ(slot0.ColorAndIntensity.b, 0.12f);
    EXPECT_FLOAT_EQ(slot0.ColorAndIntensity.a, 2.5f);
    EXPECT_FLOAT_EQ(slot0.Params.x, 1.65f);
    EXPECT_FLOAT_EQ(slot0.Params.y, 1.0f);
    EXPECT_FLOAT_EQ(slot0.Params.z, 1.8f);
    EXPECT_FLOAT_EQ(slot0.Params.w, 0.002f);

    // Slots 1..9 must hold all 9 High_Tactical lights (Intensity in [1.30, 1.38]), before Normal_World [1.20, 1.29]
    for (uint32_t i = 1; i <= 9; ++i) {
        EXPECT_GE(ctx.lighting.PointLights[i].ColorAndIntensity.a, 1.295f);
        EXPECT_FLOAT_EQ(ctx.lighting.PointLights[i].Params.y, 0.0f);
    }
    for (uint32_t i = 10; i <= 19; ++i) {
        EXPECT_GE(ctx.lighting.PointLights[i].ColorAndIntensity.a, 1.195f);
        EXPECT_LT(ctx.lighting.PointLights[i].ColorAndIntensity.a, 1.295f);
    }
}

TEST(SceneLightingPipelineTest, DistanceTieBreakerAndHeadlessUBOAlignment) {
    Engine::Scene scene("TieBreakerScene");
    Engine::SceneRenderer renderer;
    renderer.OnViewportResize(scene, 1920u, 1080u);
    const glm::vec3 camFocal = renderer.GetCamera().GetFocalPoint();

    // Farther Normal_World added first
    auto* farGo = scene.CreateGameObject("FarNormal");
    farGo->GetTransform()->position = { camFocal.x + 3.5f, 0.0f, camFocal.z };
    auto* farPl = farGo->AddComponent<Engine::PointLightComponent>();
    farPl->Priority = Engine::LightPriority::Normal_World;
    farPl->Intensity = 1.1f;
    farPl->Flicker = false;

    // Closer Normal_World added second
    auto* nearGo = scene.CreateGameObject("NearNormal");
    nearGo->GetTransform()->position = { camFocal.x, 0.0f, camFocal.z };
    auto* nearPl = nearGo->AddComponent<Engine::PointLightComponent>();
    nearPl->Priority = Engine::LightPriority::Normal_World;
    nearPl->Intensity = 2.2f;
    nearPl->Flicker = false;

    // Shadow caster (Low_VFX priority but CastShadows = true -> ranks first!)
    auto* shadowGo = scene.CreateGameObject("ShadowLow");
    shadowGo->GetTransform()->position = { camFocal.x + 4.0f, 0.0f, camFocal.z };
    auto* shadowPl = shadowGo->AddComponent<Engine::PointLightComponent>();
    shadowPl->Priority = Engine::LightPriority::Low_VFX;
    shadowPl->CastShadows = true;
    shadowPl->Intensity = 3.3f;
    shadowPl->Flicker = false;

    const Engine::SceneRenderContext ctx = renderer.GatherRenderContext(scene);
    ASSERT_EQ(ctx.lighting.PointLightCount, 3u);
    EXPECT_FLOAT_EQ(ctx.lighting.PointLights[0].ColorAndIntensity.a, 3.3f);
    EXPECT_FLOAT_EQ(ctx.lighting.PointLights[1].ColorAndIntensity.a, 2.2f);
    EXPECT_FLOAT_EQ(ctx.lighting.PointLights[2].ColorAndIntensity.a, 1.1f);

    // std140 UBO upload in headless test environment must be a safe no-op
    Engine::Renderer::UploadPointLights(&ctx.lighting);
    Engine::Renderer::UploadPointLights(nullptr);
    SUCCEED();
}

TEST(TorchShadowMapTest, HeadlessLifecycleAndAtlasConstants) {
    Engine::TorchShadowMap::Init(512u);
    EXPECT_FALSE(Engine::TorchShadowMap::IsValid()); // No OpenGL context in headless EngineTests
    EXPECT_EQ(Engine::TorchShadowMap::GetDepthTextureID(), 0u);
    EXPECT_EQ(Engine::TorchShadowMap::GetQuadrantResolution(), 512u);
    EXPECT_EQ(Engine::TorchShadowMap::GetAtlasWidth(), 2048u);
    EXPECT_EQ(Engine::TorchShadowMap::GetAtlasHeight(), 512u);

    Engine::TorchShadowMap::BeginCapture();
    Engine::TorchShadowMap::BeginQuadrant(Engine::TorchShadowQuadrant::North);
    Engine::TorchShadowMap::EndCapture();
    Engine::TorchShadowMap::Shutdown();
    SUCCEED();
}

TEST(TorchShadowMapTest, FourQuadrantFrustumCoverageAndRadialBillboardTangents) {
    const glm::vec3 torchPos = { 2.0f, 0.85f, -3.0f };
    const float radius = 12.0f;

    const auto matrices = Engine::TorchShadowMap::ComputeAllQuadrantMatrices(torchPos, radius);
    const std::array<Engine::Frustum, 4> frustums = {
        Engine::Frustum::FromViewProjection(matrices[0]), // North (-Z)
        Engine::Frustum::FromViewProjection(matrices[1]), // East  (+X)
        Engine::Frustum::FromViewProjection(matrices[2]), // South (+Z)
        Engine::Frustum::FromViewProjection(matrices[3])  // West  (-X)
    };

    const struct CardinalCase {
        glm::vec3 offset;
        size_t expectedQuadIdx;
        size_t oppositeQuadIdx;
    } kCardinals[4] = {
        { {  0.0f, 0.0f, -5.0f }, 0u, 2u }, // North
        { {  5.0f, 0.0f,  0.0f }, 1u, 3u }, // East
        { {  0.0f, 0.0f,  5.0f }, 2u, 0u }, // South
        { { -5.0f, 0.0f,  0.0f }, 3u, 1u }  // West
    };

    for (const auto& c : kCardinals) {
        const glm::vec3 standeeAnchor = { torchPos.x + c.offset.x, 0.0f, torchPos.z + c.offset.z };
        const glm::vec3 boundsCenter = standeeAnchor + glm::vec3(0.0f, 1.0f, 0.0f);

        // 1. 4-Quadrant frustum contains standee in expected quadrant and culls in opposite quadrant
        EXPECT_TRUE(frustums[c.expectedQuadIdx].IntersectsSphere(boundsCenter, 0.25f));
        EXPECT_FALSE(frustums[c.oppositeQuadIdx].IntersectsSphere(boundsCenter, 0.25f));

        // 2. Radial billboard tangent math produces full-width perpendicular XZ vector
        const glm::vec3 rightXZ = Engine::TorchShadowMap::ComputeRadialBillboardRightXZ(standeeAnchor, torchPos);
        const glm::vec3 toStandeeXZ = { standeeAnchor.x - torchPos.x, 0.0f, standeeAnchor.z - torchPos.z };
        EXPECT_NEAR(glm::length(rightXZ), 1.0f, 1e-5f);
        EXPECT_NEAR(rightXZ.y, 0.0f, 1e-6f);
        EXPECT_NEAR(glm::dot(rightXZ, toStandeeXZ), 0.0f, 1e-5f);
    }

    // 3. Diagonal / arbitrary positions remain orthogonal and normalized across 360 degrees
    for (float deg = 0.0f; deg < 360.0f; deg += 22.5f) {
        const float rad = glm::radians(deg);
        const glm::vec3 pos = { torchPos.x + std::cos(rad) * 4.5f, 0.0f, torchPos.z + std::sin(rad) * 4.5f };
        const glm::vec3 rightXZ = Engine::TorchShadowMap::ComputeRadialBillboardRightXZ(pos, torchPos);
        const glm::vec3 toStandeeXZ = { pos.x - torchPos.x, 0.0f, pos.z - torchPos.z };
        EXPECT_NEAR(glm::length(rightXZ), 1.0f, 1e-5f);
        EXPECT_NEAR(rightXZ.y, 0.0f, 1e-6f);
        EXPECT_NEAR(glm::dot(rightXZ, toStandeeXZ), 0.0f, 1e-5f);
    }

    // 4. Degenerate overlap with torch position falls back cleanly without NaN
    const glm::vec3 fallbackRight = Engine::TorchShadowMap::ComputeRadialBillboardRightXZ(torchPos, torchPos);
    EXPECT_NEAR(glm::length(fallbackRight), 1.0f, 1e-5f);
    EXPECT_NEAR(fallbackRight.y, 0.0f, 1e-6f);
}

} // namespace

