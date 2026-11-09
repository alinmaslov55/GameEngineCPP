#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Transform.h"
#include <box2d/box2d.h>
#include <nlohmann/json.hpp>

namespace {

// Test 1: Dynamic entity sliding on ground (Y = 0) is blocked by a LowObstacle fence
TEST(ElevationFilterTest, GroundSliding_BlockedByLowObstacle) {
    Engine::Scene scene("GroundSlidingFenceTest");

    // Static LowObstacle Fence at (0, 0, 3)
    auto* fence = scene.CreateGameObject("Fence");
    fence->GetTransform()->position = { 0.0f, 0.0f, 3.0f };
    auto* fenceCol = fence->AddComponent<Engine::BoxCollider2D>();
    fenceCol->size = { 4.0f, 0.5f };
    fenceCol->CategoryBits = Engine::PhysicsLayer::LowObstacle;
    fenceCol->MaskBits = Engine::PhysicsLayer::All;

    // Dynamic Jiangshi at (0, 0, 0)
    auto* jiangshi = scene.CreateGameObject("Jiangshi");
    jiangshi->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb2d = jiangshi->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 0.0f; // frictionless for clean measurement
    rb2d->GroundLevelY = 0.0f;
    rb2d->UseGravityY = true;
    rb2d->EnableAirbornePassThrough = true;
    rb2d->AirborneClearanceHeight = 0.75f;

    auto* col = jiangshi->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.45f;
    col->CategoryBits = Engine::PhysicsLayer::Enemy;
    col->MaskBits = Engine::PhysicsLayer::All;

    scene.OnRuntimeStart();
    rb2d->SetLinearVelocityXZ(glm::vec2{ 0.0f, 6.0f }); // Velocity towards +Z

    // Step 60 frames (1 second)
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Jiangshi must NOT pass the fence at Z = 3.0f (must stop around Z ~ 2.3m)
    auto* t = jiangshi->GetTransform();
    EXPECT_LT(t->position.z, 3.0f);
    EXPECT_FLOAT_EQ(t->position.y, 0.0f);
    EXPECT_FALSE(rb2d->IsAirbornePassThroughActive());

    scene.OnRuntimeStop();
}

// Test 2: Dynamic entity hopping over fence vaults cleanly when Y > 0.75m
TEST(ElevationFilterTest, AirborneHop_VaultsOverLowObstacleCleanly) {
    Engine::Scene scene("AirborneVaultTest");

    // Static LowObstacle Fence at (0, 0, 3)
    auto* fence = scene.CreateGameObject("Fence");
    fence->GetTransform()->position = { 0.0f, 0.0f, 3.0f };
    auto* fenceCol = fence->AddComponent<Engine::BoxCollider2D>();
    fenceCol->size = { 4.0f, 0.5f };
    fenceCol->CategoryBits = Engine::PhysicsLayer::LowObstacle;
    fenceCol->MaskBits = Engine::PhysicsLayer::All;

    // Dynamic Jiangshi at (0, 0, 0)
    auto* jiangshi = scene.CreateGameObject("Jiangshi");
    jiangshi->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb2d = jiangshi->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 0.0f;
    rb2d->GroundLevelY = 0.0f;
    rb2d->UseGravityY = true;
    rb2d->GravityY = -22.0f;
    rb2d->EnableAirbornePassThrough = true;
    rb2d->AirborneClearanceHeight = 0.75f;

    auto* col = jiangshi->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.45f;
    col->CategoryBits = Engine::PhysicsLayer::Enemy;
    col->MaskBits = Engine::PhysicsLayer::All;

    scene.OnRuntimeStart();

    // Jiangshi launches forward at 6 m/s and hops vertically with impulse 12 m/s
    rb2d->SetLinearVelocityXZ(glm::vec2{ 0.0f, 6.0f });
    rb2d->LaunchVertical(12.0f);

    constexpr float dt = 1.0f / 60.0f;
    bool reachedOverFenceMidAir = false;

    // Step 80 frames (~1.33 seconds, jump duration ~ 1.09s)
    for (int i = 0; i < 80; ++i) {
        scene.UpdateRuntime(dt);
        auto* t = jiangshi->GetTransform();
        if (t->position.z >= 2.5f && t->position.z <= 3.5f) {
            if (t->position.y > 0.75f && rb2d->IsAirbornePassThroughActive()) {
                reachedOverFenceMidAir = true;
            }
        }
    }

    auto* t = jiangshi->GetTransform();
    // Verify it was airborne while over the fence
    EXPECT_TRUE(reachedOverFenceMidAir);
    // Verify it vaulted past the fence at Z = 3.0f and landed on the other side
    EXPECT_GT(t->position.z, 5.0f);
    EXPECT_FLOAT_EQ(t->position.y, 0.0f);
    EXPECT_FALSE(rb2d->IsAirbornePassThroughActive());

    scene.OnRuntimeStop();
}

// Test 3: Airborne entity mid-hop is STILL BLOCKED by Environment (tall tree/cliff)
TEST(ElevationFilterTest, AirborneHop_StillBlockedByEnvironment) {
    Engine::Scene scene("AirborneEnvironmentBlockTest");

    // Static tall tree / shrine cliff (Environment layer) at (0, 0, 3)
    auto* tree = scene.CreateGameObject("TallTree");
    tree->GetTransform()->position = { 0.0f, 0.0f, 3.0f };
    auto* treeCol = tree->AddComponent<Engine::BoxCollider2D>();
    treeCol->size = { 4.0f, 0.5f };
    treeCol->CategoryBits = Engine::PhysicsLayer::Environment; // Unvaultable!
    treeCol->MaskBits = Engine::PhysicsLayer::All;

    // Dynamic Jiangshi at (0, 0, 0)
    auto* jiangshi = scene.CreateGameObject("Jiangshi");
    jiangshi->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb2d = jiangshi->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 0.0f;
    rb2d->GroundLevelY = 0.0f;
    rb2d->UseGravityY = true;
    rb2d->GravityY = -22.0f;
    rb2d->EnableAirbornePassThrough = true;
    rb2d->AirborneClearanceHeight = 0.75f;

    auto* col = jiangshi->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.45f;
    col->CategoryBits = Engine::PhysicsLayer::Enemy;
    col->MaskBits = Engine::PhysicsLayer::All;

    scene.OnRuntimeStart();
    rb2d->SetLinearVelocityXZ(glm::vec2{ 0.0f, 6.0f });
    rb2d->LaunchVertical(12.0f);

    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 80; ++i) {
        scene.UpdateRuntime(dt);
    }

