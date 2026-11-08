#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Transform.h"
#include <box2d/box2d.h>

namespace {

// Test 1: OverlapCircleXZ filters targets correctly based on 64-bit PhysicsLayer maskBits
TEST(SpatialQueriesAndStasisTest, OverlapCircleXZ_LayerFiltering) {
    Engine::Scene scene("OverlapCircleTest");

    // Enemy 1 at (0, 0, 2)
    auto* enemy1 = scene.CreateGameObject("Enemy1");
    enemy1->GetTransform()->position = { 0.0f, 0.0f, 2.0f };
    auto* colE1 = enemy1->AddComponent<Engine::CircleCollider2D>();
    colE1->Radius = 0.5f;
    colE1->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Enemy 2 at (2, 0, 0)
    auto* enemy2 = scene.CreateGameObject("Enemy2");
    enemy2->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    auto* colE2 = enemy2->AddComponent<Engine::BoxCollider2D>();
    colE2->size = { 0.8f, 0.8f };
    colE2->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Player at (0, 0, 1)
    auto* player = scene.CreateGameObject("Player");
    player->GetTransform()->position = { 0.0f, 0.0f, 1.0f };
    auto* colP = player->AddComponent<Engine::CircleCollider2D>();
    colP->Radius = 0.5f;
    colP->CategoryBits = Engine::PhysicsLayer::Player;

    // Distant Tree at (0, 0, 10) - outside circle radius
    auto* tree = scene.CreateGameObject("Tree");
    tree->GetTransform()->position = { 0.0f, 0.0f, 10.0f };
    auto* colT = tree->AddComponent<Engine::BoxCollider2D>();
    colT->size = { 1.0f, 1.0f };
    colT->CategoryBits = Engine::PhysicsLayer::Environment;

    scene.OnRuntimeStart();

    // Query 1: Overlap radius 4.0m with Enemy mask
    std::vector<Engine::GameObject*> enemyHits;
    scene.OverlapCircleXZ(glm::vec2{ 0.0f, 0.0f }, 4.0f, Engine::PhysicsLayer::Enemy, enemyHits);
    EXPECT_EQ(enemyHits.size(), 2u);

    // Verify both enemies were hit
    bool foundE1 = false, foundE2 = false;
    for (auto* hit : enemyHits) {
        if (hit == enemy1) foundE1 = true;
        if (hit == enemy2) foundE2 = true;
    }
    EXPECT_TRUE(foundE1);
    EXPECT_TRUE(foundE2);

    // Query 2: Overlap radius 4.0m with Player mask
    std::vector<Engine::GameObject*> playerHits;
    scene.OverlapCircleXZ(glm::vec2{ 0.0f, 0.0f }, 4.0f, Engine::PhysicsLayer::Player, playerHits);
    ASSERT_EQ(playerHits.size(), 1u);
    EXPECT_EQ(playerHits[0], player);

    // Query 3: Overlap with Environment mask (Tree is at 10m, so radius 4m should find nothing)
    std::vector<Engine::GameObject*> envHits;
    scene.OverlapCircleXZ(glm::vec2{ 0.0f, 0.0f }, 4.0f, Engine::PhysicsLayer::Environment, envHits);
    EXPECT_EQ(envHits.size(), 0u);

    scene.OnRuntimeStop();
}

// Test 2: OverlapArcXZ (90° cone) hits targets in front and ignores targets behind/outside
TEST(SpatialQueriesAndStasisTest, OverlapArcXZ_FrontHitAndRearRejection) {
    Engine::Scene scene("OverlapArcTest");

    // Forward direction is +Z ({0.0f, 1.0f})
    const glm::vec2 origin{ 0.0f, 0.0f };
    const glm::vec2 forward{ 0.0f, 1.0f };

    // Target 1: Dead ahead (0° off forward) at (0, 0, 3)
    auto* front = scene.CreateGameObject("FrontTarget");
    front->GetTransform()->position = { 0.0f, 0.0f, 3.0f };
    auto* col1 = front->AddComponent<Engine::CircleCollider2D>();
    col1->Radius = 0.4f;
    col1->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Target 2: 26.5° off forward at (1, 0, 2)
    auto* slightAngle = scene.CreateGameObject("SlightAngleTarget");
    slightAngle->GetTransform()->position = { 1.0f, 0.0f, 2.0f };
    auto* col2 = slightAngle->AddComponent<Engine::CircleCollider2D>();
    col2->Radius = 0.4f;
    col2->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Target 3: 71.5° off forward (outside 45° half-angle) at (3, 0, 1)
    auto* wideAngle = scene.CreateGameObject("WideAngleTarget");
    wideAngle->GetTransform()->position = { 3.0f, 0.0f, 1.0f };
    auto* col3 = wideAngle->AddComponent<Engine::CircleCollider2D>();
    col3->Radius = 0.4f;
    col3->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Target 4: Directly behind (180° off forward) at (0, 0, -3)
    auto* rear = scene.CreateGameObject("RearTarget");
    rear->GetTransform()->position = { 0.0f, 0.0f, -3.0f };
    auto* col4 = rear->AddComponent<Engine::CircleCollider2D>();
    col4->Radius = 0.4f;
    col4->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Target 5: Behind-left (135° off forward) at (-2, 0, -2)
    auto* leftRear = scene.CreateGameObject("LeftRearTarget");
    leftRear->GetTransform()->position = { -2.0f, 0.0f, -2.0f };
    auto* col5 = leftRear->AddComponent<Engine::CircleCollider2D>();
    col5->Radius = 0.4f;
    col5->CategoryBits = Engine::PhysicsLayer::Enemy;

    scene.OnRuntimeStart();

    // Query: 90° cone (half-angle 45°) with radius 5.0m
    std::vector<Engine::GameObject*> arcHits;
    scene.OverlapArcXZ(origin, forward, 5.0f, 45.0f, Engine::PhysicsLayer::Enemy, arcHits);

    // Front (0°) and SlightAngle (26.5°) must be hit; WideAngle (71.5°), Rear (180°), and LeftRear (135°) must be rejected
    EXPECT_EQ(arcHits.size(), 2u);

    bool foundFront = false, foundSlight = false;
    for (auto* hit : arcHits) {
        if (hit == front) foundFront = true;
        if (hit == slightAngle) foundSlight = true;
    }
    EXPECT_TRUE(foundFront);
    EXPECT_TRUE(foundSlight);

    scene.OnRuntimeStop();
}

// Test 3: RaycastClosestXZ accurately returns closest hit and surface normal
TEST(SpatialQueriesAndStasisTest, RaycastClosestXZ_ObstacleOcclusion) {
    Engine::Scene scene("RaycastClosestTest");

    // Wall A at (0, 0, 3)
    auto* wallA = scene.CreateGameObject("WallA");
    wallA->GetTransform()->position = { 0.0f, 0.0f, 3.0f };
    auto* colA = wallA->AddComponent<Engine::BoxCollider2D>();
    colA->size = { 4.0f, 0.4f };
    colA->CategoryBits = Engine::PhysicsLayer::Environment;

    // Wall B at (0, 0, 7) - occluded by Wall A
    auto* wallB = scene.CreateGameObject("WallB");
    wallB->GetTransform()->position = { 0.0f, 0.0f, 7.0f };
    auto* colB = wallB->AddComponent<Engine::BoxCollider2D>();
    colB->size = { 4.0f, 0.4f };
    colB->CategoryBits = Engine::PhysicsLayer::Environment;

    scene.OnRuntimeStart();

    Engine::RaycastHitXZ hit;
    bool hitFound = scene.RaycastClosestXZ(glm::vec2{ 0.0f, 0.0f }, glm::vec2{ 0.0f, 10.0f },
                                          Engine::PhysicsLayer::Environment, hit);

    ASSERT_TRUE(hitFound);
    EXPECT_EQ(hit.Entity, wallA);
    // Wall A center is Z=3.0, half depth is 0.2 -> impact should be near Z=2.8
    EXPECT_NEAR(hit.PointXZ.x, 0.0f, 1e-3f);
    EXPECT_NEAR(hit.PointXZ.y, 2.8f, 0.05f);
    // Normal points toward ray origin (-Z)
    EXPECT_NEAR(hit.NormalXZ.x, 0.0f, 1e-3f);
    EXPECT_NEAR(hit.NormalXZ.y, -1.0f, 1e-3f);
    // Fraction along 10m ray should be ~ 0.28
    EXPECT_NEAR(hit.Fraction, 0.28f, 0.02f);

    scene.OnRuntimeStop();
}

// Test 4: SetStasisFrozen(true) halts horizontal motion and suspends airborne Y elevation
TEST(SpatialQueriesAndStasisTest, ChronosStasis_MotionHaltAndMidAirSuspension) {
    Engine::Scene scene("StasisHaltTest");

    auto* brawler = scene.CreateGameObject("Ghoul");
    auto* t = brawler->GetTransform();
    t->position = { 0.0f, 2.0f, 0.0f };

    auto* rb = brawler->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    rb->GroundLevelY = 0.0f;
    rb->UseGravityY = true;
    rb->GravityY = -22.0f;
    rb->InitialLinearVelocityXZ = { 0.0f, 6.0f };
    rb->VerticalVelocity = 8.0f;

    auto* col = brawler->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.45f;

    scene.OnRuntimeStart();

    // Step 5 frames (approx 0.083s) so entity gains motion
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 5; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Freeze in stasis!
    rb->SetStasisFrozen(true);
    EXPECT_TRUE(rb->IsStasisFrozen());

    const glm::vec3 posFrozen = t->position;
    EXPECT_GT(posFrozen.y, 2.0f); // Was airborne moving up
    EXPECT_GT(posFrozen.z, 0.2f); // Was translating forward

    // Step 120 fixed physics steps (2 full seconds)
    for (int i = 0; i < 120; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Entity must remain completely frozen at the exact same coordinates!
    EXPECT_FLOAT_EQ(t->position.x, posFrozen.x);
    EXPECT_FLOAT_EQ(t->position.y, posFrozen.y);
    EXPECT_FLOAT_EQ(t->position.z, posFrozen.z);

    scene.OnRuntimeStop();
}

// Test 5: Strike impulses queued during stasis freeze erupt all at once upon unfreeze
TEST(SpatialQueriesAndStasisTest, ChronosStasis_QueuedImpulsesEruptOnUnfreeze) {
    Engine::Scene scene("StasisEruptionTest");

    auto* target = scene.CreateGameObject("Thrall");
    auto* t = target->GetTransform();
    t->position = { 0.0f, 1.0f, 0.0f };

    auto* rb = target->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    rb->GroundLevelY = 0.0f;
    rb->UseGravityY = true;
    rb->GravityY = -22.0f;

    auto* col = target->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.5f;

    scene.OnRuntimeStart();

    // Freeze target in place
    rb->SetStasisFrozen(true);
    EXPECT_TRUE(rb->IsStasisFrozen());

    const glm::vec3 posFrozen = t->position;

    // Step 10 frames while frozen
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Player strikes frozen enemy with Maul (impulse +Z = 15 N*s)
    rb->ApplyImpulseXZ(glm::vec2{ 0.0f, 15.0f });

    // Player strikes again with Halberd (impulse +Z = 10 N*s, +X = 5 N*s)
    rb->ApplyImpulseXZ(glm::vec2{ 5.0f, 10.0f });

    // Verify impulse was queued in Rigidbody2D
    EXPECT_EQ(rb->GetQueuedStasisImpulseXZ(), glm::vec2(5.0f, 25.0f));

    // Step another 30 frames: body must remain locked in place at posFrozen
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }
    EXPECT_FLOAT_EQ(t->position.x, posFrozen.x);
    EXPECT_FLOAT_EQ(t->position.y, posFrozen.y);
    EXPECT_FLOAT_EQ(t->position.z, posFrozen.z);

    // Stasis ends! Unfreeze!
    rb->SetStasisFrozen(false);
    EXPECT_FALSE(rb->IsStasisFrozen());
    EXPECT_EQ(rb->GetQueuedStasisImpulseXZ(), glm::vec2(0.0f, 0.0f));

    // Step 30 frames: entity should launch forward along +Z with massive velocity
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Entity must have launched significantly forward along +Z and slightly along +X
    EXPECT_GT(t->position.z, posFrozen.z + 5.0f);
    EXPECT_GT(t->position.x, posFrozen.x + 0.5f);

    scene.OnRuntimeStop();
}

// Test 6: Serialization of Rigidbody2D stasis state
TEST(SpatialQueriesAndStasisTest, SceneSerializerStasisRoundTrip) {
    auto scene = std::make_shared<Engine::Scene>("StasisSerializationScene");

    constexpr uint64_t kGhoulUUID = 556677889900ULL;
    auto* ghoul = scene->CreateGameObjectWithUUID(kGhoulUUID, "FrozenGhoul");
    auto* rb = ghoul->AddComponent<Engine::Rigidbody2D>();
    rb->SetStasisFrozen(true);
    EXPECT_TRUE(rb->IsStasisFrozen());

    auto* col = ghoul->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.6f;

    // Serialize
    Engine::SceneSerializer sourceSerializer(scene);
    const std::string jsonPayload = sourceSerializer.SerializeToString();
    ASSERT_FALSE(jsonPayload.empty());

    // Deserialize into fresh scene
    auto loadedScene = std::make_shared<Engine::Scene>("LoadedScene");
    Engine::SceneSerializer cloneSerializer(loadedScene);
    ASSERT_TRUE(cloneSerializer.DeserializeFromString(jsonPayload));

    auto* loadedGhoul = loadedScene->FindGameObjectByUUID(kGhoulUUID);
    ASSERT_NE(loadedGhoul, nullptr);

    auto* loadedRb = loadedGhoul->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(loadedRb, nullptr);
    EXPECT_TRUE(loadedRb->IsStasisFrozen());
}

} // namespace
