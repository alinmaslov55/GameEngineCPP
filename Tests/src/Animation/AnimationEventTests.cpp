#include <gtest/gtest.h>
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include <glm/glm.hpp>
#include <cmath>

namespace {

TEST(AnimationEventTest, AddAndRetrieveOnClip) {
    Engine::AnimationClip clip("HeavySlash", 0.08f, false);
    EXPECT_TRUE(clip.GetEvents().empty());

    clip.AddEvent(2, "HitboxActive", { 0.8f, 0.2f });
    clip.AddEvent(4, "HitboxEnd", { 0.8f, 0.2f });

    ASSERT_EQ(clip.GetEvents().size(), 2u);
    EXPECT_EQ(clip.GetEvents()[0].FrameIndex, 2u);
    EXPECT_EQ(clip.GetEvents()[0].EventID, "HitboxActive");
    EXPECT_FLOAT_EQ(clip.GetEvents()[0].SocketOffset.x, 0.8f);
    EXPECT_FLOAT_EQ(clip.GetEvents()[0].SocketOffset.y, 0.2f);

    EXPECT_EQ(clip.GetEvents()[1].FrameIndex, 4u);
    EXPECT_EQ(clip.GetEvents()[1].EventID, "HitboxEnd");

    clip.ClearEvents();
    EXPECT_TRUE(clip.GetEvents().empty());
}

TEST(AnimatorEventTest, SingleFrameAdvanceFiresEvent) {
    Engine::GameObject entity("Player");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Walk", 0.10f, true);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    clip->AddEvent(1, "Footstep", { 0.1f, -0.4f });
    animator->AddClip(clip);

    std::vector<std::pair<std::string, uint32_t>> fired;
    animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent& ev) {
        fired.emplace_back(std::string(eventID), ev.FrameIndex);
    });

    animator->Play("Walk");
    EXPECT_TRUE(fired.empty());
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // Advance 0.11s -> advances 0 -> 1, triggering Footstep
    animator->Update(0.11f);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].first, "Footstep");
    EXPECT_EQ(fired[0].second, 1u);
}

TEST(AnimatorEventTest, FrameSkipGuaranteeOnLargeDelta) {
    // Specification: Attach "ChopImpact" on frame 3 of a 6-frame clip (0.05s/frame);
    // advance Animator::Update(0.22f) in a single large step and verify "ChopImpact" still fires exactly once.
    Engine::GameObject entity("Hero");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Chop", 0.05f, false);
    for (int i = 0; i < 6; ++i) {
        clip->AddFrame(nullptr);
    }
    clip->AddEvent(3, "ChopImpact", { 1.2f, 0.3f });
    animator->AddClip(clip);

    int impactFiredCount = 0;
    animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent& ev) {
        if (eventID == "ChopImpact") {
            impactFiredCount++;
            EXPECT_EQ(ev.FrameIndex, 3u);
            EXPECT_FLOAT_EQ(ev.SocketOffset.x, 1.2f);
        }
    });

    animator->Play("Chop");
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);
    EXPECT_EQ(impactFiredCount, 0);

    // Large delta hitch: 0.22s in a single step (0.22 / 0.05 = 4 frames traversed: 0 -> 1 -> 2 -> 3 -> 4)
    animator->Update(0.22f);

    EXPECT_EQ(animator->GetCurrentFrameIndex(), 4u);
    EXPECT_EQ(impactFiredCount, 1); // Frame-Skip Guarantee: fired exactly once!
}

TEST(AnimatorEventTest, IntermediateFrameEventsFiredInChronologicalOrder) {
    Engine::GameObject entity("Warrior");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Combo", 0.05f, false);
    for (int i = 0; i < 6; ++i) {
        clip->AddFrame(nullptr);
    }
    clip->AddEvent(1, "Windup");
    clip->AddEvent(2, "HitboxActive");
    clip->AddEvent(3, "SlashImpact");
    clip->AddEvent(4, "HitboxEnd");
    animator->AddClip(clip);

    std::vector<std::string> sequence;
    animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent&) {
        sequence.emplace_back(eventID);
    });

    animator->Play("Combo");
    // Single large delta step 0.26s traversals: 0 -> 1 -> 2 -> 3 -> 4 -> 5
    animator->Update(0.26f);

    const std::vector<std::string> expected = { "Windup", "HitboxActive", "SlashImpact", "HitboxEnd" };
    EXPECT_EQ(sequence, expected);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 5u);
}

TEST(AnimatorEventTest, Frame0EventFiresOnFirstUpdateAndLoopWrap) {
    Engine::GameObject entity("Spinner");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Spin", 0.10f, true);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    clip->AddEvent(0, "LoopBegin");
    animator->AddClip(clip);

    int loopBeginCount = 0;
    animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent& ev) {
        if (eventID == "LoopBegin") {
            loopBeginCount++;
            EXPECT_EQ(ev.FrameIndex, 0u);
        }
    });

    animator->Play("Spin");
    EXPECT_EQ(loopBeginCount, 0);

    // 1. First small update (0.01s < 0.10s, stays on frame 0) -> Frame 0 event fires once on initial start!
    animator->Update(0.01f);
    EXPECT_EQ(loopBeginCount, 1);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // 2. Subsequent small update on frame 0 -> MUST NOT re-fire
    animator->Update(0.02f);
    EXPECT_EQ(loopBeginCount, 1);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // 3. Advance across frame 1, 2, and wrap back to frame 0 (need 0.27s: remaining 0.07s on frame 0 + 0.10s on frame 1 + 0.10s on frame 2)
    animator->Update(0.28f);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);
    EXPECT_EQ(loopBeginCount, 2); // Fired again on loop wrap-around!
}

