#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include <box2d/box2d.h>
#include <thread>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

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

TEST_F(RigidbodyLifecycleTest, PhysicsWorldAndComponentsEnforceMainThreadAffinity) {
    Engine::Scene scene("PhysicsThreadAffinityScene");
    scene.OnRuntimeStart();

    auto* go = scene.CreateGameObject("PhysicsActor");
    auto* rb2d = go->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* col = go->AddComponent<Engine::BoxCollider2D>();

    EXPECT_TRUE(scene.GetPhysicsWorld().IsOnOwnerThread());
    scene.GetPhysicsWorld().MarkPhysicsDirty(go);
    rb2d->ApplyImpulseXZ({ 1.0f, 0.0f });
    col->SetMaskBits(Engine::PhysicsLayer::Environment);

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST)
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    auto suppressCrtDialogs = []() {
#ifdef _MSC_VER
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    };
    EXPECT_DEATH(
        {
            suppressCrtDialogs();
            std::thread worker([&]() {
                scene.GetPhysicsWorld().MarkPhysicsDirty(go);
            });
            worker.join();
        },
        "Main-thread affinity violation");
    EXPECT_DEATH(
        {
            suppressCrtDialogs();
            std::thread worker([&]() {
                rb2d->ApplyImpulseXZ({ 2.0f, 0.0f });
            });
            worker.join();
        },
        "Main-thread affinity violation");
    EXPECT_DEATH(
        {
            suppressCrtDialogs();
            std::thread worker([&]() {
                col->SetMaskBits(Engine::PhysicsLayer::All);
            });
            worker.join();
        },
        "Main-thread affinity violation");
