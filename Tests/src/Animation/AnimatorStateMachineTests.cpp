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

TEST(AnimatorTest, UnattachedAnimatorAndMultiFrameHitchDoNotCrash) {
    // 1. Unattached Animator (null GetGameObject()) must not crash on UpdateSpriteRenderer
    Engine::Animator unattached;
    auto clip = std::make_shared<Engine::AnimationClip>("Attack", 0.05f, false);
    clip->AddFrame(nullptr);
    clip->AddFrame(nullptr);
    clip->AddFrame(nullptr);
    clip->AddFrame(nullptr);

    unattached.AddClip(clip);
    unattached.Play("Attack");

    // Advance across a 0.20s frame hitch (4 frames worth of duration)
    // Must loop/advance without crashing on null GameObject
    unattached.Update(0.20f);
    EXPECT_EQ(unattached.GetCurrentClipName(), "Attack");
}

TEST(SpriteRendererTest, FlipXAndFlipYProperties) {
    Engine::SpriteRenderer sprite;
    EXPECT_FALSE(sprite.GetFlipX());
    EXPECT_FALSE(sprite.GetFlipY());

    sprite.SetFlipX(true);
    EXPECT_TRUE(sprite.GetFlipX());

    sprite.SetFlipY(true);
    EXPECT_TRUE(sprite.GetFlipY());

    sprite.SetFlipX(false);
    EXPECT_FALSE(sprite.GetFlipX());
    EXPECT_TRUE(sprite.GetFlipY());
}

TEST(AnimationClipTest, VariableHoldFrames) {
    Engine::AnimationClip clip("HeavyMaulSmash", 0.1f, false);

    // Frame 0: Windup anticipation hold (300ms)
    clip.AddFrame(nullptr, 0.30f);
    // Frame 1: Downward kinetic smash (50ms)
    clip.AddFrame(nullptr, 0.05f);
    // Frame 2: Impact crater recovery (150ms)
    clip.AddFrame(nullptr, 0.15f);

    EXPECT_EQ(clip.Frames.size(), 3u);
    EXPECT_EQ(clip.frames.size(), 3u); // Legacy container remains in sync
    EXPECT_FLOAT_EQ(clip.Frames[0].DurationSeconds, 0.30f);
    EXPECT_FLOAT_EQ(clip.Frames[1].DurationSeconds, 0.05f);
    EXPECT_FLOAT_EQ(clip.Frames[2].DurationSeconds, 0.15f);
    EXPECT_FLOAT_EQ(clip.GetTotalDuration(), 0.50f);
}