    auto* t = jiangshi->GetTransform();
    // Environment layer must keep solid mid-air! Jiangshi must not pass through Z = 3.0f
    EXPECT_LT(t->position.z, 3.0f);

    scene.OnRuntimeStop();
}

// Test 4: Touchdown restores ground maskBits, blocking subsequent LowObstacle
TEST(ElevationFilterTest, AirborneTouchdown_RestoresGroundMask) {
    Engine::Scene scene("TouchdownRestoreMaskTest");

    // Fence 1 at Z = 2.5m (vaulted over)
    auto* fence1 = scene.CreateGameObject("Fence1");
    fence1->GetTransform()->position = { 0.0f, 0.0f, 2.5f };
    auto* col1 = fence1->AddComponent<Engine::BoxCollider2D>();
    col1->size = { 4.0f, 0.4f };
    col1->CategoryBits = Engine::PhysicsLayer::LowObstacle;
    col1->MaskBits = Engine::PhysicsLayer::All;

    // Fence 2 at Z = 7.0m (collided with after touchdown)
    auto* fence2 = scene.CreateGameObject("Fence2");
    fence2->GetTransform()->position = { 0.0f, 0.0f, 7.0f };
    auto* col2 = fence2->AddComponent<Engine::BoxCollider2D>();
    col2->size = { 4.0f, 0.4f };
    col2->CategoryBits = Engine::PhysicsLayer::LowObstacle;
    col2->MaskBits = Engine::PhysicsLayer::All;

    // Dynamic entity hopping forward
    auto* actor = scene.CreateGameObject("Actor");
    actor->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb2d = actor->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 0.0f;
    rb2d->GroundLevelY = 0.0f;
    rb2d->UseGravityY = true;
    rb2d->GravityY = -22.0f;
    rb2d->EnableAirbornePassThrough = true;
    rb2d->AirborneClearanceHeight = 0.75f;

    auto* col = actor->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.35f;
    col->CategoryBits = Engine::PhysicsLayer::Player;
    col->MaskBits = Engine::PhysicsLayer::All;

    scene.OnRuntimeStart();
    // Launch: air time ~ 2 * 9.0 / 22.0 = 0.82s. Distance in air = 6 * 0.82 = 4.9m -> lands at Z ~ 4.9m
    rb2d->SetLinearVelocityXZ(glm::vec2{ 0.0f, 6.0f });
    rb2d->LaunchVertical(9.0f);

    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 90; ++i) { // 1.5 seconds
        scene.UpdateRuntime(dt);
    }

    auto* t = actor->GetTransform();
    // Passed Fence 1 cleanly
    EXPECT_GT(t->position.z, 3.0f);
    // Blocked by Fence 2 after landing
    EXPECT_LT(t->position.z, 7.0f);
    EXPECT_FLOAT_EQ(t->position.y, 0.0f);
    EXPECT_FALSE(rb2d->IsAirbornePassThroughActive());

    scene.OnRuntimeStop();
}

