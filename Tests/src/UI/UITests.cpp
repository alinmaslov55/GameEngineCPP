#include <gtest/gtest.h>
#include "Engine/UI/Font.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include <algorithm>
#include <vector>

namespace {

TEST(FontTest, AddAndRetrieveCharactersWithFallback) {
    Engine::Font font;
    EXPECT_TRUE(font.GetCharacters().empty());

    Engine::Character charA;
    charA.size = { 16.0f, 24.0f };
    charA.bearing = { 0.0f, 20.0f };
    charA.advance = 18.0f;

    Engine::Character questionMark;
    questionMark.size = { 14.0f, 22.0f };
    questionMark.bearing = { 1.0f, 19.0f };
    questionMark.advance = 16.0f;

    font.AddCharacter('A', charA);
    font.AddCharacter('?', questionMark);

    EXPECT_EQ(font.GetCharacters().size(), 2u);

    // Existing character lookup
    const auto& retrievedA = font.GetCharacter('A');
    EXPECT_FLOAT_EQ(retrievedA.size.x, 16.0f);
    EXPECT_FLOAT_EQ(retrievedA.advance, 18.0f);

    // Missing character lookup falls back to '?'
    const auto& fallback = font.GetCharacter('Z');
    EXPECT_FLOAT_EQ(fallback.size.x, 14.0f);
    EXPECT_FLOAT_EQ(fallback.advance, 16.0f);
}

TEST(FontTest, MissingCharacterWithoutFallbackReturnsEmpty) {
    Engine::Font fontWithoutFallback;
    const auto& emptyChar = fontWithoutFallback.GetCharacter('X');
    EXPECT_FLOAT_EQ(emptyChar.size.x, 0.0f);
    EXPECT_FLOAT_EQ(emptyChar.advance, 0.0f);
}

TEST(FontTest, AsciiLookupAndSilentFallback) {
    Engine::Font font;
    Engine::Character charM;
    charM.size = { 12.0f, 18.0f };
    charM.advance = 14.0f;
    font.AddCharacter('M', charM);

    // O(1) table lookup
    EXPECT_FLOAT_EQ(font.GetCharacter('M').advance, 14.0f);

    // Space / newline / missing glyph lookup does not throw or crash and returns empty
    const auto& spaceChar = font.GetCharacter(' ');
    EXPECT_FLOAT_EQ(spaceChar.size.x, 0.0f);
    EXPECT_FLOAT_EQ(spaceChar.advance, 0.0f);

    // Now add '?'
    Engine::Character questionMark;
    questionMark.size = { 10.0f, 16.0f };
    questionMark.advance = 12.0f;
    font.AddCharacter('?', questionMark);

    // Missing glyph falls back to '?'
    EXPECT_FLOAT_EQ(font.GetCharacter(' ').advance, 12.0f);
    EXPECT_FLOAT_EQ(font.GetCharacter('\n').advance, 12.0f);
}

TEST(UILabelTest, ComponentCreationAndProperties) {
    Engine::GameObject entity("TextEntity");
    auto font = std::make_shared<Engine::Font>();

    auto* label = entity.AddComponent<Engine::UILabel>("Hello World", font, glm::vec2(100.0f, 200.0f));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello World");
    EXPECT_EQ(label->font, font);
    EXPECT_FLOAT_EQ(label->position.x, 100.0f);
    EXPECT_FLOAT_EQ(label->position.y, 200.0f);
    EXPECT_FLOAT_EQ(label->scale, 1.0f);
    EXPECT_FLOAT_EQ(label->color.a, 1.0f);
}

TEST(UIPanelTest, ComponentCreationAndProperties) {
    Engine::GameObject entity("PanelEntity");

    auto* panel = entity.AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 75.0f),
        glm::vec2(300.0f, 150.0f),
        glm::vec4(0.2f, 0.4f, 0.8f, 0.9f)
    );

    ASSERT_NE(panel, nullptr);
    EXPECT_FLOAT_EQ(panel->position.x, 50.0f);
    EXPECT_FLOAT_EQ(panel->position.y, 75.0f);
    EXPECT_FLOAT_EQ(panel->size.x, 300.0f);
    EXPECT_FLOAT_EQ(panel->size.y, 150.0f);
    EXPECT_FLOAT_EQ(panel->color.r, 0.2f);
    EXPECT_FLOAT_EQ(panel->color.a, 0.9f);
    EXPECT_EQ(panel->texture, nullptr);
}

