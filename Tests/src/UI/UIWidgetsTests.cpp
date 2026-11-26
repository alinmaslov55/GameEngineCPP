#include <gtest/gtest.h>
#include "Engine/UI/Font.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/UI/UIButtonComponent.h"
#include "Engine/UI/UIRadialWheelComponent.h"
#include "Engine/UI/MinimapFogSystem.h"
#include "Engine/UI/UIMinimapComponent.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Time.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace {

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

TEST(UIPanelGeometryTest, NineSliceCornerProportionsAndAspectPreservation) {
    Engine::UIPanel panel;
    panel.renderMode = Engine::UIRenderMode::NineSlice;
    panel.nineSliceBordersPx = { 32.0f, 32.0f, 32.0f, 32.0f }; // Left, Right, Top, Bottom
    panel.color = glm::vec4(1.0f);

    const glm::vec2 resolvedPos = { 100.0f, 50.0f };
    const glm::vec2 resolvedSize = { 800.0f, 200.0f };
    const auto quads = panel.GenerateQuads(resolvedPos, resolvedSize, 1.0f);

    ASSERT_EQ(quads.size(), 9u);

    auto quadWidth = [](const Engine::UIQuad& q) {
        return q.Vertices[1].Position.x - q.Vertices[0].Position.x;
    };
    auto quadHeight = [](const Engine::UIQuad& q) {
        return q.Vertices[3].Position.y - q.Vertices[0].Position.y;
    };

    // Row-major order (row 0 = bottom, row 1 = middle, row 2 = top):
    // Quad 0: Bottom-Left corner
    // Quad 2: Bottom-Right corner
    // Quad 6: Top-Left corner
    // Quad 8: Top-Right corner
    // Quad 4: Center slice
    EXPECT_FLOAT_EQ(quadWidth(quads[0]), 32.0f);
    EXPECT_FLOAT_EQ(quadHeight(quads[0]), 32.0f);

    EXPECT_FLOAT_EQ(quadWidth(quads[2]), 32.0f);
    EXPECT_FLOAT_EQ(quadHeight(quads[2]), 32.0f);

    EXPECT_FLOAT_EQ(quadWidth(quads[6]), 32.0f);
    EXPECT_FLOAT_EQ(quadHeight(quads[6]), 32.0f);

    EXPECT_FLOAT_EQ(quadWidth(quads[8]), 32.0f);
    EXPECT_FLOAT_EQ(quadHeight(quads[8]), 32.0f);

    // Center quad stretches to fill remaining space (800 - 64 = 736 wide, 200 - 64 = 136 tall)
    EXPECT_FLOAT_EQ(quadWidth(quads[4]), 736.0f);
    EXPECT_FLOAT_EQ(quadHeight(quads[4]), 136.0f);

    // Edge slices stretch along one axis only
    EXPECT_FLOAT_EQ(quadWidth(quads[1]), 736.0f); // Bottom edge
    EXPECT_FLOAT_EQ(quadHeight(quads[1]), 32.0f);
    EXPECT_FLOAT_EQ(quadWidth(quads[7]), 736.0f); // Top edge
    EXPECT_FLOAT_EQ(quadHeight(quads[7]), 32.0f);
    EXPECT_FLOAT_EQ(quadWidth(quads[3]), 32.0f);  // Left edge
    EXPECT_FLOAT_EQ(quadHeight(quads[3]), 136.0f);
    EXPECT_FLOAT_EQ(quadWidth(quads[5]), 32.0f);  // Right edge
    EXPECT_FLOAT_EQ(quadHeight(quads[5]), 136.0f);

    // Verify UV mapping (assuming 512x512 fallback texture: 32 / 512 = 0.0625, (512 - 32) / 512 = 0.9375)
    // Quad 0 (Bottom-Left)
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.y, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].TexCoord.x, 0.0625f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].TexCoord.y, 0.0625f);

    // Quad 8 (Top-Right)
    EXPECT_FLOAT_EQ(quads[8].Vertices[0].TexCoord.x, 0.9375f);
    EXPECT_FLOAT_EQ(quads[8].Vertices[0].TexCoord.y, 0.9375f);
    EXPECT_FLOAT_EQ(quads[8].Vertices[2].TexCoord.x, 1.0f);
    EXPECT_FLOAT_EQ(quads[8].Vertices[2].TexCoord.y, 1.0f);
}

