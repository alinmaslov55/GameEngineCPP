#include <gtest/gtest.h>
#include "Engine/AI/StateMachine.h"
#include "Engine/AI/SteeringSystem.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Utils/CutoutProcessor.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

TEST(HighPostModRegressionTest, StateMachineUpdateReentrantRegisterStateSafe) {
    struct Ctx { int ticks = 0; };
    Engine::StateMachine<Ctx> sm;
    sm.RegisterState(1, { nullptr, [&](Ctx& ctx, float) {
        ++ctx.ticks;
        // Force unordered_map rehash by inserting many states mid-OnUpdate
        for (int s = 10; s < 100; ++s) {
            sm.RegisterState(s, { nullptr, nullptr, nullptr });
        }
    }, nullptr });

    Ctx ctx;
    sm.SetInitialState(1, ctx);
    sm.Update(ctx, 0.016f);
    EXPECT_EQ(ctx.ticks, 1);
    EXPECT_TRUE(sm.HasState(99));
}

TEST(HighPostModRegressionTest, AnimatorDispatchEventHonorsUnsubscribeAndClipSwitch) {
    Engine::Scene scene("AnimDispatchScene");
    auto* go = scene.CreateGameObject("AnimObj");
    auto* anim = go->AddComponent<Engine::Animator>();

    auto clipA = std::make_shared<Engine::AnimationClip>("ClipA", 10.0f, true);
    clipA->AddFrame(nullptr);
    clipA->AddFrame(nullptr);
    clipA->AddEvent(0, "Hit");

    auto clipB = std::make_shared<Engine::AnimationClip>("ClipB", 10.0f, true);
    clipB->AddFrame(nullptr);

    anim->AddClip(clipA);
    anim->AddClip(clipB);

    int listener2Calls = 0;
    uint32_t h2 = 0;
    anim->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        anim->RemoveEventListener(h2);
    });
    h2 = anim->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        ++listener2Calls;
    });

    anim->Play("ClipA");
    anim->Update(0.01f);
    EXPECT_EQ(listener2Calls, 0);

    // Clip switch inside listener 1 must also suppress listener 2
    int listener3Calls = 0;
    anim->ClearEventListeners();
    anim->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        anim->Play("ClipB");
    });
    anim->AddEventListener([&](std::string_view, const Engine::AnimationEvent&) {
        ++listener3Calls;
    });
    anim->Play("ClipA");
    anim->Update(0.01f);
    EXPECT_EQ(listener3Calls, 0);
}

TEST(HighPostModRegressionTest, SteeringSystemNonFiniteParamsDoNotHangOrPoison) {
    Engine::Scene scene("SteeringNaNScene");
    Engine::SteeringParams params{};
    params.MaxSpeed = 5.0f;
    params.ObstacleWhiskerLength = std::numeric_limits<float>::infinity();
    params.AvoidanceWeight = std::numeric_limits<float>::quiet_NaN();
    params.FireAvoidanceWeight = 2.0f;
    params.SeparationWeight = std::numeric_limits<float>::quiet_NaN();

    std::array<float, Engine::SteeringSystem::SlotCount> interest{};
    std::array<float, Engine::SteeringSystem::SlotCount> danger{};
    std::array<float, Engine::SteeringSystem::SlotCount> context{};

    const glm::vec2 vel = Engine::SteeringSystem::EvaluateContextMap(
        scene, { 0.0f, 0.0f }, 0.35f, { 5.0f, 0.0f }, {}, params, nullptr, &interest, &danger, &context);
    EXPECT_TRUE(std::isfinite(vel.x));
    EXPECT_TRUE(std::isfinite(vel.y));
    EXPECT_GT(vel.x, 0.0f);

    // Non-finite agent position must return {0,0} and zero diagnostic outputs
    interest.fill(9.0f);
    danger.fill(9.0f);
    context.fill(9.0f);
    const glm::vec2 nanVel = Engine::SteeringSystem::EvaluateContextMap(
        scene, { std::numeric_limits<float>::quiet_NaN(), 0.0f }, 0.35f, { 5.0f, 0.0f }, {}, params, nullptr,
        &interest, &danger, &context);
    EXPECT_EQ(nanVel, glm::vec2(0.0f));
    EXPECT_FLOAT_EQ(interest[0], 0.0f);
    EXPECT_FLOAT_EQ(danger[0], 0.0f);
    EXPECT_FLOAT_EQ(context[0], 0.0f);
}

TEST(HighPostModRegressionTest, PhysicsUpdateRuntimeReentrancyAndNaNSettersSafe) {
    Engine::Scene scene("PhysicsReentrancyScene");
    auto* go = scene.CreateGameObject("ReenterObj");
    auto* rb = go->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* col = go->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.5f;

    class ReentrantScript final : public Engine::Component {
    public:
        Engine::Scene* targetScene = nullptr;
        int fixedCalls = 0;
        void FixedUpdate(float dt) override {
            ++fixedCalls;
            if (targetScene && fixedCalls == 1) {
                targetScene->UpdateRuntime(dt);
            }
        }
    };
    auto* script = go->AddComponent<ReentrantScript>();
    script->targetScene = &scene;

    scene.OnRuntimeStart();
    // NaN/Inf setters must be rejected without triggering Box2D asserts
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const float pinf = std::numeric_limits<float>::infinity();
    rb->ApplyImpulseXZ({ qnan, pinf });
    rb->ApplyForceXZ({ pinf, qnan });
    rb->SetVelocityXZ({ qnan, 1.0f });
    rb->SetPositionXZ({ pinf, 0.0f });
    rb->SetLinearDampingRuntime(qnan);
    rb->SetMassMultiplier(pinf);
    rb->LaunchVertical(qnan);

    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(script->fixedCalls, 1);
    scene.OnRuntimeStop();
}

TEST(HighPostModRegressionTest, GameObjectRemoveComponentByPointerRemovesExactCollider) {
    Engine::Scene scene("MultiColliderRemoveScene");
    auto* go = scene.CreateGameObject("MultiColObj");
    auto* col1 = go->AddComponent<Engine::BoxCollider2D>();
    col1->size = { 1.0f, 1.0f };
    auto* col2 = go->AddComponent<Engine::BoxCollider2D>();
    col2->size = { 3.0f, 3.0f };

    ASSERT_EQ(go->GetComponents<Engine::BoxCollider2D>().size(), 2u);
    EXPECT_TRUE(go->RemoveComponent(col2));

    const auto remaining = go->GetComponents<Engine::BoxCollider2D>();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0], col1);
    EXPECT_FLOAT_EQ(remaining[0]->size.x, 1.0f);
}

TEST(HighPostModRegressionTest, CutoutProcessorRejectsPaddingOverflow) {
    uint32_t w = 4, h = 4;
    std::vector<uint8_t> rgba(w * h * 4u, 255u);
    Engine::CutoutProcessSettings settings{};
    settings.AutoTrimAndBottomFlush = true;
    settings.HorizontalPaddingPx = std::numeric_limits<uint32_t>::max() / 2u;

    const auto res = Engine::CutoutProcessor::ProcessRGBAInPlace(rgba, w, h, settings);
    EXPECT_FALSE(res.Success);
    EXPECT_FALSE(res.ErrorMessage.empty());
}

} // namespace
