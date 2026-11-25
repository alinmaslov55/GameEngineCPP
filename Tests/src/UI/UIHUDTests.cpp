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
#include "Engine/Scene/Components/LightComponents.h"
#include <algorithm>
#include <vector>

namespace {

TEST(RadialWheelTest, FourQuadrantAngleSelectionAndDeadzone) {
    constexpr float kPi = 3.14159265358979323846f;

    // Wedge 0: Top [45 deg .. 135 deg]
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi * 0.5f), 0);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi * 0.35f), 0);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi * 0.65f), 0);

    // Wedge 1: Right [-45 deg .. 45 deg]
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(0.0f), 1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi * 0.15f), 1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi * 0.15f), 1);

    // Wedge 2: Bottom [-135 deg .. -45 deg]
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi * 0.5f), 2);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi * 0.35f), 2);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi * 0.65f), 2);

    // Wedge 3: Left (> 135 deg or < -135 deg)
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi), 3);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi), 3);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(kPi * 0.85f), 3);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromAngle(-kPi * 0.85f), 3);

    // Deadzone vector testing
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.2f, 0.2f), 0.35f), -1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.0f, 0.0f), 0.35f), -1);

    // Vectors past deadzone
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.0f, 0.8f), 0.35f), 0);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.8f, 0.0f), 0.35f), 1);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.0f, -0.8f), 0.35f), 2);
    EXPECT_EQ(Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(-0.8f, 0.0f), 0.35f), 3);
}

TEST(RadialWheelTest, StickReleaseConfirmationAndAffordability) {
    Engine::UIRadialWheelComponent wheel;
    EXPECT_FALSE(wheel.IsOpen);

    wheel.Open();
    EXPECT_TRUE(wheel.IsOpen);
    EXPECT_EQ(wheel.ActiveWedge, -1);

    int confirmedWedge = -1;
    Engine::RadialWheelItem confirmedItem = Engine::RadialWheelItem::None;
    wheel.OnConfirmDrop = [&](int w, Engine::RadialWheelItem item) {
        confirmedWedge = w;
        confirmedItem = item;
    };

    // 1. Stick deflection into Top quadrant (Wedge 0: Splinter Barricade, Cost: 5)
    wheel.AvailableWood = 10;
    wheel.ActiveWedge = Engine::UIRadialWheelComponent::ResolveWedgeFromVector(glm::vec2(0.0f, 0.7f), 0.35f);
    EXPECT_EQ(wheel.ActiveWedge, 0);
    wheel.WasStickDeflected = true;

    // Simulate stick return to deadzone (< 0.15f)
    const float stickMag = 0.05f;
    if (wheel.WasStickDeflected && stickMag < 0.15f) {
        EXPECT_TRUE(wheel.ConfirmSelection(wheel.ActiveWedge));
    }

    EXPECT_EQ(confirmedWedge, 0);
    EXPECT_EQ(confirmedItem, Engine::RadialWheelItem::SplinterBarricade);
    EXPECT_FALSE(wheel.IsOpen); // Closed on drop confirmation!

    // 2. Unaffordable item rejection (Wedge 3: Tar Siphon, Cost: 20, Player has 10 wood)
    wheel.Open();
    wheel.AvailableWood = 10;
    wheel.ActiveWedge = 3;
    confirmedWedge = -1;

    EXPECT_FALSE(wheel.ConfirmSelection(3)); // Rejected!
    EXPECT_EQ(confirmedWedge, -1);
    EXPECT_TRUE(wheel.IsOpen); // Remains open!
}

TEST(RadialWheelTest, InstantKeyAndDpadShortcuts) {
    Engine::UIRadialWheelComponent wheel;
    wheel.AvailableWood = 50;
    wheel.Open();

    int lastConfirmed = -1;
    wheel.OnConfirmDrop = [&](int w, Engine::RadialWheelItem) {
        lastConfirmed = w;
    };

    // Confirm Wedge 1 (Flare Peg)
    EXPECT_TRUE(wheel.ConfirmSelection(1));
    EXPECT_EQ(lastConfirmed, 1);
    EXPECT_FALSE(wheel.IsOpen);

    // Confirm Wedge 2 (Thumper)
    wheel.Open();
    EXPECT_TRUE(wheel.ConfirmSelection(2));
    EXPECT_EQ(lastConfirmed, 2);
    EXPECT_FALSE(wheel.IsOpen);
}

