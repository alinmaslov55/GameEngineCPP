#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"

#include <cmath>
#include <vector>

namespace {

class StepCounterComponent : public Engine::Component {
public:
    uint32_t updateCount = 0;
    uint32_t fixedUpdateCount = 0;
    float accumulatedFixedDt = 0.0f;

    void Update(float deltaTime) override {
        (void)deltaTime;
        ++updateCount;
    }

    void FixedUpdate(float fixedDeltaTime) override {
        ++fixedUpdateCount;
        accumulatedFixedDt += fixedDeltaTime;
    }
};

TEST(TimeAndPhysicsStepTest, TimeScaleAndHitStopDecay) {
    Engine::Time::Init();
    Engine::Time::Reset();

    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 1.0f);
    EXPECT_FLOAT_EQ(Engine::Time::GetFixedDeltaTime(), 1.0f / 60.0f);
    EXPECT_FALSE(Engine::Time::IsInHitStop());

    // 1. Time scale modification
    Engine::Time::SetTimeScale(0.5f);
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.5f);

    // 2. Mock delta step with scaled time
    Engine::Time::SetMockDeltaTime(0.04f);
    Engine::Time::Update();
    EXPECT_FLOAT_EQ(Engine::Time::GetUnscaledDeltaTime(), 0.04f);
    EXPECT_FLOAT_EQ(Engine::Time::GetDeltaTime(), 0.02f); // 0.04 * 0.5

    // 3. Trigger Hit-Stop (freeze-frame)
    // 0.05s unscaled duration, hit scale = 0.02f
    Engine::Time::TriggerHitStop(0.05f, 0.02f);
    EXPECT_TRUE(Engine::Time::IsInHitStop());
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.02f);

    // Step 0.025s unscaled (less than 0.05s duration)
    Engine::Time::SetMockDeltaTime(0.025f);
    Engine::Time::Update();
    EXPECT_TRUE(Engine::Time::IsInHitStop());
    EXPECT_FLOAT_EQ(Engine::Time::GetUnscaledDeltaTime(), 0.025f);
    EXPECT_FLOAT_EQ(Engine::Time::GetDeltaTime(), 0.025f * 0.02f);

    // Step another 0.030s unscaled (cumulative 0.055s > 0.05s) -> hit stop must expire
    Engine::Time::SetMockDeltaTime(0.030f);
    Engine::Time::Update();
    EXPECT_FALSE(Engine::Time::IsInHitStop());
    // Prior time scale (0.5f) must be seamlessly restored!
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.5f);

    // Subsequent frame resumes running at restored base time scale (0.5f)
    Engine::Time::SetMockDeltaTime(0.020f);
    Engine::Time::Update();
    EXPECT_FALSE(Engine::Time::IsInHitStop());
    EXPECT_FLOAT_EQ(Engine::Time::GetDeltaTime(), 0.020f * 0.5f);

    Engine::Time::Reset();
}

TEST(TimeAndPhysicsStepTest, DeltaTimeClamping) {
    Engine::Time::Init();
    Engine::Time::Reset();

    // Large lag spike (0.5s) clamped to maxFrameDelta (0.10s)
    Engine::Time::SetMockDeltaTime(0.50f);
    Engine::Time::Update();
    EXPECT_FLOAT_EQ(Engine::Time::GetUnscaledDeltaTime(), 0.10f);
    EXPECT_FLOAT_EQ(Engine::Time::GetDeltaTime(), 0.10f);

    Engine::Time::Reset();
}

TEST(TimeAndPhysicsStepTest, TransformInterpolationAlpha) {
    Engine::Transform transform;
    transform.position = { 0.0f, 0.0f, 0.0f };
    transform.rotation = { 0.0f, 0.0f, 0.0f };
    transform.SnapshotPrevious();

    transform.position = { 10.0f, 20.0f, 30.0f };
    transform.rotation = { 0.0f, 0.0f, 90.0f };

    // At alpha = 0.0: matches prev
    glm::vec3 pos0 = transform.GetInterpolatedPosition(0.0f);
    EXPECT_NEAR(pos0.x, 0.0f, 1e-5f);
    EXPECT_NEAR(pos0.y, 0.0f, 1e-5f);
    EXPECT_NEAR(pos0.z, 0.0f, 1e-5f);

    // At alpha = 1.0: matches curr
    glm::vec3 pos1 = transform.GetInterpolatedPosition(1.0f);
    EXPECT_NEAR(pos1.x, 10.0f, 1e-5f);
    EXPECT_NEAR(pos1.y, 20.0f, 1e-5f);
    EXPECT_NEAR(pos1.z, 30.0f, 1e-5f);

    // At alpha = 0.5: exact midpoint
    glm::vec3 posMid = transform.GetInterpolatedPosition(0.5f);
    EXPECT_NEAR(posMid.x, 5.0f, 1e-5f);
    EXPECT_NEAR(posMid.y, 10.0f, 1e-5f);
    EXPECT_NEAR(posMid.z, 15.0f, 1e-5f);

    glm::vec3 rotMid = transform.GetInterpolatedRotation(0.5f);
    EXPECT_NEAR(rotMid.z, 45.0f, 1e-5f);

    // Interpolated matrix translation check
    glm::mat4 matrixMid = transform.GetInterpolatedTransformMatrix(0.5f);
    EXPECT_NEAR(matrixMid[3][0], 5.0f, 1e-4f);
    EXPECT_NEAR(matrixMid[3][1], 10.0f, 1e-4f);
    EXPECT_NEAR(matrixMid[3][2], 15.0f, 1e-4f);
}

