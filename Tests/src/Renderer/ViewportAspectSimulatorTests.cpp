#include <gtest/gtest.h>

#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/ViewportAspectSimulator.h"
#include "Engine/UI/UICanvas.h"

using namespace Engine;

// Test 1: Verify exact mathematical target ratios and human-readable mode names
TEST(ViewportAspectSimulatorTests, TargetAspectRatiosAndNames) {
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode::Landscape_16_9), 16.0f / 9.0f);
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode::Landscape_16_10), 16.0f / 10.0f);
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode::Portrait_9_16), 9.0f / 16.0f);
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode::Portrait_10_16), 10.0f / 16.0f);

    // Free aspect falls back to passed custom ratio
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode::Free, 2.39f), 2.39f);

    EXPECT_EQ(ViewportAspectSimulator::GetModeName(ViewportAspectMode::Free), "Free");
    EXPECT_EQ(ViewportAspectSimulator::GetModeName(ViewportAspectMode::Landscape_16_9), "16:9 Landscape");
    EXPECT_EQ(ViewportAspectSimulator::GetModeName(ViewportAspectMode::Landscape_16_10), "16:10 Landscape");
    EXPECT_EQ(ViewportAspectSimulator::GetModeName(ViewportAspectMode::Portrait_9_16), "9:16 Portrait");
    EXPECT_EQ(ViewportAspectSimulator::GetModeName(ViewportAspectMode::Portrait_10_16), "10:16 Portrait");
}

// Test 2: Free mode uses 100% of available space with zero offsets
TEST(ViewportAspectSimulatorTests, FreeModePassesThroughAvailableDimensions) {
    const glm::vec2 availLandscape = { 1920.0f, 1080.0f };
    const auto m1 = ViewportAspectSimulator::CalculateMetrics(availLandscape, ViewportAspectMode::Free);
    EXPECT_FLOAT_EQ(m1.RenderSize.x, 1920.0f);
    EXPECT_FLOAT_EQ(m1.RenderSize.y, 1080.0f);
    EXPECT_FLOAT_EQ(m1.Offset.x, 0.0f);
    EXPECT_FLOAT_EQ(m1.Offset.y, 0.0f);
    EXPECT_FALSE(m1.IsPillarboxed);
    EXPECT_FALSE(m1.IsLetterboxed);
    EXPECT_FLOAT_EQ(m1.AspectRatio, 1920.0f / 1080.0f);

    const glm::vec2 availPortrait = { 720.0f, 1280.0f };
    const auto m2 = ViewportAspectSimulator::CalculateMetrics(availPortrait, ViewportAspectMode::Free);
    EXPECT_FLOAT_EQ(m2.RenderSize.x, 720.0f);
    EXPECT_FLOAT_EQ(m2.RenderSize.y, 1280.0f);
    EXPECT_FLOAT_EQ(m2.Offset.x, 0.0f);
    EXPECT_FLOAT_EQ(m2.Offset.y, 0.0f);
    EXPECT_FALSE(m2.IsPillarboxed);
    EXPECT_FALSE(m2.IsLetterboxed);
}