TEST(UICanvasTest, ComputeScaleFactorLandscape) {
    // 4K (3840x2160): scale = 2.00x
    EXPECT_FLOAT_EQ(Engine::UICanvas::ComputeScaleFactor(3840, 2160), 2.0f);

    // 1440p (2560x1440): scale = 1.3333x
    EXPECT_NEAR(Engine::UICanvas::ComputeScaleFactor(2560, 1440), 1.3333333f, 1e-4f);

    // 1080p (1920x1080): scale = 1.00x
    EXPECT_FLOAT_EQ(Engine::UICanvas::ComputeScaleFactor(1920, 1080), 1.0f);

    // 720p (1280x720): scale = 0.6667x
    EXPECT_NEAR(Engine::UICanvas::ComputeScaleFactor(1280, 720), 0.6666667f, 1e-4f);

    // Degenerate zero height safety check
    EXPECT_FLOAT_EQ(Engine::UICanvas::ComputeScaleFactor(1920, 0), 1.0f);
}

TEST(UICanvasTest, ComputeScaleFactorPortrait) {
    // Standard Portrait 1080p (1080x1920): scale = 1.00x
    EXPECT_FLOAT_EQ(Engine::UICanvas::ComputeScaleFactor(1080, 1920), 1.0f);

    // Portrait 1440p (1440x2560): scale = 1.3333x
    EXPECT_NEAR(Engine::UICanvas::ComputeScaleFactor(1440, 2560), 1.3333333f, 1e-4f);

    // Portrait 4K (2160x3840): scale = 2.00x
    EXPECT_FLOAT_EQ(Engine::UICanvas::ComputeScaleFactor(2160, 3840), 2.0f);

    // Portrait 720p (720x1280): scale = 0.6667x
    EXPECT_NEAR(Engine::UICanvas::ComputeScaleFactor(720, 1280), 0.6666667f, 1e-4f);
}