TEST(UIPanelGeometryTest, RadialSweepCWQuadrantCoverage) {
    Engine::UIPanel panel;
    panel.renderMode = Engine::UIRenderMode::RadialSweepCW;
    panel.color = glm::vec4(1.0f);

    const glm::vec2 pos = { 100.0f, 100.0f };
    const glm::vec2 size = { 200.0f, 200.0f };

    // At FillAmount = 0.25f, covers exactly one quadrant (Quadrant 0: Top-Right)
    panel.fillAmount = 0.25f;
    auto quads = panel.GenerateQuads(pos, size);
    ASSERT_EQ(quads.size(), 1u);

    // Quadrant 0 covers [pos + 0.5*size .. pos + size] = [200..300, 200..300]
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].Position.x, 200.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].Position.y, 200.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].Position.x, 300.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].Position.y, 200.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].Position.x, 300.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].Position.y, 300.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[3].Position.x, 200.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[3].Position.y, 300.0f);

    // UV coordinates cover [0.5..1.0, 0.5..1.0]
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.x, 0.5f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.y, 0.5f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].TexCoord.x, 1.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].TexCoord.y, 1.0f);

    // At FillAmount = 0.50f, covers 2 quadrants (Top-Right and Bottom-Right)
    panel.fillAmount = 0.50f;
    quads = panel.GenerateQuads(pos, size);
    EXPECT_EQ(quads.size(), 2u);

    // At FillAmount = 0.75f, covers 3 quadrants
    panel.fillAmount = 0.75f;
    quads = panel.GenerateQuads(pos, size);
    EXPECT_EQ(quads.size(), 3u);

    // At FillAmount = 1.00f, covers all 4 quadrants
    panel.fillAmount = 1.00f;
    quads = panel.GenerateQuads(pos, size);
    EXPECT_EQ(quads.size(), 4u);

    // At FillAmount = 0.0f, covers 0 quads
    panel.fillAmount = 0.0f;
    quads = panel.GenerateQuads(pos, size);
    EXPECT_EQ(quads.size(), 0u);
}

TEST(UIPanelGeometryTest, HorizontalAndVerticalFillClipping) {
    Engine::UIPanel panel;
    panel.color = glm::vec4(1.0f);

    const glm::vec2 pos = { 50.0f, 50.0f };
    const glm::vec2 size = { 200.0f, 100.0f };

    // HorizontalFill standard (left-to-right)
    panel.renderMode = Engine::UIRenderMode::HorizontalFill;
    panel.fillAmount = 0.65f;
    panel.invertFill = false;

    auto quads = panel.GenerateQuads(pos, size);
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].Position.x, 50.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].Position.x, 50.0f + 130.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].TexCoord.x, 0.65f);

    // HorizontalFill inverted (right-to-left)
    panel.invertFill = true;
    quads = panel.GenerateQuads(pos, size);
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].Position.x, 50.0f + 200.0f - 130.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].Position.x, 250.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.x, 0.35f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[1].TexCoord.x, 1.0f);

    // VerticalFill standard (bottom-to-top)
    panel.renderMode = Engine::UIRenderMode::VerticalFill;
    panel.fillAmount = 0.75f;
    panel.invertFill = false;
    quads = panel.GenerateQuads(pos, size);
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].Position.y, 50.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].Position.y, 50.0f + 75.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[0].TexCoord.y, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].Vertices[2].TexCoord.y, 0.75f);
}

