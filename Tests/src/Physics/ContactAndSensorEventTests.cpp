#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Scene/SceneSerializer.h"
#include <box2d/box2d.h>

namespace {

class TestEventReceiver : public Engine::Component {
public:
    int CollisionEnterCount = 0;
    int CollisionExitCount = 0;
    int TriggerEnterCount = 0;
    int TriggerExitCount = 0;

    Engine::Collision2DInfo LastCollisionInfo;
    Engine::GameObject* LastCollisionOther = nullptr;
    Engine::GameObject* LastTriggerEnterOther = nullptr;
    Engine::GameObject* LastTriggerExitOther = nullptr;

    bool DestroySelfOnCollision = false;

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        ++CollisionEnterCount;
        LastCollisionInfo = info;
        LastCollisionOther = info.Other;

        if (DestroySelfOnCollision && GetGameObject() && GetGameObject()->GetScene()) {
            GetGameObject()->GetScene()->DestroyGameObject(GetGameObject());
        }
    }

    void OnCollisionExit2D(Engine::GameObject* other) override {
        ++CollisionExitCount;
        LastCollisionOther = other;
    }

    void OnTriggerEnter2D(Engine::GameObject* other) override {
        ++TriggerEnterCount;
        LastTriggerEnterOther = other;
    }

    void OnTriggerExit2D(Engine::GameObject* other) override {
        ++TriggerExitCount;
        LastTriggerExitOther = other;
    }
};

} // namespace

class ContactAndSensorEventTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 1. Launch Enemy A (CircleCollider2D) via ApplyImpulseXZ into Enemy B; assert OnCollisionEnter2D with ApproachSpeed > 5.0f
TEST_F(ContactAndSensorEventTest, CircleVsCircleDynamicCollisionAndHitEvent) {
    Engine::Scene scene("CircleVsCircleScene");
    scene.OnRuntimeStart();

    // Enemy A
    auto* enemyA = scene.CreateGameObject("EnemyA");
    enemyA->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbA = enemyA->AddComponent<Engine::Rigidbody2D>();
    rbA->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbA->FixedRotation = true;
    rbA->LinearDamping = 0.0f;
    auto* colA = enemyA->AddComponent<Engine::CircleCollider2D>();
    colA->Radius = 0.5f;
    colA->Density = 1.0f;
    colA->EnableHitEvents = true;
    auto* receiverA = enemyA->AddComponent<TestEventReceiver>();

    // Enemy B
    auto* enemyB = scene.CreateGameObject("EnemyB");
    enemyB->GetTransform()->position = { 3.0f, 0.0f, 0.0f };
    auto* rbB = enemyB->AddComponent<Engine::Rigidbody2D>();
    rbB->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbB->FixedRotation = true;
    rbB->LinearDamping = 0.0f;
    auto* colB = enemyB->AddComponent<Engine::CircleCollider2D>();
    colB->Radius = 0.5f;
    colB->Density = 1.0f;
    colB->EnableHitEvents = true;
    auto* receiverB = enemyB->AddComponent<TestEventReceiver>();

    // Launch Enemy A directly towards Enemy B along +X with 15 m/s
    rbA->SetVelocityXZ({ 15.0f, 0.0f });

    // Step simulation until collision
    bool collided = false;
    for (int frame = 0; frame < 30; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
        if (receiverA->CollisionEnterCount > 0 && receiverB->CollisionEnterCount > 0) {
            collided = true;
            break;
        }
    }

    ASSERT_TRUE(collided);
    EXPECT_EQ(receiverA->CollisionEnterCount, 1);
    EXPECT_EQ(receiverB->CollisionEnterCount, 1);

    EXPECT_EQ(receiverA->LastCollisionOther, enemyB);
    EXPECT_EQ(receiverB->LastCollisionOther, enemyA);

    EXPECT_GT(receiverA->LastCollisionInfo.ApproachSpeed, 5.0f);
    EXPECT_GT(receiverA->LastCollisionInfo.EffectiveImpulse, 0.0f);

    // Normal on A points towards B (+X), normal on B points towards A (-X)
    EXPECT_GT(receiverA->LastCollisionInfo.NormalXZ.x, 0.5f);
    EXPECT_LT(receiverB->LastCollisionInfo.NormalXZ.x, -0.5f);

    scene.OnRuntimeStop();
}