TEST(UICanvasTest, HealthFrameResolutionAcrossStandards) {
    // Exact verification requirement:
    // Assert ResolveScreenRect for ui_hud_health_frame (240x32 @ 1080p) produces 480x64 at 4K,
    // 320x42.6 at 1440p, and 160x21.3 at 720p, inset by 5% from TopLeft.
    Engine::UIRectTransform healthFrame;
    healthFrame.Anchor = Engine::UIAnchor::TopLeft;
    healthFrame.Pivot = { 0.0f, 1.0f }; // Top-left pivot
    healthFrame.AnchoredOffset = { 0.0f, 0.0f };
    healthFrame.Size = { 240.0f, 32.0f }; // 1080p reference dimensions

    glm::vec2 pos, size;

    // 1. 4K (3840x2160): scale 2.0x -> 480x64
    Engine::UICanvas::ResolveScreenRect(healthFrame, 3840, 2160, pos, size);
    EXPECT_FLOAT_EQ(size.x, 480.0f);
    EXPECT_FLOAT_EQ(size.y, 64.0f);
    // 5% safe area inset: Left = 0.05 * 3840 = 192, Top = 0.95 * 2160 = 2052 (pos.y = 2052 - 64 = 1988)
    EXPECT_FLOAT_EQ(pos.x, 192.0f);
    EXPECT_FLOAT_EQ(pos.y, 1988.0f);
    EXPECT_FLOAT_EQ(pos.y + size.y, 2052.0f);

    // 2. 1440p (2560x1440): scale 1.3333x -> 320x42.667
    Engine::UICanvas::ResolveScreenRect(healthFrame, 2560, 1440, pos, size);
    EXPECT_NEAR(size.x, 320.0f, 0.05f);
    EXPECT_NEAR(size.y, 42.6667f, 0.1f);
    // Left = 0.05 * 2560 = 128, Top = 0.95 * 1440 = 1368
    EXPECT_FLOAT_EQ(pos.x, 128.0f);
    EXPECT_NEAR(pos.y + size.y, 1368.0f, 0.01f);

    // 3. 1080p (1920x1080): scale 1.0x -> 240x32
    Engine::UICanvas::ResolveScreenRect(healthFrame, 1920, 1080, pos, size);
    EXPECT_FLOAT_EQ(size.x, 240.0f);
    EXPECT_FLOAT_EQ(size.y, 32.0f);
    // Left = 0.05 * 1920 = 96, Top = 0.95 * 1080 = 1026 (pos.y = 1026 - 32 = 994)
    EXPECT_FLOAT_EQ(pos.x, 96.0f);
    EXPECT_FLOAT_EQ(pos.y, 994.0f);
    EXPECT_FLOAT_EQ(pos.y + size.y, 1026.0f);

    // 4. 720p (1280x720): scale 0.6667x -> 160x21.333
    Engine::UICanvas::ResolveScreenRect(healthFrame, 1280, 720, pos, size);
    EXPECT_NEAR(size.x, 160.0f, 0.05f);
    EXPECT_NEAR(size.y, 21.3333f, 0.1f);
    // Left = 0.05 * 1280 = 64, Top = 0.95 * 720 = 684
    EXPECT_FLOAT_EQ(pos.x, 64.0f);
    EXPECT_NEAR(pos.y + size.y, 684.0f, 0.01f);
}

TEST(UICanvasTest, AllNineAnchorsEvaluateCorrectly) {
    // Screen 1920x1080 with 5% margin (safe bounds: x in [96..1824], y in [54..1026])
    // Size 100x100, Pivot {0.5f, 0.5f} (center pivot)
    Engine::UIRectTransform rt;
    rt.Size = { 100.0f, 100.0f };
    rt.Pivot = { 0.5f, 0.5f };
    rt.AnchoredOffset = { 0.0f, 0.0f };

    glm::vec2 pos, size;

    auto testCenter = [&](Engine::UIAnchor anchor, float expectedCenterX, float expectedCenterY) {
        rt.Anchor = anchor;
        Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);
        EXPECT_FLOAT_EQ(pos.x + size.x * 0.5f, expectedCenterX);
        EXPECT_FLOAT_EQ(pos.y + size.y * 0.5f, expectedCenterY);
    };

    testCenter(Engine::UIAnchor::BottomLeft,   96.0f,   54.0f);
    testCenter(Engine::UIAnchor::BottomCenter, 960.0f,  54.0f);
    testCenter(Engine::UIAnchor::BottomRight,  1824.0f, 54.0f);
    testCenter(Engine::UIAnchor::MiddleLeft,     96.0f,   540.0f);
    testCenter(Engine::UIAnchor::Center,         960.0f,  540.0f);
    testCenter(Engine::UIAnchor::MiddleRight,    1824.0f, 540.0f);
    testCenter(Engine::UIAnchor::TopLeft,        96.0f,   1026.0f);
    testCenter(Engine::UIAnchor::TopCenter,      960.0f,  1026.0f);
    testCenter(Engine::UIAnchor::TopRight,       1824.0f, 1026.0f);
}