// Test 5: Custom layer conversion and mask matching
TEST(ElevationFilterTest, CustomLayerCollisionFiltering) {
    // Compile-time verification of branchless bit alignment across all 10 layers
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Default) == Engine::PhysicsLayer::Default);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Environment) == Engine::PhysicsLayer::Environment);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::LowObstacle) == Engine::PhysicsLayer::LowObstacle);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Player) == Engine::PhysicsLayer::Player);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Enemy) == Engine::PhysicsLayer::Enemy);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Companion) == Engine::PhysicsLayer::Companion);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::PlayerHitbox) == Engine::PhysicsLayer::PlayerHitbox);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::EnemyHitbox) == Engine::PhysicsLayer::EnemyHitbox);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Projectile) == Engine::PhysicsLayer::Projectile);
    static_assert(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Pickup) == Engine::PhysicsLayer::Pickup);

    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Default) == Engine::EntityLayer::Default);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Environment) == Engine::EntityLayer::Environment);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::LowObstacle) == Engine::EntityLayer::LowObstacle);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Player) == Engine::EntityLayer::Player);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Enemy) == Engine::EntityLayer::Enemy);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Companion) == Engine::EntityLayer::Companion);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::PlayerHitbox) == Engine::EntityLayer::PlayerHitbox);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::EnemyHitbox) == Engine::EntityLayer::EnemyHitbox);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Projectile) == Engine::EntityLayer::Projectile);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Pickup) == Engine::EntityLayer::Pickup);

    // Multi-layer mask bit combination cast
    constexpr uint32_t multiEntity = Engine::EntityLayer::Player | Engine::EntityLayer::PlayerHitbox | Engine::EntityLayer::Environment;
    constexpr uint64_t multiPhysics = Engine::PhysicsLayer::Player | Engine::PhysicsLayer::PlayerHitbox | Engine::PhysicsLayer::Environment;
    static_assert(Engine::PhysicsLayer::FromEntityLayer(multiEntity) == multiPhysics);
    static_assert(Engine::PhysicsLayer::ToEntityLayer(multiPhysics) == multiEntity);

    // EntityLayer <-> PhysicsLayer bidirectional runtime mapping
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Default), Engine::PhysicsLayer::Default);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Environment), Engine::PhysicsLayer::Environment);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::LowObstacle), Engine::PhysicsLayer::LowObstacle);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Player), Engine::PhysicsLayer::Player);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Enemy), Engine::PhysicsLayer::Enemy);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Companion), Engine::PhysicsLayer::Companion);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::PlayerHitbox), Engine::PhysicsLayer::PlayerHitbox);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::EnemyHitbox), Engine::PhysicsLayer::EnemyHitbox);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Projectile), Engine::PhysicsLayer::Projectile);
    EXPECT_EQ(Engine::PhysicsLayer::FromEntityLayer(Engine::EntityLayer::Pickup), Engine::PhysicsLayer::Pickup);

    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Default), Engine::EntityLayer::Default);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Environment), Engine::EntityLayer::Environment);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::LowObstacle), Engine::EntityLayer::LowObstacle);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Player), Engine::EntityLayer::Player);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Enemy), Engine::EntityLayer::Enemy);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Companion), Engine::EntityLayer::Companion);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::PlayerHitbox), Engine::EntityLayer::PlayerHitbox);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::EnemyHitbox), Engine::EntityLayer::EnemyHitbox);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Projectile), Engine::EntityLayer::Projectile);
    EXPECT_EQ(Engine::PhysicsLayer::ToEntityLayer(Engine::PhysicsLayer::Pickup), Engine::EntityLayer::Pickup);

    // Box2D Shape Filtering logic
    Engine::Scene scene("FilterMatchingScene");

    auto* bodyA = scene.CreateGameObject("BodyA");
    bodyA->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbA = bodyA->AddComponent<Engine::Rigidbody2D>();
    rbA->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbA->LinearDamping = 0.0f;
    auto* colA = bodyA->AddComponent<Engine::CircleCollider2D>();
    colA->Radius = 0.5f;
    // Body A is a PlayerHitbox that only collides with Enemy
    colA->CategoryBits = Engine::PhysicsLayer::PlayerHitbox;
    colA->MaskBits = Engine::PhysicsLayer::Enemy;

    auto* bodyB = scene.CreateGameObject("BodyB");
    bodyB->GetTransform()->position = { 0.0f, 0.0f, 2.0f };
    auto* colB = bodyB->AddComponent<Engine::CircleCollider2D>();
    colB->Radius = 0.5f;
    // Body B is Player (should NOT collide with Body A)
    colB->CategoryBits = Engine::PhysicsLayer::Player;
    colB->MaskBits = Engine::PhysicsLayer::Environment | Engine::PhysicsLayer::Enemy;

    scene.OnRuntimeStart();
    rbA->SetLinearVelocityXZ(glm::vec2{ 0.0f, 5.0f });

    // Step 30 frames (0.5s) -> Body A translates to Z ~ 2.5m, passing right through Body B
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    EXPECT_GT(bodyA->GetTransform()->position.z, 2.0f);

    scene.OnRuntimeStop();
}