// 2. Dynamic Circle Vs Static Fence Collision
TEST_F(ContactAndSensorEventTest, DynamicCircleVsStaticFenceCollision) {
    Engine::Scene scene("CircleVsStaticFenceScene");
    scene.OnRuntimeStart();

    auto* hero = scene.CreateGameObject("Hero");
    hero->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* heroRb = hero->AddComponent<Engine::Rigidbody2D>();
    heroRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    heroRb->LinearDamping = 0.0f;
    auto* heroCol = hero->AddComponent<Engine::CircleCollider2D>();
    heroCol->Radius = 0.5f;
    heroCol->Density = 2.0f;
    heroCol->EnableHitEvents = true;
    auto* heroReceiver = hero->AddComponent<TestEventReceiver>();

    // Static fence with standalone BoxCollider2D (no Rigidbody2D -> static body)
    auto* fence = scene.CreateGameObject("WoodFence");
    fence->GetTransform()->position = { 3.0f, 0.0f, 0.0f };
    auto* fenceCol = fence->AddComponent<Engine::BoxCollider2D>();
    fenceCol->size = { 1.0f, 2.0f };
    fenceCol->EnableHitEvents = true;
    auto* fenceReceiver = fence->AddComponent<TestEventReceiver>();

    heroRb->SetVelocityXZ({ 14.0f, 0.0f });

    bool collided = false;
    for (int frame = 0; frame < 30; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
        if (heroReceiver->CollisionEnterCount > 0) {
            collided = true;
            break;
        }
    }

    ASSERT_TRUE(collided);
    EXPECT_EQ(heroReceiver->CollisionEnterCount, 1);
    EXPECT_EQ(fenceReceiver->CollisionEnterCount, 1);

    EXPECT_EQ(heroReceiver->LastCollisionOther, fence);
    EXPECT_EQ(fenceReceiver->LastCollisionOther, hero);

    EXPECT_GT(heroReceiver->LastCollisionInfo.ApproachSpeed, 5.0f);

    // Reduced mass for dynamic vs static should equal the dynamic body's mass
    const float heroMass = heroRb->GetMass();
    EXPECT_GT(heroMass, 0.0f);
    EXPECT_NEAR(heroReceiver->LastCollisionInfo.EffectiveImpulse, heroMass * heroReceiver->LastCollisionInfo.ApproachSpeed, 0.1f);

    scene.OnRuntimeStop();
}

// 3. Sensor Trigger Enter and Exit without physical deflection
TEST_F(ContactAndSensorEventTest, SensorTriggerEnterAndExitWithoutDeflection) {
    Engine::Scene scene("SensorTriggerScene");
    scene.OnRuntimeStart();

    auto* player = scene.CreateGameObject("Player");
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->LinearDamping = 0.0f;
    auto* playerCol = player->AddComponent<Engine::CircleCollider2D>();
    playerCol->Radius = 0.4f;
    auto* playerReceiver = player->AddComponent<TestEventReceiver>();

    // Wood pickup zone as a static Sensor
    auto* pickup = scene.CreateGameObject("WoodPickup");
    pickup->GetTransform()->position = { 2.5f, 0.0f, 0.0f };
    auto* pickupCol = pickup->AddComponent<Engine::CircleCollider2D>();
    pickupCol->Radius = 0.6f;
    pickupCol->IsSensor = true; // Sensor trigger!
    auto* pickupReceiver = pickup->AddComponent<TestEventReceiver>();

    const float initialSpeedX = 10.0f;
    playerRb->SetVelocityXZ({ initialSpeedX, 0.0f });

    bool enteredTrigger = false;
    bool exitedTrigger = false;

    // Step 50 frames (enough for player to enter and fully pass through)
    for (int frame = 0; frame < 50; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);

        if (playerReceiver->TriggerEnterCount > 0) {
            enteredTrigger = true;
            // No physical collision enter should be triggered
            EXPECT_EQ(playerReceiver->CollisionEnterCount, 0);

            // Velocity must NOT be deflected by sensor overlap
            glm::vec2 currentVel = playerRb->GetVelocityXZ();
            EXPECT_NEAR(currentVel.x, initialSpeedX, 0.05f);
            EXPECT_NEAR(currentVel.y, 0.0f, 0.05f);
        }

        if (playerReceiver->TriggerExitCount > 0) {
            exitedTrigger = true;
        }
    }

    EXPECT_TRUE(enteredTrigger);
    EXPECT_TRUE(exitedTrigger);
    EXPECT_EQ(playerReceiver->TriggerEnterCount, 1);
    EXPECT_EQ(playerReceiver->TriggerExitCount, 1);
    EXPECT_EQ(pickupReceiver->TriggerEnterCount, 1);
    EXPECT_EQ(pickupReceiver->TriggerExitCount, 1);

    EXPECT_EQ(playerReceiver->LastTriggerEnterOther, pickup);
    EXPECT_EQ(pickupReceiver->LastTriggerEnterOther, player);
    EXPECT_EQ(playerReceiver->LastTriggerExitOther, pickup);
    EXPECT_EQ(pickupReceiver->LastTriggerExitOther, player);

    scene.OnRuntimeStop();
}