TEST(UICanvasTest, PivotOffsetsShiftBoundsAccurately) {
    Engine::UIRectTransform rt;
    rt.Anchor = Engine::UIAnchor::Center; // Screen center (960, 540)
    rt.Size = { 200.0f, 100.0f };
    rt.AnchoredOffset = { 0.0f, 0.0f };

    glm::vec2 pos, size;

    // 1. Pivot {0, 0} (bottom-left): bottom-left at (960, 540)
    rt.Pivot = { 0.0f, 0.0f };
    Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);
    EXPECT_FLOAT_EQ(pos.x, 960.0f);
    EXPECT_FLOAT_EQ(pos.y, 540.0f);

    // 2. Pivot {1, 1} (top-right): top-right at (960, 540) -> bottom-left at (760, 440)
    rt.Pivot = { 1.0f, 1.0f };
    Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);
    EXPECT_FLOAT_EQ(pos.x, 760.0f);
    EXPECT_FLOAT_EQ(pos.y, 440.0f);

    // 3. Pivot {0.5, 0.5} (center): center at (960, 540) -> bottom-left at (860, 490)
    rt.Pivot = { 0.5f, 0.5f };
    Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);
    EXPECT_FLOAT_EQ(pos.x, 860.0f);
    EXPECT_FLOAT_EQ(pos.y, 490.0f);
}

TEST(UICanvasTest, PortraitOverrideReflowsAnchor) {
    Engine::UIRectTransform rt;
    rt.Anchor = Engine::UIAnchor::BottomLeft;
    rt.PortraitAnchorOverride = Engine::UIAnchor::TopCenter;
    rt.UsePortraitOverride = true;
    rt.Size = { 100.0f, 50.0f };
    rt.Pivot = { 0.5f, 0.5f };

    glm::vec2 pos, size;

    // 1. In landscape (1920x1080, aspect >= 1.0): uses standard Anchor (BottomLeft)
    Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);
    EXPECT_FLOAT_EQ(pos.x + size.x * 0.5f, 96.0f);  // minX
    EXPECT_FLOAT_EQ(pos.y + size.y * 0.5f, 54.0f);  // minY

    // 2. In portrait (1080x1920, aspect < 1.0): reflows to PortraitAnchorOverride (TopCenter)
    Engine::UICanvas::ResolveScreenRect(rt, 1080, 1920, pos, size);
    // Portrait safe area: W=1080, H=1920. Margin = 5%.
    // TopCenter: X = 0.5 * 1080 = 540, Y = 0.95 * 1920 = 1824
    EXPECT_FLOAT_EQ(pos.x + size.x * 0.5f, 540.0f);
    EXPECT_FLOAT_EQ(pos.y + size.y * 0.5f, 1824.0f);
}

TEST(UICanvasTest, IgnoreSafeAreaAllowsFullScreenSpans) {
    Engine::UIRectTransform rt;
    rt.Anchor = Engine::UIAnchor::BottomLeft;
    rt.Pivot = { 0.0f, 0.0f };
    rt.Size = { 1920.0f, 1080.0f };
    rt.AnchoredOffset = { 0.0f, 0.0f };
    rt.IgnoreSafeArea = true; // Backdrop panel spanning full screen

    glm::vec2 pos, size;
    Engine::UICanvas::ResolveScreenRect(rt, 1920, 1080, pos, size);

    // When IgnoreSafeArea is true, anchor is at (0, 0) instead of (96, 54)
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(size.x, 1920.0f);
    EXPECT_FLOAT_EQ(size.y, 1080.0f);
}

TEST(UICanvasTest, ZOrderSortingMaintainsLayerOrder) {
    Engine::GameObject objA("A");
    Engine::GameObject objB("B");
    Engine::GameObject objC("C");

    auto* panelA = objA.AddComponent<Engine::UIPanel>(Engine::UIRectTransform{}, glm::vec4(1.0f));
    panelA->rectTransform.ZOrder = 10; // Foreground

    auto* panelB = objB.AddComponent<Engine::UIPanel>(Engine::UIRectTransform{}, glm::vec4(1.0f));
    panelB->rectTransform.ZOrder = -5; // Deep background

    auto* panelC = objC.AddComponent<Engine::UIPanel>(Engine::UIRectTransform{}, glm::vec4(1.0f));
    panelC->rectTransform.ZOrder = 2;  // Midground

    std::vector<Engine::UIPanel*> panels = { panelA, panelB, panelC };
    std::stable_sort(panels.begin(), panels.end(), [](const Engine::UIPanel* a, const Engine::UIPanel* b) {
        return a->rectTransform.ZOrder < b->rectTransform.ZOrder;
    });

    EXPECT_EQ(panels[0], panelB); // -5
    EXPECT_EQ(panels[1], panelC); // 2
    EXPECT_EQ(panels[2], panelA); // 10
}