TEST(UIButtonComponentTest, DirectionalFocusGraphNavigation) {
    auto scene = std::make_shared<Engine::Scene>();

    auto* leftObj = scene->CreateGameObject("LeftCard");
    auto* midObj = scene->CreateGameObject("MidCard");
    auto* rightObj = scene->CreateGameObject("RightCard");

    auto* leftBtn = leftObj->AddComponent<Engine::UIButtonComponent>();
    auto* midBtn = midObj->AddComponent<Engine::UIButtonComponent>();
    auto* rightBtn = rightObj->AddComponent<Engine::UIButtonComponent>();

    leftBtn->SetNeighbors(0, 0, 0, midObj->GetUUID());
    midBtn->SetNeighbors(0, 0, leftObj->GetUUID(), rightObj->GetUUID());
    rightBtn->SetNeighbors(0, 0, midObj->GetUUID(), 0);

    Engine::UIButtonComponent::SetFocusedButton(leftBtn);
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), leftBtn);
    EXPECT_TRUE(leftBtn->IsFocused());
    EXPECT_FALSE(midBtn->IsFocused());

    // Navigate Right: Left -> Mid
    EXPECT_TRUE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, *scene));
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), midBtn);
    EXPECT_FALSE(leftBtn->IsFocused());
    EXPECT_TRUE(midBtn->IsFocused());

    // Navigate Right: Mid -> Right
    EXPECT_TRUE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, *scene));
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), rightBtn);
    EXPECT_TRUE(rightBtn->IsFocused());

    // Navigate Right from Right (no right neighbor): remains on Right
    EXPECT_FALSE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, *scene));
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), rightBtn);

    // Navigate Left: Right -> Mid
    EXPECT_TRUE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Left, *scene));
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), midBtn);

    Engine::UIButtonComponent::ClearFocus();
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), nullptr);
    EXPECT_FALSE(midBtn->IsFocused());
}

TEST(UIButtonComponentTest, ClickAndAutoTintModulation) {
    auto scene = std::make_shared<Engine::Scene>();
    auto* obj = scene->CreateGameObject("TestBtn");

    auto* panel = obj->AddComponent<Engine::UIPanel>(glm::vec2(0.0f), glm::vec2(100.0f), glm::vec4(1.0f));
    auto* btn = obj->AddComponent<Engine::UIButtonComponent>();

    btn->NormalTint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    btn->HoverTint = glm::vec4(1.15f, 1.15f, 1.15f, 1.0f);
    btn->PressedTint = glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
    btn->DisabledTint = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f);
    btn->FocusedTint = glm::vec4(1.25f, 1.20f, 0.85f, 1.0f);

    btn->SetState(Engine::ButtonState::Hovered);
    EXPECT_EQ(panel->color, btn->HoverTint);

    btn->SetState(Engine::ButtonState::Pressed);
    EXPECT_EQ(panel->color, btn->PressedTint);

    btn->SetState(Engine::ButtonState::Normal);
    btn->SetFocused(true);
    EXPECT_EQ(panel->color, btn->FocusedTint);

    btn->Interactable = false;
    btn->SetState(Engine::ButtonState::Disabled);
    EXPECT_EQ(panel->color, btn->DisabledTint);

    // Click callback test
    btn->Interactable = true;
    bool clicked = false;
    btn->OnClick = [&]() { clicked = true; };

    btn->Click();
    EXPECT_TRUE(clicked);

    clicked = false;
    Engine::UIButtonComponent::SetFocusedButton(btn);
    EXPECT_TRUE(Engine::UIButtonComponent::SubmitActiveFocus());
    EXPECT_TRUE(clicked);

    Engine::UIButtonComponent::ClearFocus();
}