#endif

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, RemoveRigidbodyComponent_NoBodyLeakWithAndWithoutColliders) {
    Engine::Scene scene("RemoveRigidbodyLeakScene");
    scene.OnRuntimeStart();

    b2WorldId worldId = b2LoadWorldId(scene.GetPhysicsWorld().GetPhysicsWorldId());
    ASSERT_TRUE(b2World_IsValid(worldId));
    const int baseBodies = b2World_GetCounters(worldId).bodyCount;

    // 1. Collider-less GameObject with Rigidbody2D: removing Rigidbody2D must destroy the b2BodyId immediately
    auto* bare = scene.CreateGameObject("BareRB");
    bare->AddComponent<Engine::Rigidbody2D>();
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies + 1);
    EXPECT_EQ(scene.GetPhysicsWorld().GetRegisteredPhysicsBodyCount(), 1u);

    EXPECT_TRUE(bare->RemoveComponent<Engine::Rigidbody2D>());
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies);
    EXPECT_EQ(scene.GetPhysicsWorld().GetRegisteredPhysicsBodyCount(), 0u);

    // 2. GameObject with Rigidbody2D + BoxCollider2D: removing Rigidbody2D must destroy old body & recreate single standalone static body
    auto* withCol = scene.CreateGameObject("RBAndCollider");
    withCol->AddComponent<Engine::Rigidbody2D>();
    auto* box = withCol->AddComponent<Engine::BoxCollider2D>();
    box->size = { 1.0f, 1.0f };
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies + 1);

    EXPECT_TRUE(withCol->RemoveComponent<Engine::Rigidbody2D>());
    EXPECT_EQ(scene.GetPhysicsWorld().GetRegisteredPhysicsBodyCount(), 0u);
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies + 1);
    ASSERT_TRUE(box->RuntimeBody.IsValid());
    EXPECT_EQ(b2Body_GetType(b2LoadBodyId(box->RuntimeBody.Value)), b2_staticBody);

    // 3. Deferred removal during component iteration must also not leak
    class SelfRemoveRigidbodyInCollision : public Engine::Component {
    public:
        bool Removed = false;
        void OnCollisionEnter2D(const Engine::Collision2DInfo&) override {
            if (!Removed) {
                Removed = GetGameObject()->RemoveComponent<Engine::Rigidbody2D>();
            }
        }
    };

    auto* deferredGo = scene.CreateGameObject("DeferredRemoveRB");
    deferredGo->AddComponent<Engine::Rigidbody2D>();
    auto* triggerHook = deferredGo->AddComponent<SelfRemoveRigidbodyInCollision>();
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies + 2);
    deferredGo->DispatchCollisionEnter2D(Engine::Collision2DInfo{});
    EXPECT_TRUE(triggerHook->Removed);
    EXPECT_EQ(b2World_GetCounters(worldId).bodyCount, baseBodies + 1);

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, ColliderDisableAndDestroy_PreservesSharedRigidbodyAndSiblingColliders) {
    Engine::Scene scene("ColliderDisablePreserveScene");
    scene.OnRuntimeStart();

    // 1. Dynamic Rigidbody2D + two Collider2Ds: disabling one collider detaches its shape without freezing Rigidbody2D
    auto* actor = scene.CreateGameObject("Actor");
    auto* rb = actor->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    auto* box = actor->AddComponent<Engine::BoxCollider2D>();
    box->size = { 1.0f, 1.0f };
    auto* circle = actor->AddComponent<Engine::CircleCollider2D>();
    circle->Radius = 0.5f;

    b2BodyId actorBody = b2LoadBodyId(rb->RuntimeBody.Value);
    ASSERT_TRUE(b2Body_IsValid(actorBody));
    ASSERT_TRUE(box->RuntimeShape.IsValid());
    ASSERT_TRUE(circle->RuntimeShape.IsValid());

    box->SetEnabled(false);
    EXPECT_FALSE(box->RuntimeShape.IsValid());
    EXPECT_TRUE(circle->RuntimeShape.IsValid());
    EXPECT_TRUE(b2Body_IsEnabled(actorBody));

    rb->SetVelocityXZ({ 5.0f, 0.0f });
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_GT(actor->GetTransform()->position.x, 0.05f);

    box->SetEnabled(true);
    EXPECT_TRUE(box->RuntimeShape.IsValid());
    EXPECT_TRUE(b2Body_IsEnabled(actorBody));

    // 2. Standalone multi-collider GameObject: disabling one keeps shared static body enabled; disabling both disables it
    auto* prop = scene.CreateGameObject("Prop");
    auto* propBox = prop->AddComponent<Engine::BoxCollider2D>();
    auto* propCircle = prop->AddComponent<Engine::CircleCollider2D>();
    b2BodyId propBody = b2LoadBodyId(propBox->RuntimeBody.Value);
    ASSERT_TRUE(b2Body_IsValid(propBody));
    EXPECT_TRUE(b2Body_IsEnabled(propBody));

    propBox->SetEnabled(false);
    EXPECT_FALSE(propBox->RuntimeShape.IsValid());
    EXPECT_TRUE(propCircle->RuntimeShape.IsValid());
    EXPECT_TRUE(b2Body_IsEnabled(propBody));

    propCircle->SetEnabled(false);
    EXPECT_FALSE(propCircle->RuntimeShape.IsValid());
    EXPECT_FALSE(b2Body_IsEnabled(propBody));

    propBox->SetEnabled(true);
    EXPECT_TRUE(propBox->RuntimeShape.IsValid());
    EXPECT_TRUE(b2Body_IsEnabled(propBody));

    // 3. DestroyGameObject disables standalone Collider2D & Tilemap bodies during deferred destruction and destroys them on flush
    auto* tmGo = scene.CreateGameObject("TilemapObj");
    auto* tm = tmGo->AddComponent<Engine::Tilemap>(2, 2);
    tm->SetTile(0, 0, 1, false);
    scene.MarkPhysicsDirty(tmGo);
    scene.UpdateRuntime(1.0f / 60.0f);
    ASSERT_TRUE(tm->RuntimeBody.IsValid());
    b2BodyId tmBody = b2LoadBodyId(tm->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsEnabled(tmBody));

    scene.BeginIteration();
    scene.DestroyGameObject(prop);
    scene.DestroyGameObject(tmGo);
    EXPECT_FALSE(b2Body_IsEnabled(propBody));
    EXPECT_FALSE(b2Body_IsEnabled(tmBody));
    scene.EndIteration();
    EXPECT_FALSE(b2Body_IsValid(propBody));
    EXPECT_FALSE(b2Body_IsValid(tmBody));

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, DegenerateCapsuleAndBoxAsymmetricResize_UpdateCleanly) {
    Engine::Scene scene("ShapeResizeEdgeCaseScene");
    scene.OnRuntimeStart();

    // 1. Degenerate CapsuleCollider2D (Center1 == Center2) uses circle shape and resizes without Box2D assertion
    auto* capGo = scene.CreateGameObject("DegenerateCapsule");
    capGo->AddComponent<Engine::Rigidbody2D>();
    auto* cap = capGo->AddComponent<Engine::CapsuleCollider2D>(0.5f, glm::vec2{ 0.0f, 0.0f }, glm::vec2{ 0.0f, 0.0f });

    ASSERT_TRUE(cap->RuntimeShape.IsValid());
    EXPECT_EQ(b2Shape_GetType(b2LoadShapeId(cap->RuntimeShape.Value)), b2_circleShape);

    cap->Radius = 0.85f;
    scene.MarkPhysicsDirty(capGo);
    scene.UpdateRuntime(1.0f / 60.0f);
    ASSERT_TRUE(cap->RuntimeShape.IsValid());
    EXPECT_EQ(b2Shape_GetType(b2LoadShapeId(cap->RuntimeShape.Value)), b2_circleShape);
    EXPECT_NEAR(b2Shape_GetCircle(b2LoadShapeId(cap->RuntimeShape.Value)).radius, 0.85f, 1e-4f);

    // Expand capsule endpoints -> transitions to capsule shape; collapse -> transitions back to circle shape
    cap->Center2 = { 0.0f, 1.0f };
    scene.MarkPhysicsDirty(capGo);
    scene.UpdateRuntime(1.0f / 60.0f);
    ASSERT_TRUE(cap->RuntimeShape.IsValid());
    EXPECT_EQ(b2Shape_GetType(b2LoadShapeId(cap->RuntimeShape.Value)), b2_capsuleShape);

    cap->Center2 = { 0.0f, 0.0f };
    scene.MarkPhysicsDirty(capGo);
    scene.UpdateRuntime(1.0f / 60.0f);
    ASSERT_TRUE(cap->RuntimeShape.IsValid());
    EXPECT_EQ(b2Shape_GetType(b2LoadShapeId(cap->RuntimeShape.Value)), b2_circleShape);

    // 2. BoxCollider2D resize keeping vertices[0] constant while shifting vertices[2]
    auto* boxGo = scene.CreateGameObject("AsymmetricBox");
    boxGo->AddComponent<Engine::Rigidbody2D>();
    auto* box = boxGo->AddComponent<Engine::BoxCollider2D>();
    box->size = { 1.0f, 1.0f };
    box->offset = { 0.0f, 0.0f };
    ASSERT_TRUE(box->RuntimeShape.IsValid());

    // Shift offset & size so vertices[0].x remains -0.5f while vertices[2].x moves from +0.5f to +2.5f
    box->size = { 3.0f, 1.0f };
    box->offset = { 1.0f, 0.0f };
    scene.MarkPhysicsDirty(boxGo);
    scene.UpdateRuntime(1.0f / 60.0f);

    b2Polygon poly = b2Shape_GetPolygon(b2LoadShapeId(box->RuntimeShape.Value));
    float maxX = poly.vertices[0].x;
    for (int i = 1; i < poly.count; ++i) {
        maxX = std::max(maxX, poly.vertices[i].x);
    }
    EXPECT_NEAR(maxX, 2.5f, 1e-3f);

    scene.OnRuntimeStop();
}