TEST(AnimatorEventTest, RemoveEventListenerStopsNotifications) {
    Engine::GameObject entity("ListenerEntity");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("TestClip", 0.05f, false);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddFrame(nullptr); // Frame 2
    clip->AddEvent(1, "EventA");
    clip->AddEvent(2, "EventB");
    animator->AddClip(clip);

    int countA = 0;
    int countB = 0;
    uint32_t handle = animator->AddEventListener([&](std::string_view eventID, const Engine::AnimationEvent&) {
        if (eventID == "EventA") countA++;
        if (eventID == "EventB") countB++;
    });

    EXPECT_EQ(animator->GetEventListenerCount(), 1u);
    animator->Play("TestClip");

    // Advance to frame 1 -> EventA fires
    animator->Update(0.06f);
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 0);

    // Remove listener before frame 2
    EXPECT_TRUE(animator->RemoveEventListener(handle));
    EXPECT_EQ(animator->GetEventListenerCount(), 0u);

    // Advance to frame 2 -> EventB must NOT notify the removed listener
    animator->Update(0.06f);
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 0);
}

TEST(AnimatorEventTest, SocketOffsetFlippedWhenFlipX) {
    Engine::GameObject entity("SocketEntity");
    [[maybe_unused]] auto* sprite = entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    Engine::AnimationEvent ev;
    ev.FrameIndex = 2;
    ev.EventID = "Sparks";
    ev.SocketOffset = { 1.5f, 0.4f };

    // When facing East / normal (FlipX == false): offset is unchanged
    animator->SetCurrentFlipX(false);
    glm::vec2 normalOffset = animator->GetEventSocketOffset(ev);
    EXPECT_FLOAT_EQ(normalOffset.x, 1.5f);
    EXPECT_FLOAT_EQ(normalOffset.y, 0.4f);

    // When facing West / flipped (FlipX == true): offset X is mirrored across center
    animator->SetCurrentFlipX(true);
    glm::vec2 flippedOffset = animator->GetEventSocketOffset(ev);
    EXPECT_FLOAT_EQ(flippedOffset.x, -1.5f);
    EXPECT_FLOAT_EQ(flippedOffset.y, 0.4f);
}

TEST(AnimatorEventTest, ReentrantListenerRemovalDuringCallback) {
    Engine::GameObject entity("ReentrantEntity");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Clip", 0.05f, false);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    clip->AddEvent(1, "SelfDestruct");
    animator->AddClip(clip);

    uint32_t handle1 = 0;
    int listener2Calls = 0;

    handle1 = animator->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        // Re-entrant listener removal inside its own callback
        animator->RemoveEventListener(handle1);
    });

    animator->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        listener2Calls++;
    });

    animator->Play("Clip");
    // Triggering frame 1 must safely execute both callbacks without iterator corruption
    EXPECT_NO_THROW(animator->Update(0.06f));
    EXPECT_EQ(listener2Calls, 1);
    EXPECT_EQ(animator->GetEventListenerCount(), 1u); // handle1 removed cleanly
}

TEST(AnimatorEventTest, SceneSerializerAnimationEventsRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    const uint64_t entityUUID = 999111ULL;
    auto* srcObj = sourceScene->CreateGameObjectWithUUID(entityUUID, "CombatStandee");
    srcObj->AddComponent<Engine::SpriteRenderer>();
    auto* srcAnim = srcObj->AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("AttackHeavy", 0.10f, false);
    clip->AddFrame(nullptr, 0.15f);
    clip->AddFrame(nullptr, 0.10f);
    clip->AddFrame(nullptr, 0.20f);
    clip->AddEvent(1, "HitboxActive", { 0.95f, 0.25f });
    clip->AddEvent(2, "HitboxEnd", { 0.40f, 0.10f });
    srcAnim->AddClip(clip);
    srcAnim->Play("AttackHeavy");

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(entityUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstAnim = dstObj->GetComponent<Engine::Animator>();
    ASSERT_NE(dstAnim, nullptr);

    auto restoredClip = dstAnim->GetClip("AttackHeavy");
    ASSERT_NE(restoredClip, nullptr);
    ASSERT_EQ(restoredClip->GetEvents().size(), 2u);

    EXPECT_EQ(restoredClip->GetEvents()[0].FrameIndex, 1u);
    EXPECT_EQ(restoredClip->GetEvents()[0].EventID, "HitboxActive");
    EXPECT_FLOAT_EQ(restoredClip->GetEvents()[0].SocketOffset.x, 0.95f);
    EXPECT_FLOAT_EQ(restoredClip->GetEvents()[0].SocketOffset.y, 0.25f);

    EXPECT_EQ(restoredClip->GetEvents()[1].FrameIndex, 2u);
    EXPECT_EQ(restoredClip->GetEvents()[1].EventID, "HitboxEnd");
    EXPECT_FLOAT_EQ(restoredClip->GetEvents()[1].SocketOffset.x, 0.40f);
    EXPECT_FLOAT_EQ(restoredClip->GetEvents()[1].SocketOffset.y, 0.10f);
}

} // namespace