// Test 3: Landscape letterboxing and pillarboxing
TEST(ViewportAspectSimulatorTests, LandscapePillarboxAndLetterbox) {
    // Ultra-wide display (2560x1080) with 16:9 -> pillarboxed horizontally
    const glm::vec2 ultrawide = { 2560.0f, 1080.0f };
    const auto m1 = ViewportAspectSimulator::CalculateMetrics(ultrawide, ViewportAspectMode::Landscape_16_9);
    EXPECT_TRUE(m1.IsPillarboxed);
    EXPECT_FALSE(m1.IsLetterboxed);
    EXPECT_FLOAT_EQ(m1.RenderSize.x, 1920.0f);
    EXPECT_FLOAT_EQ(m1.RenderSize.y, 1080.0f);
    EXPECT_FLOAT_EQ(m1.Offset.x, 320.0f); // (2560 - 1920) / 2
    EXPECT_FLOAT_EQ(m1.Offset.y, 0.0f);

    // Square / tall window (1000x1000) with 16:9 -> letterboxed vertically
    const glm::vec2 square = { 1000.0f, 1000.0f };
    const auto m2 = ViewportAspectSimulator::CalculateMetrics(square, ViewportAspectMode::Landscape_16_9);
    EXPECT_FALSE(m2.IsPillarboxed);
    EXPECT_TRUE(m2.IsLetterboxed);
    EXPECT_FLOAT_EQ(m2.RenderSize.x, 1000.0f);
    EXPECT_FLOAT_EQ(m2.RenderSize.y, 563.0f); // 1000 / (16/9) = 562.5 -> rounded to 563
    EXPECT_FLOAT_EQ(m2.Offset.x, 0.0f);
    EXPECT_FLOAT_EQ(m2.Offset.y, 218.5f); // (1000 - 563) / 2

    // Exact fit 1280x720 with 16:9 -> no black bars
    const glm::vec2 exact = { 1280.0f, 720.0f };
    const auto m3 = ViewportAspectSimulator::CalculateMetrics(exact, ViewportAspectMode::Landscape_16_9);
    EXPECT_FALSE(m3.IsPillarboxed);
    EXPECT_FALSE(m3.IsLetterboxed);
    EXPECT_FLOAT_EQ(m3.RenderSize.x, 1280.0f);
    EXPECT_FLOAT_EQ(m3.RenderSize.y, 720.0f);
    EXPECT_FLOAT_EQ(m3.Offset.x, 0.0f);
    EXPECT_FLOAT_EQ(m3.Offset.y, 0.0f);
}

// Test 4: Portrait letterboxing and pillarboxing
TEST(ViewportAspectSimulatorTests, PortraitPillarboxAndLetterbox) {
    // Standard landscape monitor (1920x1080) simulating 9:16 mobile portrait -> pillarboxed
    const glm::vec2 landscape = { 1920.0f, 1080.0f };
    const auto m1 = ViewportAspectSimulator::CalculateMetrics(landscape, ViewportAspectMode::Portrait_9_16);
    EXPECT_TRUE(m1.IsPillarboxed);
    EXPECT_FALSE(m1.IsLetterboxed);
    EXPECT_FLOAT_EQ(m1.RenderSize.x, 608.0f); // 1080 * (9/16) = 607.5 -> rounded to 608
    EXPECT_FLOAT_EQ(m1.RenderSize.y, 1080.0f);
    EXPECT_FLOAT_EQ(m1.Offset.x, 656.0f); // (1920 - 608) / 2
    EXPECT_FLOAT_EQ(m1.Offset.y, 0.0f);

    // Excessively tall / thin window (300x1200) with 9:16 -> letterboxed vertically
    const glm::vec2 thinTall = { 300.0f, 1200.0f };
    const auto m2 = ViewportAspectSimulator::CalculateMetrics(thinTall, ViewportAspectMode::Portrait_9_16);
    EXPECT_FALSE(m2.IsPillarboxed);
    EXPECT_TRUE(m2.IsLetterboxed);
    EXPECT_FLOAT_EQ(m2.RenderSize.x, 300.0f);
    EXPECT_FLOAT_EQ(m2.RenderSize.y, 533.0f); // 300 / (9/16) = 533.3 -> rounded to 533
    EXPECT_FLOAT_EQ(m2.Offset.x, 0.0f);
    EXPECT_FLOAT_EQ(m2.Offset.y, 333.5f); // (1200 - 533) / 2
}

