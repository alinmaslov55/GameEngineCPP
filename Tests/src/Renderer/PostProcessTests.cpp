#include <gtest/gtest.h>
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Renderer/PostProcessor.h"
#include <cmath>

namespace {

TEST(PostProcessMathTest, SoftKneePrefilterToneMappingAndHeadlessLifecycle) {
    Engine::PostProcessComponent pp;
    pp.BloomThreshold = 1.0f;
    pp.BloomKnee = 0.4f;

    // 1. Quadratic soft-knee prefilter: zero below (Threshold - Knee = 0.6), smooth C1 curve in [0.6, 1.4],
    //    and exact linear excess (C - Threshold) above (Threshold + Knee = 1.4)
    const glm::vec3 belowKnee = pp.EvaluateSoftKneePrefilter({ 0.55f, 0.40f, 0.20f });
    EXPECT_NEAR(belowKnee.r, 0.0f, 1e-6f);
    EXPECT_NEAR(belowKnee.g, 0.0f, 1e-6f);
    EXPECT_NEAR(belowKnee.b, 0.0f, 1e-6f);

    const glm::vec3 atThreshold = pp.EvaluateSoftKneePrefilter({ 1.0f, 1.0f, 1.0f });
    // At brightness == Threshold, quadratic soft knee evaluates to 0.25 * Knee = 0.10
    EXPECT_NEAR(atThreshold.r, 0.10f, 1e-5f);

    const glm::vec3 atUpperKnee = pp.EvaluateSoftKneePrefilter({ 1.4f, 0.7f, 0.0f });
    // At brightness == Threshold + Knee = 1.4, excess is 0.4 -> scaled proportionally across channels
    EXPECT_NEAR(atUpperKnee.r, 0.4f, 1e-5f);
    EXPECT_NEAR(atUpperKnee.g, 0.2f, 1e-5f);

    const glm::vec3 brightHDR = pp.EvaluateSoftKneePrefilter({ 3.0f, 1.5f, 0.0f });
    // Above knee (3.0 > 1.4): contribution is (3.0 - 1.0) / 3.0 = 2/3
    EXPECT_NEAR(brightHDR.r, 2.0f, 1e-5f);
    EXPECT_NEAR(brightHDR.g, 1.0f, 1e-5f);

    // 2. Tone mapping monotonicity, [0, 1] bounds & crimson hue preservation (StorybookNeutral vs ACESFilmic)
    const Engine::PostProcessComponent::ToneMapMode modes[] = {
        Engine::PostProcessComponent::ToneMapMode::ACESFilmic,
        Engine::PostProcessComponent::ToneMapMode::StorybookNeutral,
        Engine::PostProcessComponent::ToneMapMode::Reinhard
    };
    for (auto mode : modes) {
        pp.ToneMapping = mode;
        const glm::vec3 zeroOut = pp.ApplyToneMapCPU({ 0.0f, 0.0f, 0.0f });
        const glm::vec3 lowOut  = pp.ApplyToneMapCPU({ 0.35f, 0.35f, 0.35f });
        const glm::vec3 midOut  = pp.ApplyToneMapCPU({ 1.25f, 1.25f, 1.25f });
        const glm::vec3 highOut = pp.ApplyToneMapCPU({ 8.00f, 8.00f, 8.00f });

        EXPECT_NEAR(zeroOut.r, 0.0f, 1e-5f);
        EXPECT_LT(zeroOut.r, lowOut.r);
        EXPECT_LT(lowOut.r, midOut.r);
        EXPECT_LT(midOut.r, highOut.r);
        EXPECT_LE(highOut.r, 1.0f);
    }

    const glm::vec3 crimsonHDR = { 3.5f, 0.25f, 0.20f };
    pp.ToneMapping = Engine::PostProcessComponent::ToneMapMode::ACESFilmic;
    const glm::vec3 acesCrimson = pp.ApplyToneMapCPU(crimsonHDR);
    pp.ToneMapping = Engine::PostProcessComponent::ToneMapMode::StorybookNeutral;
    const glm::vec3 neutralCrimson = pp.ApplyToneMapCPU(crimsonHDR);

    // StorybookNeutral avoids ACES yellow-orange skew (G - B spread) on vivid crimson flames & runes
    EXPECT_GT(neutralCrimson.r, acesCrimson.r);
    EXPECT_LT(std::abs(neutralCrimson.g - neutralCrimson.b), std::abs(acesCrimson.g - acesCrimson.b));

    // 3. Verify all 5 mood presets populate valid distinct configurations
    pp.ApplyPreset(Engine::PostProcessComponent::Preset::MidnightSanctum);
    EXPECT_LT(pp.Temperature, 0.0f);
    EXPECT_TRUE(pp.VignetteEnabled);

    pp.ApplyPreset(Engine::PostProcessComponent::Preset::CrispNeutral);
    EXPECT_FALSE(pp.VignetteEnabled);
    EXPECT_NEAR(pp.Contrast, 1.0f, 1e-5f);
    EXPECT_NEAR(pp.Saturation, 1.0f, 1e-5f);

    pp.ApplyPreset(Engine::PostProcessComponent::Preset::BloodMoonRitual);
    EXPECT_TRUE(pp.Enabled);
    EXPECT_TRUE(pp.BloomEnabled);

    pp.ApplyPreset(Engine::PostProcessComponent::Preset::GoldenClearing);
    EXPECT_GT(pp.Temperature, 0.0f);

    pp.ApplyPreset(Engine::PostProcessComponent::Preset::CultStorybook);
    EXPECT_TRUE(pp.Enabled);

    // 4. Headless PostProcessor lifecycle safety (no OpenGL context active in unit test runner)
    Engine::PostProcessor::Init();
    EXPECT_FALSE(Engine::PostProcessor::IsValid());
    Engine::PostProcessor::Shutdown();
}

TEST(PostProcessMathTest, BufferlessFullscreenTriangleVertexMath) {
    // Vertex shader bufferless fullscreen triangle generation formula:
    //   vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    //   gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    //
    // For vertex IDs 0, 1, 2:
    //   ID 0: uv = (0, 0), pos = (-1, -1) [Bottom-Left]
    //   ID 1: uv = (2, 0), pos = ( 3, -1) [Bottom-Right, extended]
    //   ID 2: uv = (0, 2), pos = (-1,  3) [Top-Left, extended]
    struct Vertex {
        glm::vec2 uv;
        glm::vec4 pos;
    };

    auto computeVertex = [](int id) -> Vertex {
        glm::vec2 uv = glm::vec2(static_cast<float>((id << 1) & 2),
                                 static_cast<float>(id & 2));
        glm::vec4 pos = glm::vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        return { uv, pos };
    };

    const Vertex v0 = computeVertex(0);
    const Vertex v1 = computeVertex(1);
    const Vertex v2 = computeVertex(2);

    // Verify exact positions and UV coordinates
    EXPECT_NEAR(v0.uv.x, 0.0f, 1e-6f);
    EXPECT_NEAR(v0.uv.y, 0.0f, 1e-6f);
    EXPECT_NEAR(v0.pos.x, -1.0f, 1e-6f);
    EXPECT_NEAR(v0.pos.y, -1.0f, 1e-6f);

    EXPECT_NEAR(v1.uv.x, 2.0f, 1e-6f);
    EXPECT_NEAR(v1.uv.y, 0.0f, 1e-6f);
    EXPECT_NEAR(v1.pos.x, 3.0f, 1e-6f);
    EXPECT_NEAR(v1.pos.y, -1.0f, 1e-6f);

    EXPECT_NEAR(v2.uv.x, 0.0f, 1e-6f);
    EXPECT_NEAR(v2.uv.y, 2.0f, 1e-6f);
    EXPECT_NEAR(v2.pos.x, -1.0f, 1e-6f);
    EXPECT_NEAR(v2.pos.y, 3.0f, 1e-6f);

    // Winding order check: cross product of (v1 - v0) and (v2 - v0) must be > 0 (CCW)
    const glm::vec2 edge01 = glm::vec2(v1.pos.x - v0.pos.x, v1.pos.y - v0.pos.y);
    const glm::vec2 edge02 = glm::vec2(v2.pos.x - v0.pos.x, v2.pos.y - v0.pos.y);
    const float crossZ = edge01.x * edge02.y - edge01.y * edge02.x;
    EXPECT_GT(crossZ, 0.0f); // Positive implies Counter-Clockwise (front-facing in standard OpenGL)

    // Barycentric interpolation helper to verify any NDC point inside [-1, 1]^2 lies within the triangle
    auto computeBarycentric = [&](const glm::vec2& p) -> glm::vec3 {
        const glm::vec2 a = glm::vec2(v0.pos.x, v0.pos.y);
        const glm::vec2 b = glm::vec2(v1.pos.x, v1.pos.y);
        const glm::vec2 c = glm::vec2(v2.pos.x, v2.pos.y);

        const glm::vec2 v0p = b - a;
        const glm::vec2 v1p = c - a;
        const glm::vec2 v2p = p - a;

        const float d00 = glm::dot(v0p, v0p);
        const float d01 = glm::dot(v0p, v1p);
        const float d11 = glm::dot(v1p, v1p);
        const float d20 = glm::dot(v2p, v0p);
        const float d21 = glm::dot(v2p, v1p);

        const float denom = d00 * d11 - d01 * d01;
        const float v = (d11 * d20 - d01 * d21) / denom;
        const float w = (d00 * d21 - d01 * d20) / denom;
        const float u = 1.0f - v - w;
        return { u, v, w };
    };

    // Test coverage of all 4 corners and center of NDC screen quad [-1, 1] x [-1, 1]
    const glm::vec2 ndcPoints[] = {
        { -1.0f, -1.0f }, // Bottom-Left
        {  1.0f, -1.0f }, // Bottom-Right
        { -1.0f,  1.0f }, // Top-Left
        {  1.0f,  1.0f }, // Top-Right (hypotenuse edge: u + v + w = 1 with all >= 0)
        {  0.0f,  0.0f }  // Center
    };

    for (const auto& ndc : ndcPoints) {
        const glm::vec3 bary = computeBarycentric(ndc);
        EXPECT_GE(bary.x, -1e-6f);
        EXPECT_GE(bary.y, -1e-6f);
        EXPECT_GE(bary.z, -1e-6f);
        EXPECT_NEAR(bary.x + bary.y + bary.z, 1.0f, 1e-5f);

        // Interpolated UV should exactly match standard (NDC + 1) * 0.5 mapping
        const glm::vec2 interpolatedUV = bary.x * v0.uv + bary.y * v1.uv + bary.z * v2.uv;
        const glm::vec2 expectedUV = (ndc + glm::vec2(1.0f)) * 0.5f;
        EXPECT_NEAR(interpolatedUV.x, expectedUV.x, 1e-5f);
        EXPECT_NEAR(interpolatedUV.y, expectedUV.y, 1e-5f);
    }
}

} // namespace