TEST(UIButtonComponentTest, SceneSerializerRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    auto* obj = sourceScene->CreateGameObject("ButtonEntity");
    const uint64_t btnUUID = obj->GetUUID();

    auto* panel = obj->AddComponent<Engine::UIPanel>(glm::vec2(20.0f, 30.0f), glm::vec2(200.0f, 60.0f), glm::vec4(1.0f));
    panel->renderMode = Engine::UIRenderMode::NineSlice;
    panel->nineSliceBordersPx = { 24.0f, 24.0f, 16.0f, 16.0f };
    panel->fillAmount = 0.85f;
    panel->invertFill = true;

    auto* btn = obj->AddComponent<Engine::UIButtonComponent>();
    btn->NormalTint = { 0.9f, 0.9f, 0.9f, 1.0f };
    btn->HoverTint = { 1.2f, 1.2f, 1.2f, 1.0f };
    btn->PressedTint = { 0.7f, 0.7f, 0.7f, 1.0f };
    btn->DisabledTint = { 0.4f, 0.4f, 0.4f, 0.5f };
    btn->FocusedTint = { 1.3f, 1.1f, 0.8f, 1.0f };
    btn->Interactable = true;
    btn->AutoTintPanel = true;
    btn->UpNeighborUUID = 1111ull;
    btn->DownNeighborUUID = 2222ull;
    btn->LeftNeighborUUID = 3333ull;
    btn->RightNeighborUUID = 4444ull;

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(btnUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstPanel = dstObj->GetComponent<Engine::UIPanel>();
    ASSERT_NE(dstPanel, nullptr);
    EXPECT_EQ(dstPanel->renderMode, Engine::UIRenderMode::NineSlice);
    EXPECT_FLOAT_EQ(dstPanel->nineSliceBordersPx.x, 24.0f);
    EXPECT_FLOAT_EQ(dstPanel->nineSliceBordersPx.y, 24.0f);
    EXPECT_FLOAT_EQ(dstPanel->nineSliceBordersPx.z, 16.0f);
    EXPECT_FLOAT_EQ(dstPanel->nineSliceBordersPx.w, 16.0f);
    EXPECT_FLOAT_EQ(dstPanel->fillAmount, 0.85f);
    EXPECT_TRUE(dstPanel->invertFill);

    auto* dstBtn = dstObj->GetComponent<Engine::UIButtonComponent>();
    ASSERT_NE(dstBtn, nullptr);
    EXPECT_FLOAT_EQ(dstBtn->NormalTint.r, 0.9f);
    EXPECT_FLOAT_EQ(dstBtn->HoverTint.r, 1.2f);
    EXPECT_FLOAT_EQ(dstBtn->PressedTint.r, 0.7f);
    EXPECT_FLOAT_EQ(dstBtn->DisabledTint.r, 0.4f);
    EXPECT_FLOAT_EQ(dstBtn->FocusedTint.r, 1.3f);
    EXPECT_TRUE(dstBtn->Interactable);
    EXPECT_TRUE(dstBtn->AutoTintPanel);
    EXPECT_EQ(dstBtn->UpNeighborUUID, 1111ull);
    EXPECT_EQ(dstBtn->DownNeighborUUID, 2222ull);
    EXPECT_EQ(dstBtn->LeftNeighborUUID, 3333ull);
    EXPECT_EQ(dstBtn->RightNeighborUUID, 4444ull);
}

TEST(UIButtonComponentTest, SingleFrameFocusStepNoCascade) {
    Engine::Time::Reset();
    Engine::Input::Init();
    Engine::Input::BeginFrame();
    Engine::Time::SetMockDeltaTime(1.0f / 60.0f);
    Engine::Time::Update();
    Engine::Time::Update();

    Engine::Scene scene("SingleFrameFocusStepScene");
    auto* go0 = scene.CreateGameObject("Btn0");
    auto* go1 = scene.CreateGameObject("Btn1");
    auto* go2 = scene.CreateGameObject("Btn2");

    auto* btn0 = go0->AddComponent<Engine::UIButtonComponent>();
    auto* btn1 = go1->AddComponent<Engine::UIButtonComponent>();
    auto* btn2 = go2->AddComponent<Engine::UIButtonComponent>();

    btn0->SetNeighbors(0, go1->GetUUID(), 0, 0);
    btn1->SetNeighbors(go0->GetUUID(), go2->GetUUID(), 0, 0);
    btn2->SetNeighbors(go1->GetUUID(), 0, 0, 0);

    Engine::UIButtonComponent::SetFocusedButton(btn0);
    ASSERT_EQ(Engine::UIButtonComponent::GetFocusedButton(), btn0);

    // Simulate a single DOWN key press for 1 frame
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_DOWN, true);
    scene.Update(1.0f / 60.0f);

    // Must advance exactly one step to Btn1, NOT cascade to Btn2 on the same frame
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), btn1);

    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_DOWN, false);
    Engine::Input::BeginFrame();
    Engine::UIButtonComponent::ClearFocus();
    Engine::Time::Reset();
}

TEST(UIButtonComponentTest, DeactivatedButtonClearsFocusAndBlocksClick) {
    Engine::Scene scene("DeactivatedButtonFocusScene");
    auto* go = scene.CreateGameObject("Btn");
    auto* btn = go->AddComponent<Engine::UIButtonComponent>();
    int clickCount = 0;
    btn->OnClick = [&]() { ++clickCount; };

    Engine::UIButtonComponent::SetFocusedButton(btn);
    ASSERT_EQ(Engine::UIButtonComponent::GetFocusedButton(), btn);

    // Deactivating the GameObject must clear focus and prevent SubmitActiveFocus/Click
    go->SetActive(false);
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), nullptr);
    EXPECT_FALSE(Engine::UIButtonComponent::SubmitActiveFocus());
    btn->Click();
    EXPECT_EQ(clickCount, 0);
}