// Test 5: Safe area calculation matches 5% margin ratio specification
TEST(ViewportAspectSimulatorTests, SafeAreaCalculationMatches5Percent) {
    const glm::vec2 origin = { 100.0f, 50.0f };
    const glm::vec2 renderSize = { 1920.0f, 1080.0f };

    const auto safeRect = ViewportAspectSimulator::CalculateSafeArea(origin, renderSize, 0.05f);

    EXPECT_FLOAT_EQ(safeRect.MarginX, 96.0f);  // 1920 * 0.05
    EXPECT_FLOAT_EQ(safeRect.MarginY, 54.0f);  // 1080 * 0.05
    EXPECT_FLOAT_EQ(safeRect.Min.x, 196.0f);   // 100 + 96
    EXPECT_FLOAT_EQ(safeRect.Min.y, 104.0f);   // 50 + 54
    EXPECT_FLOAT_EQ(safeRect.Max.x, 1924.0f);  // 100 + 1920 - 96
    EXPECT_FLOAT_EQ(safeRect.Max.y, 1076.0f);  // 50 + 1080 - 54
    EXPECT_FLOAT_EQ(safeRect.Size.x, 1728.0f); // 1920 * 0.90
    EXPECT_FLOAT_EQ(safeRect.Size.y, 972.0f);  // 1080 * 0.90

    // Matches UICanvas constant
    EXPECT_FLOAT_EQ(ViewportAspectSimulator::kDefaultSafeAreaMargin, UICanvas::SafeAreaMarginRatio);
}

// Test 6: Coupling with CAM-01 (PerspectiveCamera Hor+/Vert+ switch) and UIX-01 (UICanvas)
TEST(ViewportAspectSimulatorTests, IntegrationWithCameraAndCanvas) {
    PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetFOVAdaptationMode(FOVAdaptationMode::LandscapeVert_PortraitHorz);
    camera.SetPortraitHorizontalFOVDegrees(68.0f);

    // 1. In simulated Landscape (16:9), effective vertical FOV is preserved at 60 deg
    const auto landscapeMetrics = ViewportAspectSimulator::CalculateMetrics({ 1920.0f, 1080.0f }, ViewportAspectMode::Landscape_16_9);
    camera.SetAspectRatio(landscapeMetrics.AspectRatio);
    EXPECT_GT(camera.GetAspectRatio(), 1.0f);
    EXPECT_NEAR(camera.GetEffectiveVerticalFOVDegrees(), 60.0f, 0.01f);

    // 2. In simulated Portrait (9:16), aspect ratio < 1.0f triggers CAM-01 lock to 68 deg horizontal FOV!
    const auto portraitMetrics = ViewportAspectSimulator::CalculateMetrics({ 1920.0f, 1080.0f }, ViewportAspectMode::Portrait_9_16);
    camera.SetAspectRatio(portraitMetrics.AspectRatio);
    EXPECT_LT(camera.GetAspectRatio(), 1.0f);
    EXPECT_NEAR(camera.GetEffectiveHorizontalFOVDegrees(), 68.0f, 0.01f);
    EXPECT_GT(camera.GetEffectiveVerticalFOVDegrees(), 60.0f); // Vertical FOV expands to maintain horizontal framing

    // 3. In simulated Portrait, UIX-01 anchors reflow against 5% safe area bounds
    UIRectTransform panel;
    panel.Anchor = UIAnchor::BottomLeft;
    panel.PortraitAnchorOverride = UIAnchor::BottomCenter;
    panel.UsePortraitOverride = true;
    panel.Pivot = { 0.0f, 0.0f };
    panel.AnchoredOffset = { 0.0f, 0.0f };
    panel.Size = { 100.0f, 40.0f };
    panel.IgnoreSafeArea = false;

    glm::vec2 resolvedPos{ 0.0f };
    glm::vec2 resolvedSize{ 0.0f };
    UICanvas::ResolveScreenRect(panel,
                                static_cast<uint32_t>(portraitMetrics.RenderSize.x),
                                static_cast<uint32_t>(portraitMetrics.RenderSize.y),
                                resolvedPos,
                                resolvedSize);

    // In portrait mode, active anchor switches to BottomCenter inside the safe area!
    const float expectedMarginY = portraitMetrics.RenderSize.y * 0.05f;
    EXPECT_NEAR(resolvedPos.y, expectedMarginY, 0.5f);
    EXPECT_GT(resolvedSize.x, 0.0f);
    EXPECT_GT(resolvedSize.y, 0.0f);
}