TEST(AnimatorTest, Cardinal4WayFacingResolutionAcross8CompassAnglesWithHysteresis) {
    const glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };
    const glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
    bool flipX = false;

    // 1. Omni_1Way
    auto omniDir = Engine::Animator::ResolveDirection({ 1.0f, 0.0f }, camRight, camForward,
                                                      Engine::FacingDirection::None, 7.0f,
                                                      Engine::DirectionalScheme::Omni_1Way, &flipX);
    EXPECT_EQ(omniDir, Engine::FacingDirection::None);
    EXPECT_FALSE(flipX);

    // 2. Cardinal_4Way pure compass facings
    // East (1, 0)
    auto eastDir = Engine::Animator::ResolveDirection({ 1.0f, 0.0f }, camRight, camForward,
                                                      Engine::FacingDirection::None, 7.0f,
                                                      Engine::DirectionalScheme::Cardinal_4Way, &flipX);
    EXPECT_EQ(eastDir, Engine::FacingDirection::Side);
    EXPECT_FALSE(flipX);

    // West (-1, 0)
    auto westDir = Engine::Animator::ResolveDirection({ -1.0f, 0.0f }, camRight, camForward,
                                                      Engine::FacingDirection::None, 7.0f,
                                                      Engine::DirectionalScheme::Cardinal_4Way, &flipX);
    EXPECT_EQ(westDir, Engine::FacingDirection::Side);
    EXPECT_TRUE(flipX); // West sets FlipX == true!

    // North (0, -1)
    auto northDir = Engine::Animator::ResolveDirection({ 0.0f, -1.0f }, camRight, camForward,
                                                       Engine::FacingDirection::None, 7.0f,
                                                       Engine::DirectionalScheme::Cardinal_4Way, &flipX);
    EXPECT_EQ(northDir, Engine::FacingDirection::Up);
    EXPECT_FALSE(flipX);

    // South (0, 1)
    auto southDir = Engine::Animator::ResolveDirection({ 0.0f, 1.0f }, camRight, camForward,
                                                       Engine::FacingDirection::None, 7.0f,
                                                       Engine::DirectionalScheme::Cardinal_4Way, &flipX);
    EXPECT_EQ(southDir, Engine::FacingDirection::Down);
    EXPECT_FALSE(flipX);

    // 3. 7° Angular Hysteresis Deadband at 315° (South-East boundary between Down and Side)
    // Nominal threshold is 315°. With 7° deadband (±3.5°):
    // From Down (nominal [225..315]): expanded sector is [221.5° .. 318.5°].
    // From Side (nominal [315..45]): expanded sector is [311.5° .. 48.5°].
    auto makeFacingFromAngleDeg = [](float deg) {
        const float rad = glm::radians(deg);
        return glm::vec2(std::cos(rad), -std::sin(rad));
    };

    // Transitioning from Down -> Side:
    // At 314° (before boundary): remains Down
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(314.0f), camRight, camForward,
                                                 Engine::FacingDirection::Down, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Down);
    // At 315° (nominal boundary): remains Down because Down resists leaving up to 318.5°!
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(315.0f), camRight, camForward,
                                                 Engine::FacingDirection::Down, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Down);
    // At 318°: still within [221.5 .. 318.5°] -> remains Down
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(318.0f), camRight, camForward,
                                                 Engine::FacingDirection::Down, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Down);
    // At 319° (> 318.5°): cleanly switches to Side!
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(319.0f), camRight, camForward,
                                                 Engine::FacingDirection::Down, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Side);

    // Transitioning from Side -> Down:
    // At 316°: remains Side
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(316.0f), camRight, camForward,
                                                 Engine::FacingDirection::Side, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Side);
    // At 315°: remains Side because Side resists leaving down to 311.5°!
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(315.0f), camRight, camForward,
                                                 Engine::FacingDirection::Side, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Side);
    // At 312°: still within [311.5 .. 48.5°] -> remains Side
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(312.0f), camRight, camForward,
                                                 Engine::FacingDirection::Side, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Side);
    // At 311° (< 311.5°): cleanly switches to Down!
    EXPECT_EQ(Engine::Animator::ResolveDirection(makeFacingFromAngleDeg(311.0f), camRight, camForward,
                                                 Engine::FacingDirection::Side, 7.0f,
                                                 Engine::DirectionalScheme::Cardinal_4Way, &flipX),
              Engine::FacingDirection::Down);
}

TEST(AnimatorTest, FrontBack2WayResolutionAndFlipX) {
    const glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };
    const glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
    bool flipX = false;

    // South-facing (Front): (0, 1) -> screenY = -1
    auto frontDir = Engine::Animator::ResolveDirection({ 0.0f, 1.0f }, camRight, camForward,
                                                       Engine::FacingDirection::None, 7.0f,
                                                       Engine::DirectionalScheme::FrontBack_2Way, &flipX);
    EXPECT_EQ(frontDir, Engine::FacingDirection::Front);
    EXPECT_FALSE(flipX);

    // North-facing (Back): (0, -1) -> screenY = 1
    auto backDir = Engine::Animator::ResolveDirection({ 0.0f, -1.0f }, camRight, camForward,
                                                      Engine::FacingDirection::None, 7.0f,
                                                      Engine::DirectionalScheme::FrontBack_2Way, &flipX);
    EXPECT_EQ(backDir, Engine::FacingDirection::Back);
    EXPECT_FALSE(flipX);

    // South-West: (-1, 1) -> screenX = -1, screenY = -1 -> Front with FlipX = true
    auto swDir = Engine::Animator::ResolveDirection({ -1.0f, 1.0f }, camRight, camForward,
                                                    Engine::FacingDirection::None, 7.0f,
                                                    Engine::DirectionalScheme::FrontBack_2Way, &flipX);
    EXPECT_EQ(swDir, Engine::FacingDirection::Front);
    EXPECT_TRUE(flipX);

    // North-West: (-1, -1) -> screenX = -1, screenY = 1 -> Back with FlipX = true
    auto nwDir = Engine::Animator::ResolveDirection({ -1.0f, -1.0f }, camRight, camForward,
                                                    Engine::FacingDirection::None, 7.0f,
                                                    Engine::DirectionalScheme::FrontBack_2Way, &flipX);
    EXPECT_EQ(nwDir, Engine::FacingDirection::Back);
    EXPECT_TRUE(flipX);
}