TEST(TimeAndPhysicsStepTest, FixedUpdateFrequency) {
    Engine::Scene scene("FrequencyScene");
    auto* obj = scene.CreateGameObject("Counter");
    auto* counter = obj->AddComponent<StepCounterComponent>();

    scene.OnRuntimeStart();

    // Simulate 120 FPS for 1.0 second (120 calls of dt = 1/120s)
    constexpr float dt = 1.0f / 120.0f;
    for (int frame = 0; frame < 120; ++frame) {
        scene.UpdateRuntime(dt);
    }

    scene.OnRuntimeStop();

    // 120 variable updates
    EXPECT_EQ(counter->updateCount, 120u);
    // At default 60Hz fixed rate (1/60s), exactly 60 fixed updates
    EXPECT_EQ(counter->fixedUpdateCount, 60u);
    EXPECT_NEAR(counter->accumulatedFixedDt, 1.0f, 1e-3f);
}

TEST(TimeAndPhysicsStepTest, AccumulatorSpiralOfDeathClamping) {
    Engine::Scene scene("SpiralClampingScene");
    auto* obj = scene.CreateGameObject("ClampedCounter");
    auto* counter = obj->AddComponent<StepCounterComponent>();

    scene.OnRuntimeStart();

    // A massive 2.0s hitch in a single frame
    scene.UpdateRuntime(2.0f);

    scene.OnRuntimeStop();

    // Max allowed steps per frame is clamped to 6 to prevent spiral-of-death freeze
    EXPECT_EQ(counter->fixedUpdateCount, 6u);
    EXPECT_EQ(counter->updateCount, 1u);
}

TEST(TimeAndPhysicsStepTest, MultiFrameratePhysicsDeterminism) {
    // Run 3 independent simulations for 1.0 second of physical fall / launch under Box2D:
    // Case A: 30 FPS  (30 frames, dt = 1/30)
    // Case B: 60 FPS  (60 frames, dt = 1/60)
    // Case C: 240 FPS (240 frames, dt = 1/240)

    auto runSimulation = [](int fps) -> glm::vec3 {
        Engine::Scene scene("DeterminismScene");
        auto* bodyObj = scene.CreateGameObject("SimBody");
        auto* transform = bodyObj->GetTransform();
        transform->position = { 0.0f, 50.0f, 0.0f };

        auto* rb2d = bodyObj->AddComponent<Engine::Rigidbody2D>();
        rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;

        auto* col = bodyObj->AddComponent<Engine::BoxCollider2D>();
        col->size = { 1.0f, 1.0f };
        col->density = 1.0f;

        scene.OnRuntimeStart();

        const float dt = 1.0f / static_cast<float>(fps);
        for (int i = 0; i < fps; ++i) {
            scene.UpdateRuntime(dt);
        }

        glm::vec3 finalPos = transform->position;
        scene.OnRuntimeStop();
        return finalPos;
    };

    glm::vec3 pos30  = runSimulation(30);
    glm::vec3 pos60  = runSimulation(60);
    glm::vec3 pos240 = runSimulation(240);

    // Verify gravity pulled the object downwards
    EXPECT_LT(pos60.y, 50.0f);

    // Verify trajectory determinism: final positions must match within < 1e-4m
    EXPECT_NEAR(pos30.y, pos60.y, 1e-4f);
    EXPECT_NEAR(pos60.y, pos240.y, 1e-4f);
    EXPECT_NEAR(pos30.x, pos60.x, 1e-4f);
    EXPECT_NEAR(pos60.x, pos240.x, 1e-4f);
}

} // namespace