TEST_F(RigidbodyLifecycleTest, NonRuntimeStaticColliderSwapSameCount_RefreshesCacheWithoutUAF) {
    Engine::Scene scene("NonRuntimeStaticSwapScene");

    auto* dynGo = scene.CreateGameObject("Dyn");
    dynGo->GetTransform()->position = { 5.0f, 0.0f, 0.0f };
    auto* dynRb = dynGo->AddComponent<Engine::Rigidbody2D>();
    dynRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    dynRb->UseGravityY = false;
    dynGo->AddComponent<Engine::BoxCollider2D>()->size = { 1.0f, 1.0f };

    auto* wallA = scene.CreateGameObject("WallA");
    wallA->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    wallA->AddComponent<Engine::BoxCollider2D>()->size = { 1.0f, 1.0f };

    scene.GetPhysicsWorld().UpdateNonRuntime(1.0f / 60.0f);
    EXPECT_EQ(scene.GetPhysicsWorld().GetStaticColliderCount(), 1u);

    // Destroy WallA and spawn WallB right at Dyn's position so staticCount stays 1
    scene.DestroyGameObject(wallA);
    scene.EndIteration();
    auto* wallB = scene.CreateGameObject("WallB");
    wallB->GetTransform()->position = { 5.2f, 0.0f, 0.0f };
    wallB->AddComponent<Engine::BoxCollider2D>()->size = { 1.0f, 1.0f };

    scene.GetPhysicsWorld().UpdateNonRuntime(1.0f / 60.0f);
    EXPECT_EQ(scene.GetPhysicsWorld().GetStaticColliderCount(), 1u);
    // Dyn at 5.0 overlapping WallB at 5.2 should be pushed away along X (< 4.9)
    EXPECT_LT(dynGo->GetTransform()->position.x, 4.9f);
}