// Test 6: Runtime filter modification dynamically updates Box2D broadphase
TEST(ElevationFilterTest, DynamicFilterModificationRuntime) {
    Engine::Scene scene("RuntimeFilterModScene");

    auto* wall = scene.CreateGameObject("Wall");
    wall->GetTransform()->position = { 0.0f, 0.0f, 2.0f };
    auto* wallCol = wall->AddComponent<Engine::BoxCollider2D>();
    wallCol->size = { 4.0f, 0.5f };
    wallCol->CategoryBits = Engine::PhysicsLayer::Enemy;
    wallCol->MaskBits = Engine::PhysicsLayer::All;

    auto* mover = scene.CreateGameObject("Mover");
    mover->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb = mover->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    auto* moverCol = mover->AddComponent<Engine::CircleCollider2D>();
    moverCol->Radius = 0.4f;
    moverCol->CategoryBits = Engine::PhysicsLayer::Player;
    // Initially ignores Enemy layer
    moverCol->MaskBits = Engine::PhysicsLayer::Environment;

    scene.OnRuntimeStart();
    rb->SetLinearVelocityXZ(glm::vec2{ 0.0f, 5.0f });

    // Step 10 frames: translates toward wall without collision
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; ++i) {
        scene.UpdateRuntime(dt);
    }
    EXPECT_GT(mover->GetTransform()->position.z, 0.5f);

    // Reset position and enable collision with Enemy at runtime via SetMaskBits
    rb->SetPositionXZ(glm::vec2{ 0.0f, 0.0f });
    rb->SetLinearVelocityXZ(glm::vec2{ 0.0f, 5.0f });
    moverCol->SetMaskBits(Engine::PhysicsLayer::Environment | Engine::PhysicsLayer::Enemy);

    // Step 30 frames: should now collide and be blocked before passing Z = 2.0m
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    EXPECT_LT(mover->GetTransform()->position.z, 2.0f);

    scene.OnRuntimeStop();
}

