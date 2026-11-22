#include <gtest/gtest.h>
#include "Engine/AI/StateMachine.h"
#include <string>
#include <vector>

namespace {

enum class MonsterState {
    Chase = 0,
    Windup = 1,
    Leap = 2,
    Stunned = 3
};

struct MonsterAIContext {
    int Health = 100;
    float ChargeTime = 0.0f;
    std::vector<std::string> TraceLog;
};

TEST(StateMachineTest, StateRegistrationAndLifecycleHooks) {
    Engine::StateMachine<MonsterAIContext> fsm;
    MonsterAIContext ctx;

    fsm.RegisterState(MonsterState::Chase, {
        .OnEnter = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Chase:Enter"); },
        .OnUpdate = [](MonsterAIContext& c, float dt) { c.TraceLog.emplace_back("Chase:Update:" + std::to_string(dt)); },
        .OnExit = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Chase:Exit"); }
    });

    fsm.RegisterState(MonsterState::Windup, {
        .OnEnter = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Windup:Enter"); },
        .OnUpdate = [](MonsterAIContext& c, float dt) { c.ChargeTime += dt; },
        .OnExit = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Windup:Exit"); }
    });

    EXPECT_EQ(fsm.GetRegisteredStateCount(), 2u);
    EXPECT_FALSE(fsm.HasActiveState());

    // Initial transition into Chase
    fsm.SetInitialState(MonsterState::Chase, ctx);
    EXPECT_TRUE(fsm.HasActiveState());
    EXPECT_TRUE(fsm.IsInState(MonsterState::Chase));
    EXPECT_FLOAT_EQ(fsm.GetStateElapsedTime(), 0.0f);

    ASSERT_EQ(ctx.TraceLog.size(), 1u);
    EXPECT_EQ(ctx.TraceLog[0], "Chase:Enter");

    // Update Chase state
    fsm.Update(ctx, 0.016f);
    EXPECT_FLOAT_EQ(fsm.GetStateElapsedTime(), 0.016f);
    ASSERT_EQ(ctx.TraceLog.size(), 2u);
    EXPECT_EQ(ctx.TraceLog[1], "Chase:Update:0.016000");

    // Transition Chase -> Windup
    fsm.TransitionTo(MonsterState::Windup, ctx);
    EXPECT_TRUE(fsm.IsInState(MonsterState::Windup));
    EXPECT_EQ(fsm.GetPreviousState(), static_cast<int>(MonsterState::Chase));
    EXPECT_FLOAT_EQ(fsm.GetStateElapsedTime(), 0.0f);

    ASSERT_EQ(ctx.TraceLog.size(), 4u);
    EXPECT_EQ(ctx.TraceLog[2], "Chase:Exit");
    EXPECT_EQ(ctx.TraceLog[3], "Windup:Enter");

    // Update Windup
    fsm.Update(ctx, 0.25f);
    EXPECT_FLOAT_EQ(ctx.ChargeTime, 0.25f);
    EXPECT_FLOAT_EQ(fsm.GetStateElapsedTime(), 0.25f);
}

TEST(StateMachineTest, ReentrantTransitionInUpdate) {
    Engine::StateMachine<MonsterAIContext> fsm;
    MonsterAIContext ctx;

    fsm.RegisterState(MonsterState::Windup, {
        .OnEnter = [](MonsterAIContext& c) { c.ChargeTime = 0.0f; },
        .OnUpdate = [&fsm](MonsterAIContext& c, float dt) {
            c.ChargeTime += dt;
            if (c.ChargeTime >= 0.20f) {
                fsm.TransitionTo(MonsterState::Leap, c);
            }
        },
        .OnExit = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Windup:Finished"); }
    });

    fsm.RegisterState(MonsterState::Leap, {
        .OnEnter = [](MonsterAIContext& c) { c.TraceLog.emplace_back("Leap:Launch"); },
        .OnUpdate = nullptr,
        .OnExit = nullptr
    });

    fsm.SetInitialState(MonsterState::Windup, ctx);

    // Step 1: 0.10s (ChargeTime = 0.10s, stays in Windup)
    fsm.Update(ctx, 0.10f);
    EXPECT_TRUE(fsm.IsInState(MonsterState::Windup));

    // Step 2: 0.15s (ChargeTime = 0.25s >= 0.20s -> triggers transition to Leap)
    fsm.Update(ctx, 0.15f);
    EXPECT_TRUE(fsm.IsInState(MonsterState::Leap));
    ASSERT_EQ(ctx.TraceLog.size(), 2u);
    EXPECT_EQ(ctx.TraceLog[0], "Windup:Finished");
    EXPECT_EQ(ctx.TraceLog[1], "Leap:Launch");
}

} // namespace