TEST(AnimatorTest, PlayStateSelectsDirectionalClipsAndAppliesFlipX) {
    Engine::GameObject entity("HeroStandee");
    auto* sprite = entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    animator->SetDirectionalScheme(Engine::DirectionalScheme::Cardinal_4Way);

    auto walkDown = std::make_shared<Engine::AnimationClip>("Walk_Down", 0.1f, true);
    auto walkUp   = std::make_shared<Engine::AnimationClip>("Walk_Up", 0.1f, true);
    auto walkSide = std::make_shared<Engine::AnimationClip>("Walk_Side", 0.1f, true);

    walkDown->AddFrame(nullptr);
    walkUp->AddFrame(nullptr);
    walkSide->AddFrame(nullptr);

    animator->AddClip(walkDown);
    animator->AddClip(walkUp);
    animator->AddClip(walkSide);

    // 1. Move East (1, 0) -> Walk_Side, FlipX = false
    animator->PlayState("Walk", { 1.0f, 0.0f });
    EXPECT_EQ(animator->GetCurrentClipName(), "Walk_Side");
    EXPECT_FALSE(sprite->GetFlipX());

    // 2. Move West (-1, 0) -> Walk_Side, FlipX = true!
    animator->PlayState("Walk", { -1.0f, 0.0f });
    EXPECT_EQ(animator->GetCurrentClipName(), "Walk_Side");
    EXPECT_TRUE(sprite->GetFlipX());

    // 3. Move South (0, 1) -> Walk_Down, FlipX = false
    animator->PlayState("Walk", { 0.0f, 1.0f });
    EXPECT_EQ(animator->GetCurrentClipName(), "Walk_Down");
    EXPECT_FALSE(sprite->GetFlipX());

    // 4. Move North (0, -1) -> Walk_Up, FlipX = false
    animator->PlayState("Walk", { 0.0f, -1.0f });
    EXPECT_EQ(animator->GetCurrentClipName(), "Walk_Up");
    EXPECT_FALSE(sprite->GetFlipX());
}

TEST(AnimatorTest, PlayOneShotExecutesAndTransitionsToFallbackLoopState) {
    Engine::GameObject entity("CombatEntity");
    auto* sprite = entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    animator->SetDirectionalScheme(Engine::DirectionalScheme::Cardinal_4Way);

    auto chopSide = std::make_shared<Engine::AnimationClip>("Chop_Side", 0.10f, false);
    chopSide->AddFrame(nullptr, 0.10f); // Frame 0
    chopSide->AddFrame(nullptr, 0.10f); // Frame 1

    auto idle = std::make_shared<Engine::AnimationClip>("Idle", 0.50f, true);
    idle->AddFrame(nullptr);

    animator->AddClip(chopSide);
    animator->AddClip(idle);

    // Start in Idle
    animator->PlayState("Idle", { 1.0f, 0.0f });
    EXPECT_EQ(animator->GetCurrentClipName(), "Idle");
    EXPECT_FALSE(animator->IsOneShotPlaying());

    // Play Chop OneShot facing East
    animator->PlayOneShot("Chop", { 1.0f, 0.0f }, "Idle", 10);
    EXPECT_TRUE(animator->IsOneShotPlaying());
    EXPECT_EQ(animator->GetCurrentClipName(), "Chop_Side");
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);
    EXPECT_FALSE(sprite->GetFlipX());

    // Advance 0.05s (still on frame 0)
    animator->Update(0.05f);
    EXPECT_TRUE(animator->IsOneShotPlaying());
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // Call PlayState("Walk") during active one-shot: MUST NOT interrupt the attack!
    animator->PlayState("Walk", { 1.0f, 0.0f });
    EXPECT_TRUE(animator->IsOneShotPlaying());
    EXPECT_EQ(animator->GetCurrentClipName(), "Chop_Side");

    // Advance 0.06s (total 0.11s -> advances to frame 1)
    animator->Update(0.06f);
    EXPECT_TRUE(animator->IsOneShotPlaying());
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);

    // Advance 0.10s (total 0.21s -> final frame completes, one-shot finishes!)
    animator->Update(0.10f);
    EXPECT_FALSE(animator->IsOneShotPlaying());
    // Auto-transitioned to the queued movement state ("Walk") or fallback ("Idle")
    const std::string finalClip = animator->GetCurrentClipName();
    EXPECT_TRUE(finalClip == "Idle" || finalClip == "Walk_Side");
}