TEST(UICanvasTest, SceneSerializerUIRectTransformRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    const uint64_t hudUUID = 888222ULL;
    auto* srcObj = sourceScene->CreateGameObjectWithUUID(hudUUID, "HUD_Root");

    Engine::UIRectTransform panelRect;
    panelRect.Anchor = Engine::UIAnchor::BottomLeft;
    panelRect.PortraitAnchorOverride = Engine::UIAnchor::TopCenter;
    panelRect.UsePortraitOverride = true;
    panelRect.Pivot = { 0.2f, 0.8f };
    panelRect.AnchoredOffset = { 15.0f, -25.0f };
    panelRect.Size = { 280.0f, 60.0f };
    panelRect.ZOrder = 4;
    panelRect.IgnoreSafeArea = false;

    srcObj->AddComponent<Engine::UIPanel>(panelRect, glm::vec4(0.2f, 0.4f, 0.6f, 0.95f));

    Engine::UIRectTransform labelRect;
    labelRect.Anchor = Engine::UIAnchor::TopRight;
    labelRect.Pivot = { 1.0f, 1.0f };
    labelRect.AnchoredOffset = { -10.0f, -10.0f };
    labelRect.Size = { 150.0f, 30.0f };
    labelRect.ZOrder = 5;

    srcObj->AddComponent<Engine::UILabel>("Score: 999", nullptr, labelRect, glm::vec4(1.0f, 0.9f, 0.1f, 1.0f));

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(hudUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstPanel = dstObj->GetComponent<Engine::UIPanel>();
    ASSERT_NE(dstPanel, nullptr);
    EXPECT_TRUE(dstPanel->useRectTransform);
    EXPECT_EQ(dstPanel->rectTransform.Anchor, Engine::UIAnchor::BottomLeft);
    EXPECT_EQ(dstPanel->rectTransform.PortraitAnchorOverride, Engine::UIAnchor::TopCenter);
    EXPECT_TRUE(dstPanel->rectTransform.UsePortraitOverride);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.Pivot.x, 0.2f);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.Pivot.y, 0.8f);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.AnchoredOffset.x, 15.0f);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.AnchoredOffset.y, -25.0f);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.Size.x, 280.0f);
    EXPECT_FLOAT_EQ(dstPanel->rectTransform.Size.y, 60.0f);
    EXPECT_EQ(dstPanel->rectTransform.ZOrder, 4);
    EXPECT_FALSE(dstPanel->rectTransform.IgnoreSafeArea);

    auto* dstLabel = dstObj->GetComponent<Engine::UILabel>();
    ASSERT_NE(dstLabel, nullptr);
    EXPECT_TRUE(dstLabel->useRectTransform);
    EXPECT_EQ(dstLabel->text, "Score: 999");
    EXPECT_EQ(dstLabel->rectTransform.Anchor, Engine::UIAnchor::TopRight);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.Pivot.x, 1.0f);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.Pivot.y, 1.0f);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.AnchoredOffset.x, -10.0f);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.AnchoredOffset.y, -10.0f);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.Size.x, 150.0f);
    EXPECT_FLOAT_EQ(dstLabel->rectTransform.Size.y, 30.0f);
    EXPECT_EQ(dstLabel->rectTransform.ZOrder, 5);
}

} // namespace