TEST(UIButtonComponentTest, OverlappingButtonsRespectZOrder) {
    Engine::Time::Reset();
    Engine::Input::Init();
    Engine::Input::BeginFrame();
    Engine::Input::SetViewportRect({ 0.0f, 0.0f }, { 1920.0f, 1080.0f });
    Engine::Time::SetMockDeltaTime(1.0f / 60.0f);
    Engine::Time::Update();
    Engine::Time::Update();

    Engine::Scene scene("ZOrderButtonsScene");
    auto* goLow = scene.CreateGameObject("LowButton");
    auto* panelLow = goLow->AddComponent<Engine::UIPanel>();
    panelLow->useRectTransform = false;
    panelLow->position = { 100.0f, 100.0f };
    panelLow->size = { 200.0f, 80.0f };
    panelLow->rectTransform.ZOrder = 5;
    auto* btnLow = goLow->AddComponent<Engine::UIButtonComponent>();
    int lowClicks = 0;
    btnLow->OnClick = [&]() { ++lowClicks; };

    auto* goHigh = scene.CreateGameObject("HighButton");
    auto* panelHigh = goHigh->AddComponent<Engine::UIPanel>();
    panelHigh->useRectTransform = false;
    panelHigh->position = { 100.0f, 100.0f };
    panelHigh->size = { 200.0f, 80.0f };
    panelHigh->rectTransform.ZOrder = 10;
    auto* btnHigh = goHigh->AddComponent<Engine::UIButtonComponent>();
    int highClicks = 0;
    btnHigh->OnClick = [&]() { ++highClicks; };

    // Mouse at center of overlap (screen bottom-up (200, 140) -> top-down (200, 1080 - 140 = 940))
    Engine::Input::SetMousePositionForTesting({ 200.0f, 940.0f });
    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, true);
    scene.Update(1.0f / 60.0f);

    Engine::Input::BeginFrame();
    Engine::Time::Update();
    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, false);
    scene.Update(1.0f / 60.0f);

    EXPECT_EQ(highClicks, 1);
    EXPECT_EQ(lowClicks, 0);

    Engine::UIButtonComponent::ClearFocus();
    Engine::Time::Reset();
}

TEST(UIRadialWheelTest, TeardownRestoresTimeScaleAndRejectsNonFiniteAngles) {
    Engine::Time::Reset();
    ASSERT_FLOAT_EQ(Engine::Time::GetTimeScale(), 1.0f);

    // Non-finite angles must return -1 immediately without hanging
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(posInf), -1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(negInf), -1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(nanVal), -1);

    Engine::Scene scene("RadialWheelTeardownScene");
    auto* go = scene.CreateGameObject("WheelEntity");
    auto* wheel = go->AddComponent<Engine::UIRadialWheelComponent>();
    wheel->Open();
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.15f);

    // Destroy GameObject and flush lifecycle queues: TimeScale must restore to 1.0f
    scene.DestroyGameObject(go);
    scene.Update(0.0f);
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 1.0f);

    Engine::Time::Reset();
}

TEST(UIPanelGeometryTest, RadialSweepCWPartialSectorHasCCWWindingAndOversizedNineSliceClampsUVs) {
    Engine::UIPanel panel;
    panel.renderMode = Engine::UIRenderMode::RadialSweepCW;
    panel.fillAmount = 0.12f;
    auto quads = panel.GenerateQuads({ 100.0f, 100.0f }, { 200.0f, 200.0f });
    ASSERT_EQ(quads.size(), 1u);

    auto cross2D = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        const glm::vec2 ab = b - a;
        const glm::vec2 ac = c - a;
        return ab.x * ac.y - ab.y * ac.x;
    };

    // Triangle (0, 1, 2) must be CCW (positive 2D cross product)
    const float c012 = cross2D(quads[0].Vertices[0].Position, quads[0].Vertices[1].Position, quads[0].Vertices[2].Position);
    EXPECT_GT(c012, 0.0f);

    // Also test pastCorner (fillAmount = 0.20f) where both triangles (0,1,2) and (2,3,0) are non-degenerate CCW
    panel.fillAmount = 0.20f;
    quads = panel.GenerateQuads({ 100.0f, 100.0f }, { 200.0f, 200.0f });
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_GT(cross2D(quads[0].Vertices[0].Position, quads[0].Vertices[1].Position, quads[0].Vertices[2].Position), 0.0f);
    EXPECT_GT(cross2D(quads[0].Vertices[2].Position, quads[0].Vertices[3].Position, quads[0].Vertices[0].Position), 0.0f);

    // Oversized 9-slice borders on 512x512 fallback texture must not invert center UVs
    panel.renderMode = Engine::UIRenderMode::NineSlice;
    panel.nineSliceBordersPx = { 300.0f, 300.0f, 300.0f, 300.0f };
    auto nineQuads = panel.GenerateQuads({ 0.0f, 0.0f }, { 800.0f, 800.0f }, 1.0f);
    ASSERT_EQ(nineQuads.size(), 9u);
    EXPECT_LE(nineQuads[4].Vertices[0].TexCoord.x, nineQuads[4].Vertices[1].TexCoord.x);
    EXPECT_LE(nineQuads[4].Vertices[0].TexCoord.y, nineQuads[4].Vertices[3].TexCoord.y);
}