TEST(AnimatorTest, OneShotPriorityRejectionAndHigherPriorityInterruption) {
    Engine::GameObject entity("PriorityEntity");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto lightAttack = std::make_shared<Engine::AnimationClip>("LightAttack", 0.2f, false);
    lightAttack->AddFrame(nullptr);
    auto weakJab     = std::make_shared<Engine::AnimationClip>("WeakJab", 0.2f, false);
    weakJab->AddFrame(nullptr);
    auto heavyStun   = std::make_shared<Engine::AnimationClip>("HeavyStun", 0.5f, false);
    heavyStun->AddFrame(nullptr);

    animator->AddClip(lightAttack);
    animator->AddClip(weakJab);
    animator->AddClip(heavyStun);

    // 1. Start LightAttack with priority 10
    animator->PlayOneShot("LightAttack", { 1.0f, 0.0f }, "Idle", 10);
    EXPECT_TRUE(animator->IsOneShotPlaying());
    EXPECT_EQ(animator->GetCurrentClipName(), "LightAttack");
    EXPECT_EQ(animator->GetCurrentOneShotPriority(), 10);

    // 2. Attempt WeakJab with lower priority 5 -> rejected!
    animator->PlayOneShot("WeakJab", { 1.0f, 0.0f }, "Idle", 5);
    EXPECT_EQ(animator->GetCurrentClipName(), "LightAttack");
    EXPECT_EQ(animator->GetCurrentOneShotPriority(), 10);

    // 3. Trigger HeavyStun with higher priority 20 -> interrupts LightAttack!
    animator->PlayOneShot("HeavyStun", { 1.0f, 0.0f }, "Idle", 20);
    EXPECT_EQ(animator->GetCurrentClipName(), "HeavyStun");
    EXPECT_EQ(animator->GetCurrentOneShotPriority(), 20);
}

TEST(AnimatorTest, PlaybackSpeedScalesDuration) {
    Engine::GameObject entity("SpeedEntity");
    entity.AddComponent<Engine::SpriteRenderer>();
    auto* animator = entity.AddComponent<Engine::Animator>();

    auto clip = std::make_shared<Engine::AnimationClip>("Run", 0.10f, true);
    clip->AddFrame(nullptr); // Frame 0
    clip->AddFrame(nullptr); // Frame 1
    animator->AddClip(clip);

    animator->Play("Run");
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);

    // 1. Sprint Buff: 2.0x playback speed -> 0.051s deltaTime triggers frame advance (0.051 * 2 = 0.102s >= 0.10s)
    animator->SetPlaybackSpeed(2.0f);
    animator->Update(0.051f);
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);

    // 2. Tar Slowdown: 0.5x playback speed
    animator->SetPlaybackSpeed(0.5f);
    animator->Update(0.10f); // 0.10 * 0.5 = 0.05s < 0.10s -> remains on frame 1
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 1u);

    animator->Update(0.11f); // (0.05 + 0.11 * 0.5) = 0.105s >= 0.10s -> advances to frame 0 (looping)
    EXPECT_EQ(animator->GetCurrentFrameIndex(), 0u);
}

TEST(AnimatorTest, SceneSerializerSpriteRendererAndAnimatorRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    const uint64_t heroUUID = 424242ULL;
    auto* srcObj = sourceScene->CreateGameObjectWithUUID(heroUUID, "AnimatedHero");
    auto* srcSprite = srcObj->AddComponent<Engine::SpriteRenderer>();
    srcSprite->SetFlipX(true);
    srcSprite->SetFlipY(true);

    auto* srcAnim = srcObj->AddComponent<Engine::Animator>();
    srcAnim->SetDirectionalScheme(Engine::DirectionalScheme::FrontBack_2Way);
    srcAnim->SetPlaybackSpeed(1.35f);
    srcAnim->SetHysteresisDegrees(9.0f);

    auto clip = std::make_shared<Engine::AnimationClip>("Chop", 0.12f, false);
    clip->AddFrame(nullptr, 0.25f);
    clip->AddFrame(nullptr, 0.08f);
    srcAnim->AddClip(clip);
    srcAnim->Play("Chop");

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(heroUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstSprite = dstObj->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(dstSprite, nullptr);
    EXPECT_TRUE(dstSprite->GetFlipX());
    EXPECT_TRUE(dstSprite->GetFlipY());

    auto* dstAnim = dstObj->GetComponent<Engine::Animator>();
    ASSERT_NE(dstAnim, nullptr);
    EXPECT_EQ(dstAnim->GetDirectionalScheme(), Engine::DirectionalScheme::FrontBack_2Way);
    EXPECT_FLOAT_EQ(dstAnim->GetPlaybackSpeed(), 1.35f);
    EXPECT_FLOAT_EQ(dstAnim->GetHysteresisDegrees(), 9.0f);
    EXPECT_EQ(dstAnim->GetCurrentClipName(), "Chop");
}

} // namespace
