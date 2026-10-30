#include <gtest/gtest.h>
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/SpriteRenderer.h"

namespace {

TEST(AnimationClipTest, PropertiesAndFrameManagement) {
    Engine::AnimationClip clip("Walk", 0.15f, true);
    EXPECT_EQ(clip.name, "Walk");
    EXPECT_FLOAT_EQ(clip.frameDuration, 0.15f);
    EXPECT_TRUE(clip.loop);
    EXPECT_TRUE(clip.frames.empty());

    // Add dummy null frames (testing vector container)
    clip.AddFrame(nullptr);
    clip.AddFrame(nullptr);
    EXPECT_EQ(clip.frames.size(), 2u);
}

TEST(AnimatorTest, AddAndRetrieveClips) {
    Engine::GameObject entity("AnimatedEntity");
    auto* animator = entity.AddComponent<Engine::Animator>();

    ASSERT_NE(animator, nullptr);
    EXPECT_TRUE(animator->GetClips().empty());
    EXPECT_EQ(animator->GetCurrentClipName(), "");

    auto idleClip = std::make_shared<Engine::AnimationClip>("Idle", 0.2f, true);
    auto runClip = std::make_shared<Engine::AnimationClip>("Run", 0.1f, false);

    animator->AddClip(idleClip);
    animator->AddClip(runClip);
    animator->AddClip(nullptr); // Null clip safety check

    EXPECT_EQ(animator->GetClips().size(), 2u);
    EXPECT_NE(animator->GetClips().find("Idle"), animator->GetClips().end());
    EXPECT_NE(animator->GetClips().find("Run"), animator->GetClips().end());
}

TEST(AnimatorTest, PlaybackTransitions) {
    Engine::GameObject entity("Player");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto attackClip = std::make_shared<Engine::AnimationClip>("Attack", 0.1f, false);
    animator->AddClip(attackClip);

    // Play unknown clip (no-op)
    animator->Play("UnknownClip");
    EXPECT_EQ(animator->GetCurrentClipName(), "");

    // Play registered clip
    animator->Play("Attack");
    EXPECT_EQ(animator->GetCurrentClipName(), "Attack");

    // Playing already active clip does not reset
    animator->Play("Attack");
    EXPECT_EQ(animator->GetCurrentClipName(), "Attack");
}

TEST(AnimatorTest, UpdateAdvancesFramesAndHandlesLooping) {
    Engine::GameObject entity("Monster");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto loopClip = std::make_shared<Engine::AnimationClip>("Walk", 0.1f, true);
    loopClip->AddFrame(nullptr);
    loopClip->AddFrame(nullptr);
    animator->AddClip(loopClip);

    animator->Play("Walk");

    // Advance by half duration (should remain on frame 0)
    animator->Update(0.05f);

    // Advance by another 0.06f (total 0.11f -> advances to frame 1)
    animator->Update(0.06f);

    // Advance by another 0.10f (wraps back to frame 0 due to loop = true)
    animator->Update(0.10f);

    EXPECT_EQ(animator->GetCurrentClipName(), "Walk");
}

} // namespace

