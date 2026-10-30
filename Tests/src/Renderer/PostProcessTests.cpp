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

} // namespace