// Test 7: Serialization of filter and airborne properties
TEST(ElevationFilterTest, SerializationOfFilterAndAirborneProperties) {
    auto scene = std::make_shared<Engine::Scene>("SerializationTestScene");

    constexpr uint64_t kPrefabUUID = 998877665544ULL;
    auto* entity = scene->CreateGameObjectWithUUID(kPrefabUUID, "JiangshiPrefab");
    auto* rb = entity->AddComponent<Engine::Rigidbody2D>();
    rb->AirborneClearanceHeight = 1.15f;
    rb->EnableAirbornePassThrough = false;
    rb->AirbornePassThroughMask = Engine::PhysicsLayer::LowObstacle;

    auto* box = entity->AddComponent<Engine::BoxCollider2D>();
    box->CategoryBits = Engine::PhysicsLayer::Enemy;
    box->MaskBits = Engine::PhysicsLayer::Player | Engine::PhysicsLayer::PlayerHitbox;
    box->GroupIndex = -2;

    auto* circle = entity->AddComponent<Engine::CircleCollider2D>();
    circle->CategoryBits = Engine::PhysicsLayer::EnemyHitbox;
    circle->MaskBits = Engine::PhysicsLayer::Player;
    circle->GroupIndex = 3;

    auto* capsule = entity->AddComponent<Engine::CapsuleCollider2D>();
    capsule->CategoryBits = Engine::PhysicsLayer::LowObstacle;
    capsule->MaskBits = Engine::PhysicsLayer::Projectile;
    capsule->GroupIndex = -1;

    // Serialize
    Engine::SceneSerializer sourceSerializer(scene);
    const std::string jsonPayload = sourceSerializer.SerializeToString();
    ASSERT_FALSE(jsonPayload.empty());

    // Deserialize into fresh scene
    auto loadedScene = std::make_shared<Engine::Scene>("LoadedScene");
    Engine::SceneSerializer cloneSerializer(loadedScene);
    ASSERT_TRUE(cloneSerializer.DeserializeFromString(jsonPayload));

    auto* loadedEntity = loadedScene->FindGameObjectByUUID(kPrefabUUID);
    ASSERT_NE(loadedEntity, nullptr);

    auto* loadedRb = loadedEntity->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(loadedRb, nullptr);
    EXPECT_FLOAT_EQ(loadedRb->AirborneClearanceHeight, 1.15f);
    EXPECT_FALSE(loadedRb->EnableAirbornePassThrough);
    EXPECT_EQ(loadedRb->AirbornePassThroughMask, Engine::PhysicsLayer::LowObstacle);

    auto* loadedBox = loadedEntity->GetComponent<Engine::BoxCollider2D>();
    ASSERT_NE(loadedBox, nullptr);
    EXPECT_EQ(loadedBox->CategoryBits, Engine::PhysicsLayer::Enemy);
    EXPECT_EQ(loadedBox->MaskBits, Engine::PhysicsLayer::Player | Engine::PhysicsLayer::PlayerHitbox);
    EXPECT_EQ(loadedBox->GroupIndex, -2);

    auto* loadedCircle = loadedEntity->GetComponent<Engine::CircleCollider2D>();
    ASSERT_NE(loadedCircle, nullptr);
    EXPECT_EQ(loadedCircle->CategoryBits, Engine::PhysicsLayer::EnemyHitbox);
    EXPECT_EQ(loadedCircle->MaskBits, Engine::PhysicsLayer::Player);
    EXPECT_EQ(loadedCircle->GroupIndex, 3);

    auto* loadedCapsule = loadedEntity->GetComponent<Engine::CapsuleCollider2D>();
    ASSERT_NE(loadedCapsule, nullptr);
    EXPECT_EQ(loadedCapsule->CategoryBits, Engine::PhysicsLayer::LowObstacle);
    EXPECT_EQ(loadedCapsule->MaskBits, Engine::PhysicsLayer::Projectile);
    EXPECT_EQ(loadedCapsule->GroupIndex, -1);
}

} // namespace