TEST(UIButtonComponentTest, SameFrameSubmitNoCascadeNavigateChecksEnabledAndMultiSceneHoverCache) {
    Engine::Time::Reset();
    Engine::Input::Init();
    Engine::Input::BeginFrame();
    Engine::Input::SetViewportRect({ 0.0f, 0.0f }, { 1920.0f, 1080.0f });
    Engine::Time::SetMockDeltaTime(1.0f / 60.0f);
    Engine::Time::Update();
    Engine::Time::Update();
    Engine::Time::Update();

    Engine::Scene sceneA("SubmitCascadeSceneA");
    auto* goA = sceneA.CreateGameObject("BtnA");
    auto* goB = sceneA.CreateGameObject("BtnB");
    auto* goDisabled = sceneA.CreateGameObject("BtnDisabled");

    auto* btnA = goA->AddComponent<Engine::UIButtonComponent>();
    auto* btnB = goB->AddComponent<Engine::UIButtonComponent>();
    auto* btnDisabled = goDisabled->AddComponent<Engine::UIButtonComponent>();
    btnDisabled->SetEnabled(false);

    // NavigateFocus must skip disabled component even when Interactable == true
    btnA->SetNeighbors(0, goDisabled->GetUUID(), 0, 0);
    Engine::UIButtonComponent::SetFocusedButton(btnA);
    EXPECT_FALSE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Down, sceneA));
    EXPECT_EQ(Engine::UIButtonComponent::GetFocusedButton(), btnA);

    // Same-frame submit cascade: btnA->OnClick shifts focus to btnB; btnB must NOT click on the same frame
    int clicksA = 0;
    int clicksB = 0;
    btnA->OnClick = [&]() {
        ++clicksA;
        Engine::UIButtonComponent::SetFocusedButton(btnB);
    };
    btnB->OnClick = [&]() { ++clicksB; };

    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_RETURN, true);
    sceneA.Update(1.0f / 60.0f);
    EXPECT_EQ(clicksA, 1);
    EXPECT_EQ(clicksB, 0);
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_RETURN, false);

    // Multi-scene hover cache isolation in the same frame
    auto* panelA = goA->AddComponent<Engine::UIPanel>();
    panelA->useRectTransform = false;
    panelA->position = { 100.0f, 100.0f };
    panelA->size = { 200.0f, 80.0f };

    Engine::Scene sceneB("HoverSceneB");
    auto* goOther = sceneB.CreateGameObject("BtnOther");
    auto* panelOther = goOther->AddComponent<Engine::UIPanel>();
    panelOther->useRectTransform = false;
    panelOther->position = { 100.0f, 100.0f };
    panelOther->size = { 200.0f, 80.0f };
    auto* btnOther = goOther->AddComponent<Engine::UIButtonComponent>();
    int clicksOther = 0;
    btnOther->OnClick = [&]() { ++clicksOther; };

    Engine::Input::SetMousePositionForTesting({ 150.0f, 1080.0f - 140.0f });
    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, true);
    sceneA.Update(1.0f / 60.0f);
    sceneB.Update(1.0f / 60.0f);
    EXPECT_EQ(btnOther->GetState(), Engine::ButtonState::Pressed);

    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, false);
    Engine::Input::BeginFrame();
    Engine::UIButtonComponent::ClearFocus();
    Engine::Time::Reset();
}

} // namespace
