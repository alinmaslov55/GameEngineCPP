#include <gtest/gtest.h>
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

TEST(PrefabAndClipAuthoringTest, PrefabExportAndInstantiationRoundTrip) {
    const std::string tempPrefabPath = "temp_authoring_test.prefab";

    // 1. Build an entity hierarchy with components
    auto srcScene = std::make_shared<Engine::Scene>("ExportSourceScene");
    auto* rootGo = srcScene->CreateGameObject("HeroCharacter");
    rootGo->GetTransform()->position = { 10.0f, 5.0f, 20.0f };

    auto* sprite = rootGo->AddComponent<Engine::SpriteRenderer>();
    sprite->SetColor({ 0.8f, 0.9f, 1.0f, 1.0f });

    auto* animator = rootGo->AddComponent<Engine::Animator>();
    auto walkClip = std::make_shared<Engine::AnimationClip>("Walk", 0.1f, true);
    walkClip->AddEvent(1, "Footstep", { 0.2f, 0.0f });
    walkClip->AddEvent(3, "HitboxActive", { 0.8f, 0.2f });
    animator->AddClip(walkClip);
    animator->Play("Walk");

    auto* childGo = srcScene->CreateGameObject("TorchLight");
    childGo->GetTransform()->position = { 0.5f, 1.2f, 0.0f };
    childGo->GetTransform()->SetParent(rootGo->GetTransform(), /*keepWorldTransform=*/false);
    auto* light = childGo->AddComponent<Engine::PointLightComponent>();
    light->Intensity = 4.0f;

    // 2. Snapshot entity hierarchy into Prefab (zeroRootPosition = true)
    auto prefab = Engine::Prefab::CreateFromGameObject(rootGo, /*zeroRootPosition=*/true);
    ASSERT_NE(prefab, nullptr);
    EXPECT_TRUE(prefab->IsValid());

    // 3. Save to disk and reload
    EXPECT_TRUE(prefab->SaveToFile(tempPrefabPath));
    auto loadedPrefab = Engine::Prefab::LoadFromFile(tempPrefabPath);
    ASSERT_NE(loadedPrefab, nullptr);
    EXPECT_TRUE(loadedPrefab->IsValid());

    // 4. Instantiate in a new scene at target ground position
    auto dstScene = std::make_shared<Engine::Scene>("ImportDestinationScene");
    const glm::vec3 spawnPos(15.0f, 0.0f, -8.0f);
    auto* spawnedRoot = dstScene->InstantiatePrefab(*loadedPrefab, spawnPos);
    ASSERT_NE(spawnedRoot, nullptr);

    // Assert root position matches requested spawnPos exactly
    EXPECT_EQ(spawnedRoot->GetName(), "HeroCharacter");
    EXPECT_FLOAT_EQ(spawnedRoot->GetTransform()->position.x, 15.0f);
    EXPECT_FLOAT_EQ(spawnedRoot->GetTransform()->position.y, 0.0f);
    EXPECT_FLOAT_EQ(spawnedRoot->GetTransform()->position.z, -8.0f);

    // Verify SpriteRenderer preserved
    auto* spawnedSprite = spawnedRoot->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(spawnedSprite, nullptr);
    EXPECT_FLOAT_EQ(spawnedSprite->GetColor().r, 0.8f);

    // Verify Animator & Events preserved
    auto* spawnedAnimator = spawnedRoot->GetComponent<Engine::Animator>();
    ASSERT_NE(spawnedAnimator, nullptr);
    auto spawnedWalkClip = spawnedAnimator->GetClip("Walk");
    ASSERT_NE(spawnedWalkClip, nullptr);
    ASSERT_EQ(spawnedWalkClip->GetEvents().size(), 2u);
    EXPECT_EQ(spawnedWalkClip->GetEvents()[0].EventID, "Footstep");
    EXPECT_EQ(spawnedWalkClip->GetEvents()[1].EventID, "HitboxActive");

    // Verify child hierarchy preserved
    ASSERT_EQ(spawnedRoot->GetTransform()->GetChildren().size(), 1u);
    auto* spawnedChildTr = spawnedRoot->GetTransform()->GetChildren()[0];
    ASSERT_NE(spawnedChildTr, nullptr);
    EXPECT_FLOAT_EQ(spawnedChildTr->position.x, 0.5f);
    EXPECT_FLOAT_EQ(spawnedChildTr->position.y, 1.2f);
    EXPECT_FLOAT_EQ(spawnedChildTr->position.z, 0.0f);

    auto* spawnedLight = spawnedChildTr->GetGameObject()->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(spawnedLight, nullptr);
    EXPECT_FLOAT_EQ(spawnedLight->Intensity, 4.0f);

    // Clean up temporary file
    std::error_code ec;
    std::filesystem::remove(tempPrefabPath, ec);
}

TEST(PrefabAndClipAuthoringTest, GroundPlaneRaycastIntersection) {
    Engine::PerspectiveCamera camera(45.0f, 1.777f, 0.1f, 1000.0f);
    camera.SetFocalPoint({ 0.0f, 0.0f, 0.0f });
    camera.SetDistance(15.0f);
    camera.SetPitchDegrees(45.0f);

    glm::vec3 hitPos{ 0.0f };
    const bool hit = camera.RaycastGroundPlane(glm::vec2(0.0f, 0.0f), 0.0f, hitPos);
    EXPECT_TRUE(hit);
    EXPECT_NEAR(hitPos.y, 0.0f, 1e-4f);
    EXPECT_NEAR(hitPos.x, 0.0f, 1e-4f);
    EXPECT_NEAR(hitPos.z, 0.0f, 1e-2f);
}