TEST(MinimapFogTest, ToroidalTexelStampingAndViewerScrolling) {
    Engine::MinimapFogSystem fog;

    // 1. Initial position at origin (0, 0) with Hero Torch radius 15m (10 texels)
    fog.Update(glm::vec2(0.0f, 0.0f), 15.0f);

    EXPECT_TRUE(fog.IsDiscovered(glm::vec2(0.0f, 0.0f)));
    EXPECT_GT(fog.GetLiveIllumination(glm::vec2(0.0f, 0.0f)), 0.9f);

    // Coordinate at (100, 100) is far away (~141.4m > 15m): dark
    EXPECT_FLOAT_EQ(fog.GetLiveIllumination(glm::vec2(100.0f, 100.0f)), 0.0f);

    // 2. Move viewer from (0, 0) -> (100, 100)
    fog.Update(glm::vec2(100.0f, 100.0f), 15.0f);

    // (100, 100) is now live lit and discovered
    EXPECT_TRUE(fog.IsDiscovered(glm::vec2(100.0f, 100.0f)));
    EXPECT_GT(fog.GetLiveIllumination(glm::vec2(100.0f, 100.0f)), 0.9f);

    // (0, 0) is 141.4m away, still inside the 192m window radius (256x256 * 1.5m):
    // Permanent discovery holds! Live illumination drops to 0.
    EXPECT_TRUE(fog.IsDiscovered(glm::vec2(0.0f, 0.0f)));
    EXPECT_FLOAT_EQ(fog.GetLiveIllumination(glm::vec2(0.0f, 0.0f)), 0.0f);

    // 3. Coordinate conversion testing
    const glm::ivec2 gCoord = Engine::MinimapFogSystem::WorldToGridCoord(glm::vec2(150.0f, -75.0f));
    EXPECT_EQ(gCoord.x, 100);
    EXPECT_EQ(gCoord.y, -50);

    const glm::ivec2 tCoord = Engine::MinimapFogSystem::GridToToroidalTexel(gCoord);
    EXPECT_EQ(tCoord.x, 100);
    EXPECT_EQ(tCoord.y, 206); // -50 mod 256 = 206
}

TEST(MinimapFogTest, POIPinClampingToCircularRim) {
    const glm::vec2 viewer(0.0f, 0.0f);
    constexpr float kMaxRadius = 192.0f;

    // Inside radius: POI at (96m, 0m)
    bool isClamped = false;
    glm::vec2 pin = Engine::MinimapFogSystem::ProjectAndClampPOIPin(glm::vec2(96.0f, 0.0f), viewer, kMaxRadius, isClamped);
    EXPECT_FALSE(isClamped);
    EXPECT_FLOAT_EQ(pin.x, 0.5f);
    EXPECT_FLOAT_EQ(pin.y, 0.0f);

    // Outside radius: POI at (384m, 0m) -> clamped to rim (1.0, 0.0)
    pin = Engine::MinimapFogSystem::ProjectAndClampPOIPin(glm::vec2(384.0f, 0.0f), viewer, kMaxRadius, isClamped);
    EXPECT_TRUE(isClamped);
    EXPECT_FLOAT_EQ(pin.x, 1.0f);
    EXPECT_FLOAT_EQ(pin.y, 0.0f);

    // Outside radius: POI North at (0m, -500m) -> clamped to upper rim (0.0, +1.0)
    pin = Engine::MinimapFogSystem::ProjectAndClampPOIPin(glm::vec2(0.0f, -500.0f), viewer, kMaxRadius, isClamped);
    EXPECT_TRUE(isClamped);
    EXPECT_FLOAT_EQ(pin.x, 0.0f);
    EXPECT_FLOAT_EQ(pin.y, 1.0f);
    EXPECT_NEAR(glm::length(pin), 1.0f, 1e-5f);
}