// 4. Collision Exit event after separation
TEST_F(ContactAndSensorEventTest, CollisionExitEventAfterSeparation) {
    Engine::Scene scene("CollisionExitScene");
    scene.OnRuntimeStart();

    auto* bodyA = scene.CreateGameObject("BodyA");
    bodyA->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbA = bodyA->AddComponent<Engine::Rigidbody2D>();
    rbA->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbA->LinearDamping = 0.0f;
    auto* colA = bodyA->AddComponent<Engine::CircleCollider2D>();
    colA->Radius = 0.5f;
    colA->Restitution = 1.0f; // Bouncy elastic collision
    auto* receiverA = bodyA->AddComponent<TestEventReceiver>();

    auto* bodyB = scene.CreateGameObject("BodyB");
    bodyB->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    auto* rbB = bodyB->AddComponent<Engine::Rigidbody2D>();
    rbB->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbB->LinearDamping = 0.0f;
    auto* colB = bodyB->AddComponent<Engine::CircleCollider2D>();
    colB->Radius = 0.5f;
    colB->Restitution = 1.0f;
    auto* receiverB = bodyB->AddComponent<TestEventReceiver>();

    rbA->SetVelocityXZ({ 10.0f, 0.0f });

    bool entered = false;
    bool exited = false;

    for (int frame = 0; frame < 50; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
        if (receiverA->CollisionEnterCount > 0) {
            entered = true;
        }
        if (receiverA->CollisionExitCount > 0) {
            exited = true;
        }
    }

    EXPECT_TRUE(entered);
    EXPECT_TRUE(exited);
    EXPECT_EQ(receiverA->CollisionExitCount, 1);
    EXPECT_EQ(receiverB->CollisionExitCount, 1);

    scene.OnRuntimeStop();
}