TEST(PrefabAndClipAuthoringTest, AnimatorScrubbingAndFlipXSync) {
    Engine::GameObject entity("ScrubTestEntity");
    auto* spriteRenderer = entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Attack", 0.1f, false);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    clip->AddFrame(nullptr); // Frame 3
    animator->AddClip(clip);
    animator->Play("Attack");

    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // Scrub to Frame 2
    animator->ScrubToFrame(2);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 2u);
    EXPECT_FLOAT_EQ(animator->GetCurrentTime(), 0.0f);

    // Scrub out of bounds clamps to last frame (Frame 3)
    animator->ScrubToFrame(100);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 3u);

    // FlipX sync with SpriteRenderer
    EXPECT_FALSE(animator->GetCurrentFlipX());
    EXPECT_FALSE(spriteRenderer->GetFlipX());

    animator->SetCurrentFlipX(true);
    EXPECT_TRUE(animator->GetCurrentFlipX());
    EXPECT_TRUE(spriteRenderer->GetFlipX());

    animator->SetCurrentFlipX(false);
    EXPECT_FALSE(animator->GetCurrentFlipX());
    EXPECT_FALSE(spriteRenderer->GetFlipX());
}

TEST(PrefabAndClipAuthoringTest, AnimationClipEventAuthoringAndDispatch) {
    Engine::GameObject entity("EventAuthoringEntity");
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Combo", 0.08f, false);
    clip->AddFrame(nullptr); // 0
    clip->AddFrame(nullptr); // 1
    clip->AddFrame(nullptr); // 2
    clip->AddFrame(nullptr); // 3
    clip->AddFrame(nullptr); // 4

    // Add 3 events
    clip->AddEvent(1, "Footstep", { 0.2f, 0.0f });
    clip->AddEvent(3, "HitboxActive", { 0.8f, 0.1f });
    clip->AddEvent(4, "HitboxEnd", { 0.8f, 0.1f });

    ASSERT_EQ(clip->GetEvents().size(), 3u);

    // Modify event via mutable reference
    clip->GetEventsMutable()[1].SocketOffset = { 1.25f, 0.35f };
    EXPECT_FLOAT_EQ(clip->GetEvents()[1].SocketOffset.x, 1.25f);
    EXPECT_FLOAT_EQ(clip->GetEvents()[1].SocketOffset.y, 0.35f);

    // Remove event at index 0 (Footstep)
    EXPECT_TRUE(clip->RemoveEvent(0));
    ASSERT_EQ(clip->GetEvents().size(), 2u);
    EXPECT_EQ(clip->GetEvents()[0].EventID, "HitboxActive");
    EXPECT_EQ(clip->GetEvents()[0].FrameIndex, 3u);
    EXPECT_FALSE(clip->RemoveEvent(99)); // Invalid index returns false

    animator->AddClip(clip);
    animator->Play("Combo");

    std::vector<std::string> firedEvents;
    animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent&) {
        firedEvents.emplace_back(eventID);
    });

    // Scrub to Frame 3 with triggerEvents = true
    animator->ScrubToFrame(3, /*triggerEvents=*/true);
    ASSERT_EQ(firedEvents.size(), 1u);
    EXPECT_EQ(firedEvents[0], "HitboxActive");
}

TEST(PrefabAndClipAuthoringTest, AnimatorPreviewPlayingMode) {
    Engine::GameObject entity("PreviewEntity");
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Idle", 0.1f, true);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    animator->AddClip(clip);
    animator->Play("Idle");

    EXPECT_FALSE(animator->IsPreviewPlaying());
    animator->SetPreviewPlaying(true);
    EXPECT_TRUE(animator->IsPreviewPlaying());

    // Advance 0.15s -> should advance to Frame 1
    animator->Update(0.15f);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);

    // Scrubbing to a frame pauses preview playing
    animator->ScrubToFrame(0);
    EXPECT_FALSE(animator->IsPreviewPlaying());
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);
}

TEST(PrefabAndClipAuthoringTest, AnimatorPreviewActivationWhenNoClipIsPlaying) {
    auto scene = std::make_shared<Engine::Scene>("PreviewActivationScene");
    auto* go = scene->CreateGameObject("AnimEntity");
    go->AddComponent<Engine::SpriteRenderer>();
    auto* animator = go->AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Walk", 0.1f, true);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    animator->AddClip(clip);

    // No clip is initially playing
    ASSERT_EQ(animator->GetCurrentClip(), nullptr);

    // Enabling preview activates the registered clip and advances on Update()
    animator->SetPreviewPlaying(true);
    EXPECT_TRUE(animator->IsPreviewPlaying());
    ASSERT_NE(animator->GetCurrentClip(), nullptr);

    animator->Update(0.15f);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);
}

TEST(PrefabAndClipAuthoringTest, AnimationClipSetUniformFrameDurationSynchronizesAllFrames) {
    auto clip = std::make_shared<Engine::AnimationClip>("Slash", 0.1f, false);
    clip->AddFrame(nullptr);
    clip->AddFrame(nullptr);
    clip->AddFrame(nullptr);

    ASSERT_EQ(clip->Frames.size(), 3u);
    EXPECT_FLOAT_EQ(clip->GetTotalDuration(), 0.3f);

    clip->SetUniformFrameDuration(0.25f);
    EXPECT_FLOAT_EQ(clip->frameDuration, 0.25f);
    for (const auto& frame : clip->Frames) {
        EXPECT_FLOAT_EQ(frame.DurationSeconds, 0.25f);
    }
    EXPECT_FLOAT_EQ(clip->GetTotalDuration(), 0.75f);
}

} // namespace
