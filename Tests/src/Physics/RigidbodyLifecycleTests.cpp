#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include <box2d/box2d.h>

class RigidbodyLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(RigidbodyLifecycleTest, MidRuntimeSpawnDynamicRigidbody) {
    Engine::Scene scene("MidRuntimeSpawnScene");
    scene.OnRuntimeStart();

    // Spawn a dynamic GameObject mid-runtime
    auto* go = scene.CreateGameObject("MidRuntimeDynamic");
    ASSERT_NE(go, nullptr);
    go->GetTransform()->position = { 5.0f, 0.0f, 10.0f };

    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    ASSERT_NE(rb2d, nullptr);
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;

    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    ASSERT_NE(bc2d, nullptr);
    bc2d->size = { 1.0f, 1.0f };
    bc2d->density = 1.0f;

    // Body and fixture must be immediately created mid-runtime
    EXPECT_NE(rb2d->RuntimeBody, nullptr);
    EXPECT_NE(bc2d->RuntimeFixture, nullptr);

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsValid(bodyId));

    // Verify O(1) reverse lookup via b2Body_GetUserData
    void* userData = b2Body_GetUserData(bodyId);
    EXPECT_EQ(userData, static_cast<void*>(go));

    // Verify position initialized correctly
    b2Vec2 pos = b2Body_GetPosition(bodyId);
    EXPECT_FLOAT_EQ(pos.x, 5.0f);
    EXPECT_FLOAT_EQ(pos.y, 10.0f);

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, ApplyImpulseAndLinearDampingDeceleration) {
    Engine::Scene scene("DecelerationScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("BowlingTarget");
    go->GetTransform()->position = { 0.0f, 0.0f, 0.0f };

    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 5.0f;
    rb2d->SetLinearDampingRuntime(5.0f);

    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 0.8f, 0.8f };
    bc2d->density = 1.0f;

    // Apply high-speed impulse along +X
    rb2d->ApplyImpulseXZ({ 25.0f, 0.0f });

    glm::vec2 initialVel = rb2d->GetVelocityXZ();
    EXPECT_GT(initialVel.x, 15.0f);
    EXPECT_NEAR(initialVel.y, 0.0f, 0.01f);

    float prevVelX = initialVel.x;

    // Step 60 frames (1 second total at 60 Hz)
    for (int frame = 0; frame < 60; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
        glm::vec2 currentVel = rb2d->GetVelocityXZ();

        // Velocity should monotonically decelerate under linear damping
        if (frame % 10 == 0 && frame > 0) {
            EXPECT_LT(currentVel.x, prevVelX);
            prevVelX = currentVel.x;
        }
    }

    // After 60 frames with damping 5.0, body must have decelerated smoothly to near rest
    glm::vec2 finalVel = rb2d->GetVelocityXZ();
    EXPECT_LT(finalVel.x, 0.5f);

    // Body must have moved forward along X
    EXPECT_GT(go->GetTransform()->position.x, 2.0f);

    // Verify destruction cleanup: b2Body_IsValid == false upon DestroyGameObject
    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsValid(bodyId));

    scene.DestroyGameObject(go);

    EXPECT_FALSE(b2Body_IsValid(bodyId));

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, ApplyForceXZConstantAcceleration) {
    Engine::Scene scene("ForceScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("AcceleratingBody");
    go->GetTransform()->position = { 0.0f, 0.0f, 0.0f };

    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->LinearDamping = 0.0f;
    rb2d->SetLinearDampingRuntime(0.0f);

    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 1.0f, 1.0f };
    bc2d->density = 1.0f;

    float prevVelZ = 0.0f;
    for (int step = 0; step < 15; ++step) {
        rb2d->ApplyForceXZ({ 0.0f, 20.0f });
        scene.UpdateRuntime(1.0f / 60.0f);

        glm::vec2 vel = rb2d->GetVelocityXZ();
        EXPECT_GT(vel.y, prevVelZ);
        prevVelZ = vel.y;
    }

    EXPECT_GT(go->GetTransform()->position.z, 0.1f);

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, SetVelocityXZAndPositionXZDirectManipulation) {
    Engine::Scene scene("VelocityPositionScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("DirectManip");
    go->GetTransform()->position = { 1.0f, 0.0f, 1.0f };

    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 1.0f, 1.0f };

    // Set velocity directly
    rb2d->SetVelocityXZ({ 12.0f, -8.0f });
    glm::vec2 vel = rb2d->GetVelocityXZ();
    EXPECT_FLOAT_EQ(vel.x, 12.0f);
    EXPECT_FLOAT_EQ(vel.y, -8.0f);

    // Set position directly
    rb2d->SetPositionXZ({ 35.0f, 42.0f });
    EXPECT_FLOAT_EQ(go->GetTransform()->position.x, 35.0f);
    EXPECT_FLOAT_EQ(go->GetTransform()->position.z, 42.0f);

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    b2Vec2 pos = b2Body_GetPosition(bodyId);
    EXPECT_FLOAT_EQ(pos.x, 35.0f);
    EXPECT_FLOAT_EQ(pos.y, 42.0f);

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, SetActiveEnablesAndDisablesBox2DBody) {
    Engine::Scene scene("ActiveToggleScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("ToggleEntity");
    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 1.0f, 1.0f };

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsValid(bodyId));
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    // Deactivate GameObject
    go->SetActive(false);
    EXPECT_FALSE(b2Body_IsEnabled(bodyId));

    // Reactivate GameObject
    go->SetActive(true);
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, DestroyGameObjectMidRuntimeCleansUpBody) {
    Engine::Scene scene("DestroyCleanupScene");
    scene.OnRuntimeStart();

    auto* go1 = scene.CreateGameObject("Dynamic1");
    auto* rb1 = go1->AddComponent<Engine::Rigidbody2D>();
    auto* col1 = go1->AddComponent<Engine::BoxCollider2D>();
    (void)col1;

    auto* go2 = scene.CreateGameObject("StaticProp");
    auto* col2 = go2->AddComponent<Engine::BoxCollider2D>();
    (void)col2;

    b2BodyId body1 = b2LoadBodyId(rb1->RuntimeBody.Value);
    b2BodyId body2 = b2LoadBodyId(col2->RuntimeBody.Value);

    EXPECT_TRUE(b2Body_IsValid(body1));
    EXPECT_TRUE(b2Body_IsValid(body2));

    // Test explicit UnregisterRuntimeBody while entity is alive: body is destroyed and handle zeroed
    scene.UnregisterRuntimeBody(go1);
    EXPECT_FALSE(b2Body_IsValid(body1));
    EXPECT_EQ(rb1->RuntimeBody, nullptr);

    // Test DestroyGameObject cleans up remaining body
    scene.DestroyGameObject(go2);
    EXPECT_FALSE(b2Body_IsValid(body2));

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, MassMultiplierAndBulletCCD) {
    Engine::Scene scene("MassBulletScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("HeavyBullet");
    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    auto* bc2d = go->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 2.0f, 2.0f };
    bc2d->density = 2.0f;

    float baseMass = rb2d->GetMass();
    EXPECT_GT(baseMass, 0.0f);

    // Scale mass by multiplier
    rb2d->SetMassMultiplier(3.0f);
    EXPECT_FLOAT_EQ(rb2d->GetMassMultiplier(), 3.0f);
    EXPECT_NEAR(rb2d->GetMass(), baseMass * 3.0f, 0.05f);

    // Configure bullet continuous collision detection
    EXPECT_FALSE(rb2d->GetBullet());
    rb2d->SetBullet(true);
    EXPECT_TRUE(rb2d->GetBullet());

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsBullet(bodyId));

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, MassMultiplierRemainsConstantAcross120FramesAndVelocityDecaysIdentically) {
    Engine::Scene scene("MassMultiplierStabilityScene");

    // 1. Body scaled via MassMultiplier = 2.5f (base shape 1x1 @ density 1.0 -> base mass 1.0 kg, scaled 2.5 kg)
    auto* goScaled = scene.CreateGameObject("ScaledBody");
    goScaled->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbScaled = goScaled->AddComponent<Engine::Rigidbody2D>();
    rbScaled->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbScaled->FixedRotation = true;
    rbScaled->MassMultiplier = 2.5f;
    rbScaled->LinearDamping = 3.0f;
    auto* bcScaled = goScaled->AddComponent<Engine::BoxCollider2D>();
    bcScaled->size = { 1.0f, 1.0f };
    bcScaled->density = 1.0f;

    // 2. Reference body with MassMultiplier = 1.0f and unscaled shape density = 2.5f (exact 2.5 kg mass)
    auto* goRef = scene.CreateGameObject("ReferenceBody");
    goRef->GetTransform()->position = { 0.0f, 0.0f, 10.0f };
    auto* rbRef = goRef->AddComponent<Engine::Rigidbody2D>();
    rbRef->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbRef->FixedRotation = true;
    rbRef->MassMultiplier = 1.0f;
    rbRef->LinearDamping = 3.0f;
    auto* bcRef = goRef->AddComponent<Engine::BoxCollider2D>();
    bcRef->size = { 1.0f, 1.0f };
    bcRef->density = 2.5f;

    scene.OnRuntimeStart();

    EXPECT_EQ(scene.GetPendingPhysicsSyncCount(), 0u);
    EXPECT_NEAR(rbScaled->GetMass(), 2.5f, 1e-4f);
    EXPECT_NEAR(rbRef->GetMass(), 2.5f, 1e-4f);

    // Apply identical impulse (25 N*s -> 10 m/s initial velocity for 2.5 kg mass)
    rbScaled->ApplyImpulseXZ({ 25.0f, 0.0f });
    rbRef->ApplyImpulseXZ({ 25.0f, 0.0f });

    EXPECT_NEAR(rbScaled->GetVelocityXZ().x, 10.0f, 1e-3f);
    EXPECT_NEAR(rbRef->GetVelocityXZ().x, 10.0f, 1e-3f);

    // Also verify calling RegisterRuntimeBody redundantly is idempotent and does not compound mass
    scene.RegisterRuntimeBody(goScaled);
    EXPECT_NEAR(rbScaled->GetMass(), 2.5f, 1e-4f);

    // Step 120 frames (2.0 seconds at 60 Hz)
    for (int frame = 0; frame < 120; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);

        // Pending sync queue must remain empty during steady-state simulation
        EXPECT_EQ(scene.GetPendingPhysicsSyncCount(), 0u);

        // Mass must stay strictly constant at 2.5f (no exponential compounding)
        EXPECT_NEAR(rbScaled->GetMass(), 2.5f, 1e-4f);
        EXPECT_NEAR(rbRef->GetMass(), 2.5f, 1e-4f);

        // Velocity decay must match reference body identically
        const glm::vec2 velScaled = rbScaled->GetVelocityXZ();
        const glm::vec2 velRef = rbRef->GetVelocityXZ();
        EXPECT_NEAR(velScaled.x, velRef.x, 1e-4f);
        EXPECT_NEAR(velScaled.y, velRef.y, 1e-4f);
    }

    EXPECT_NEAR( goScaled->GetTransform()->position.x, goRef->GetTransform()->position.x, 1e-4f );
    scene.OnRuntimeStop();
}