TEST(MinimapFogTest, VignetteCircularAlphaMask) {
    Engine::MinimapFogSystem fog;
    const auto& buffer = fog.GetPixelBuffer();
    ASSERT_EQ(buffer.size(), 256u * 256u * 4u);

    // Center pixel (128, 128): offset (0, 0), r = 0 <= 120 -> Alpha = 255
    const size_t centerIdx = (128u * 256u + 128u) * 4u;
    EXPECT_EQ(buffer[centerIdx + 3], 255u);

    // Corner pixel (0, 0): offset (-128, -128), r ~ 181 > 126 -> Alpha = 0 (transparent outside circle)
    const size_t cornerIdx = 0u;
    EXPECT_EQ(buffer[cornerIdx + 3], 0u);
}

TEST(UIRadialWheelTest, SceneSerializerRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    auto* go = sourceScene->CreateGameObject("RadialWheel");
    const uint64_t goUUID = go->GetUUID();

    auto* wheel = go->AddComponent<Engine::UIRadialWheelComponent>();
    wheel->SlowMoTimeScale = 0.22f;
    wheel->AvailableWood = 45;
    wheel->Slots[0].WoodCost = 8;
    wheel->Slots[1].WoodCost = 14;

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(goUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstWheel = dstObj->GetComponent<Engine::UIRadialWheelComponent>();
    ASSERT_NE(dstWheel, nullptr);
    EXPECT_FLOAT_EQ(dstWheel->SlowMoTimeScale, 0.22f);
    EXPECT_EQ(dstWheel->AvailableWood, 45u);
    EXPECT_EQ(dstWheel->Slots[0].WoodCost, 8u);
    EXPECT_EQ(dstWheel->Slots[1].WoodCost, 14u);
}

TEST(UIMinimapTest, SceneSerializerRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    auto* go = sourceScene->CreateGameObject("MinimapHUD");
    const uint64_t goUUID = go->GetUUID();

    auto* minimap = go->AddComponent<Engine::UIMinimapComponent>();
    minimap->pinSize = 11.5f;
    minimap->frameColor = { 0.8f, 0.7f, 0.5f, 1.0f };
    minimap->playerPinColor = { 1.0f, 0.9f, 0.8f, 1.0f };

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(goUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstMinimap = dstObj->GetComponent<Engine::UIMinimapComponent>();
    ASSERT_NE(dstMinimap, nullptr);
    EXPECT_FLOAT_EQ(dstMinimap->pinSize, 11.5f);
    EXPECT_FLOAT_EQ(dstMinimap->frameColor.r, 0.8f);
    EXPECT_FLOAT_EQ(dstMinimap->playerPinColor.g, 0.9f);
}

TEST(MinimapFogTest, NorthZAxisMappingDiscoveredBrazierVsUndiscoveredShrineAndPinQuadTexture) {
    Engine::Scene scene("MinimapNorthAndPOIScene");
    auto fog = std::make_shared<Engine::MinimapFogSystem>();

    // Stamp exploration North of viewer (viewer = (0,0), torch moved to (0, -30))
    fog->Update(glm::vec2(0.0f, 0.0f), 10.0f);
    fog->StampExploration(glm::vec2(0.0f, -30.0f), 8.0f, true);
    fog->Update(glm::vec2(0.0f, 0.0f), 10.0f); // RebuildDisplayBuffer around (0,0)

    // North exploration (-Z = -30 -> dz = -20 -> vy = 128 - (-20) = 148 > 128) must lie in upper half of display buffer
    const auto& buf = fog->GetPixelBuffer();
    const size_t northIdx = (148u * Engine::MinimapFogSystem::TextureSize + 128u) * 4u;
    const size_t southIdx = (108u * Engine::MinimapFogSystem::TextureSize + 128u) * 4u;
    EXPECT_EQ(buf[northIdx + 1], Engine::MinimapFogSystem::kDiscoveredAlphaByte);
    EXPECT_EQ(buf[southIdx + 1], 0u);

    // Place a discovered (ignited) Brazier at (0, -40) and an undiscovered Shrine at (0, -100)
    auto* brazier = scene.CreateGameObject("IgnitedBrazier");
    brazier->GetTransform()->position = { 0.0f, 0.0f, -40.0f };
    auto* brazierLight = brazier->AddComponent<Engine::PointLightComponent>();
    brazierLight->Semantic = Engine::LightSemantic::SafeLifeline;
    brazierLight->Radius = 6.0f;
    brazierLight->Intensity = 2.0f;

    auto* shrine = scene.CreateGameObject("UndiscoveredShrine");
    shrine->GetTransform()->position = { 0.0f, 0.0f, -100.0f };

    fog->UpdateFromScene(scene, glm::vec2(0.0f, 0.0f));

    // Only the discovered Brazier pin should be emitted; undiscovered Shrine must be excluded
    const auto& pins = fog->GetActivePins();
    ASSERT_EQ(pins.size(), 1u);
    EXPECT_EQ(pins[0].Label, "IgnitedBrazier");
    EXPECT_GT(pins[0].MinimapLocalPos.y, 0.0f);

    // Generate UIQuads and verify Brazier pin projects to upper half (Y > center.y) and carries QuadTexture == nullptr
    Engine::UIMinimapComponent minimap;
    minimap.fogSystem = fog;
    const glm::vec2 resolvedPos{ 100.0f, 100.0f };
    const glm::vec2 resolvedSize{ 200.0f, 200.0f };
    const glm::vec2 center = resolvedPos + resolvedSize * 0.5f;
    const auto quads = minimap.GenerateQuads(resolvedPos, resolvedSize, 1.0f);

    // Quad 0: fog/bg, Quad 1: rim, Quad 2: player pin, Quad 3: Brazier pin
    ASSERT_EQ(quads.size(), 4u);
    const auto& brazierQuad = quads[3];
    const float quadCenterY = 0.5f * (brazierQuad.Vertices[0].Position.y + brazierQuad.Vertices[2].Position.y);
    EXPECT_GT(quadCenterY, center.y);
    EXPECT_EQ(brazierQuad.QuadTexture, nullptr);
    EXPECT_EQ(quads[2].QuadTexture, nullptr); // Player pin also has nullptr
}

TEST(MinimapFogTest, ToroidalOutOfWindowQueriesDoNotAlias) {
    Engine::MinimapFogSystem fog;
    fog.Update(glm::vec2(0.0f, 0.0f), 10.0f);
    EXPECT_TRUE(fog.IsDiscovered(glm::vec2(0.0f, 0.0f)));
    EXPECT_GT(fog.GetLiveIllumination(glm::vec2(0.0f, 0.0f)), 0.5f);

    // 384m away aliases to toroidal texel (0,0), but is outside active [center-128, center+127] window
    EXPECT_FALSE(fog.IsDiscovered(glm::vec2(384.0f, 0.0f)));
    EXPECT_FALSE(fog.IsDiscovered(glm::vec2(0.0f, -384.0f)));
    EXPECT_FLOAT_EQ(fog.GetLiveIllumination(glm::vec2(384.0f, 0.0f)), 0.0f);
    EXPECT_FLOAT_EQ(fog.GetLiveIllumination(glm::vec2(0.0f, -384.0f)), 0.0f);
}

TEST(RadialWheelTest, QuadTextureIsolationPerQuad) {
    Engine::UIRadialWheelComponent wheel;
    wheel.IsOpen = true;
    wheel.ActiveWedge = 0;
    const auto quads = wheel.GenerateQuads({ 0.0f, 0.0f }, { 360.0f, 360.0f }, 1.0f);
    ASSERT_EQ(quads.size(), 9u); // 1 bg + 4*(sector + icon)
    for (const auto& q : quads) {
        EXPECT_EQ(q.QuadTexture, nullptr);
    }
}

} // namespace