// 5. Deferred destruction inside collision callback
TEST_F(ContactAndSensorEventTest, DeferredDestructionInsideCollisionCallback) {
    Engine::Scene scene("DeferredDestroyCollisionScene");
    scene.OnRuntimeStart();

    auto* projectile = scene.CreateGameObject("WoodSplinter");
    projectile->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbA = projectile->AddComponent<Engine::Rigidbody2D>();
    rbA->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbA->LinearDamping = 0.0f;
    auto* colA = projectile->AddComponent<Engine::CircleCollider2D>();
    colA->Radius = 0.3f;
    auto* receiverA = projectile->AddComponent<TestEventReceiver>();
    receiverA->DestroySelfOnCollision = true; // Queues DestroyGameObject during callback

    auto* wall = scene.CreateGameObject("StoneWall");
    wall->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    auto* rbB = wall->AddComponent<Engine::Rigidbody2D>();
    rbB->Type = Engine::Rigidbody2D::BodyType::Static;
    auto* colB = wall->AddComponent<Engine::BoxCollider2D>();
    colB->size = { 1.0f, 2.0f };
    auto* receiverB = wall->AddComponent<TestEventReceiver>();

    rbA->SetVelocityXZ({ 12.0f, 0.0f });

    // Step simulation: should not crash or trigger use-after-free
    for (int frame = 0; frame < 20; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    // Wall should have received the collision enter event
    EXPECT_GE(receiverB->CollisionEnterCount, 1);

    // Projectile should be deleted from active entities
    EXPECT_EQ(scene.FindGameObjectByUUID(projectile->GetUUID()), nullptr);

    scene.OnRuntimeStop();
}

// 6. CapsuleCollider2D dynamic contact
TEST_F(ContactAndSensorEventTest, CapsuleColliderDynamicContact) {
    Engine::Scene scene("CapsuleContactScene");
    scene.OnRuntimeStart();

    auto* pill = scene.CreateGameObject("CapsuleActor");
    pill->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb = pill->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    auto* cap = pill->AddComponent<Engine::CapsuleCollider2D>();
    cap->Center1 = { 0.0f, -0.4f };
    cap->Center2 = { 0.0f, 0.4f };
    cap->Radius = 0.35f;
    cap->EnableHitEvents = true;
    auto* receiver = pill->AddComponent<TestEventReceiver>();

    auto* obstacle = scene.CreateGameObject("Obstacle");
    obstacle->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    auto* obsCol = obstacle->AddComponent<Engine::CircleCollider2D>();
    obsCol->Radius = 0.5f;
    auto* obsReceiver = obstacle->AddComponent<TestEventReceiver>();

    rb->SetVelocityXZ({ 10.0f, 0.0f });

    bool collided = false;
    for (int frame = 0; frame < 30; ++frame) {
        scene.UpdateRuntime(1.0f / 60.0f);
        if (receiver->CollisionEnterCount > 0) {
            collided = true;
            break;
        }
    }

    EXPECT_TRUE(collided);
    EXPECT_EQ(receiver->CollisionEnterCount, 1);
    EXPECT_EQ(obsReceiver->CollisionEnterCount, 1);
    EXPECT_EQ(receiver->LastCollisionOther, obstacle);
    EXPECT_EQ(obsReceiver->LastCollisionOther, pill);

    scene.OnRuntimeStop();
}

// 7. SceneSerializer Circle and Capsule Round-Trip
TEST_F(ContactAndSensorEventTest, SceneSerializerCircleAndCapsuleRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("SerializerSrcScene");

    constexpr uint64_t kGo1UUID = 88888801ULL;
    auto* go1 = srcScene->CreateGameObjectWithUUID(kGo1UUID, "CircleEntity");
    auto* circle = go1->AddComponent<Engine::CircleCollider2D>();
    circle->Radius = 0.75f;
    circle->OffsetXZ = { 0.2f, 0.3f };
    circle->Density = 2.5f;
    circle->Friction = 0.8f;
    circle->Restitution = 0.6f;
    circle->IsSensor = true;
    circle->EnableHitEvents = false;

    constexpr uint64_t kGo2UUID = 88888802ULL;
    auto* go2 = srcScene->CreateGameObjectWithUUID(kGo2UUID, "CapsuleEntity");
    auto* capsule = go2->AddComponent<Engine::CapsuleCollider2D>();
    capsule->Center1 = { -0.1f, -0.4f };
    capsule->Center2 = { 0.1f, 0.4f };
    capsule->Radius = 0.55f;
    capsule->Density = 3.0f;
    capsule->Friction = 0.4f;
    capsule->Restitution = 0.2f;
    capsule->IsSensor = false;
    capsule->EnableHitEvents = true;

    // Serialize to string
    Engine::SceneSerializer srcSerializer(srcScene);
    std::string jsonStr = srcSerializer.SerializeToString();
    EXPECT_FALSE(jsonStr.empty());

    // Deserialize into target scene
    auto dstScene = std::make_shared<Engine::Scene>("SerializerDstScene");
    Engine::SceneSerializer dstSerializer(dstScene);
    bool loadOk = dstSerializer.DeserializeFromString(jsonStr);
    EXPECT_TRUE(loadOk);

    auto* dstGo1 = dstScene->FindGameObjectByUUID(kGo1UUID);
    ASSERT_NE(dstGo1, nullptr);
    auto* dstCircle = dstGo1->GetComponent<Engine::CircleCollider2D>();
    ASSERT_NE(dstCircle, nullptr);
    EXPECT_FLOAT_EQ(dstCircle->Radius, 0.75f);
    EXPECT_FLOAT_EQ(dstCircle->OffsetXZ.x, 0.2f);
    EXPECT_FLOAT_EQ(dstCircle->OffsetXZ.y, 0.3f);
    EXPECT_FLOAT_EQ(dstCircle->Density, 2.5f);
    EXPECT_FLOAT_EQ(dstCircle->Friction, 0.8f);
    EXPECT_FLOAT_EQ(dstCircle->Restitution, 0.6f);
    EXPECT_TRUE(dstCircle->IsSensor);
    EXPECT_FALSE(dstCircle->EnableHitEvents);

    auto* dstGo2 = dstScene->FindGameObjectByUUID(kGo2UUID);
    ASSERT_NE(dstGo2, nullptr);
    auto* dstCapsule = dstGo2->GetComponent<Engine::CapsuleCollider2D>();
    ASSERT_NE(dstCapsule, nullptr);
    EXPECT_FLOAT_EQ(dstCapsule->Center1.x, -0.1f);
    EXPECT_FLOAT_EQ(dstCapsule->Center1.y, -0.4f);
    EXPECT_FLOAT_EQ(dstCapsule->Center2.x, 0.1f);
    EXPECT_FLOAT_EQ(dstCapsule->Center2.y, 0.4f);
    EXPECT_FLOAT_EQ(dstCapsule->Radius, 0.55f);
    EXPECT_FLOAT_EQ(dstCapsule->Density, 3.0f);
    EXPECT_FLOAT_EQ(dstCapsule->Friction, 0.4f);
    EXPECT_FLOAT_EQ(dstCapsule->Restitution, 0.2f);
    EXPECT_FALSE(dstCapsule->IsSensor);
    EXPECT_TRUE(dstCapsule->EnableHitEvents);
}
