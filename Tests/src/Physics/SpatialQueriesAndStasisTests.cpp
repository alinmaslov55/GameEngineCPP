#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Tilemap.h"
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

// Test 7: Deserialized frozen entity remains suspended on OnRuntimeStart and steps 60 frames motionless
TEST(SpatialQueriesAndStasisTest, ChronosStasis_DeserializedFrozenEntityRemainsSuspendedOnRuntimeStart) {
    auto scene = std::make_shared<Engine::Scene>("StasisOnRuntimeStartScene");

    constexpr uint64_t kBossUUID = 9988776655ULL;
    auto* boss = scene->CreateGameObjectWithUUID(kBossUUID, "StasisBoss");
    boss->GetTransform()->position = { 5.0f, 2.0f, 5.0f };

    auto* rb = boss->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->InitialLinearVelocityXZ = { 10.0f, 10.0f };
    rb->VerticalVelocity = 5.0f;
    rb->GroundLevelY = 0.0f;
    rb->UseGravityY = true;
    rb->SetStasisFrozen(true);
    EXPECT_TRUE(rb->IsStasisFrozen());

    auto* col = boss->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.8f;

    // Serialize
    Engine::SceneSerializer sourceSerializer(scene);
    const std::string jsonPayload = sourceSerializer.SerializeToString();
    ASSERT_FALSE(jsonPayload.empty());

    // Deserialize into fresh scene
    auto loadedScene = std::make_shared<Engine::Scene>("LoadedScene");
    Engine::SceneSerializer cloneSerializer(loadedScene);
    ASSERT_TRUE(cloneSerializer.DeserializeFromString(jsonPayload));

    auto* loadedBoss = loadedScene->FindGameObjectByUUID(kBossUUID);
    ASSERT_NE(loadedBoss, nullptr);
    auto* loadedRb = loadedBoss->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(loadedRb, nullptr);
    EXPECT_TRUE(loadedRb->IsStasisFrozen());

    // Start runtime! Body should spawn as b2_kinematicBody with zero velocity
    loadedScene->OnRuntimeStart();

    const glm::vec3 posFrozen = loadedBoss->GetTransform()->position;
    EXPECT_FLOAT_EQ(posFrozen.x, 5.0f);
    EXPECT_FLOAT_EQ(posFrozen.y, 2.0f);
    EXPECT_FLOAT_EQ(posFrozen.z, 5.0f);

    // Step 60 frames (1 full second): entity must remain suspended in mid-air
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        loadedScene->UpdateRuntime(dt);
    }

    const glm::vec3 posAfter60Frames = loadedBoss->GetTransform()->position;
    EXPECT_FLOAT_EQ(posAfter60Frames.x, posFrozen.x);
    EXPECT_FLOAT_EQ(posAfter60Frames.y, posFrozen.y);
    EXPECT_FLOAT_EQ(posAfter60Frames.z, posFrozen.z);

    // Unfreeze! Entity should restore original velocities and resume motion
    loadedRb->SetStasisFrozen(false);
    EXPECT_FALSE(loadedRb->IsStasisFrozen());

    for (int i = 0; i < 30; ++i) {
        loadedScene->UpdateRuntime(dt);
    }

    const glm::vec3 posResumed = loadedBoss->GetTransform()->position;
    EXPECT_GT(posResumed.x, posFrozen.x + 1.0f);
    EXPECT_GT(posResumed.z, posFrozen.z + 1.0f);

    loadedScene->OnRuntimeStop();
}

// Test 8: Offline queued impulse does not divide by zero if mass is zero
TEST(SpatialQueriesAndStasisTest, ChronosStasis_OfflineQueuedImpulseSafeDivisionByMass) {
    Engine::Scene scene("OfflineStasisScene");

    auto* go = scene.CreateGameObject("NoShapeEntity");
    auto* rb = go->AddComponent<Engine::Rigidbody2D>();
    rb->MassMultiplier = 0.0f;
    EXPECT_FLOAT_EQ(rb->GetMass(), 0.0f);

    rb->SetStasisFrozen(true);
    EXPECT_TRUE(rb->IsStasisFrozen());

    // Apply impulse offline while frozen
    rb->ApplyImpulseXZ(glm::vec2{ 15.0f, 25.0f });
    EXPECT_EQ(rb->GetQueuedStasisImpulseXZ(), glm::vec2(15.0f, 25.0f));

    // Unfreeze offline: should not produce NaN or Inf
    rb->SetStasisFrozen(false);
    EXPECT_FALSE(rb->IsStasisFrozen());
    EXPECT_FALSE(std::isnan(rb->InitialLinearVelocityXZ.x));
    EXPECT_FALSE(std::isnan(rb->InitialLinearVelocityXZ.y));
    EXPECT_FALSE(std::isinf(rb->InitialLinearVelocityXZ.x));
    EXPECT_FALSE(std::isinf(rb->InitialLinearVelocityXZ.y));
    EXPECT_GT(rb->InitialLinearVelocityXZ.x, 0.0f);
    EXPECT_GT(rb->InitialLinearVelocityXZ.y, 0.0f);
}

// Test 9: Non-runtime spatial queries handle parent-child hierarchy and scale
TEST(SpatialQueriesAndStasisTest, PhysicsQueries_NonRuntimeQueriesWithTransformHierarchyAndScale) {
    Engine::Scene scene("NonRuntimeHierarchyScene");

    // Parent at (10, 0, 10), scaled 2x
    auto* parent = scene.CreateGameObject("Parent");
    parent->GetTransform()->position = { 10.0f, 0.0f, 10.0f };
    parent->GetTransform()->scale = { 2.0f, 1.0f, 2.0f };

    // Child at local position (2, 0, 0) -> world position is (14, 0, 10)
    auto* child = scene.CreateGameObject("Child");
    child->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    child->GetTransform()->SetParent(parent->GetTransform(), false);

    auto* col = child->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.5f;
    col->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Do NOT call OnRuntimeStart() -> strictly non-runtime fallback!
    EXPECT_FALSE(scene.IsRuntimeRunning());

    // Query 1: Overlap at world position (14, 10) with radius 1.5 -> must find Child
    std::vector<Engine::GameObject*> hits;
    scene.OverlapCircleXZ(glm::vec2{ 14.0f, 10.0f }, 1.5f, Engine::PhysicsLayer::Enemy, hits);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], child);

    // Query 2: Overlap at local position (2, 0) -> must find nothing
    hits.clear();
    scene.OverlapCircleXZ(glm::vec2{ 2.0f, 0.0f }, 1.0f, Engine::PhysicsLayer::Enemy, hits);
    EXPECT_EQ(hits.size(), 0u);

    // Query 3: OverlapArcXZ facing +X from (10, 10) towards child at (14, 10) -> must hit
    hits.clear();
    scene.OverlapArcXZ(glm::vec2{ 10.0f, 10.0f }, glm::vec2{ 1.0f, 0.0f }, 6.0f, 45.0f, Engine::PhysicsLayer::Enemy, hits);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], child);
}

// Test 10: Non-runtime RaycastClosestXZ handles ray containment, CapsuleCollider2D, and Tilemap
TEST(SpatialQueriesAndStasisTest, PhysicsQueries_NonRuntimeRaycastContainmentAndShapes) {
    Engine::Scene scene("NonRuntimeRaycastScene");
    EXPECT_FALSE(scene.IsRuntimeRunning());

    // 1. Circle containment: ray starts inside CircleCollider2D
    auto* circleGo = scene.CreateGameObject("CircleEntity");
    circleGo->GetTransform()->position = { 0.0f, 0.0f, 2.0f };
    auto* circleCol = circleGo->AddComponent<Engine::CircleCollider2D>();
    circleCol->Radius = 1.0f;
    circleCol->CategoryBits = Engine::PhysicsLayer::Enemy;

    Engine::RaycastHitXZ hit;
    // Cast ray from (0, 2) [inside circle] towards (0, 10)
    bool hasHit = scene.RaycastClosestXZ(glm::vec2{ 0.0f, 2.0f }, glm::vec2{ 0.0f, 10.0f }, Engine::PhysicsLayer::Enemy, hit);
    EXPECT_TRUE(hasHit);
    EXPECT_EQ(hit.Entity, circleGo);
    EXPECT_FLOAT_EQ(hit.Fraction, 0.0f);
    EXPECT_FLOAT_EQ(hit.PointXZ.x, 0.0f);
    EXPECT_FLOAT_EQ(hit.PointXZ.y, 2.0f);

    // 2. CapsuleCollider2D raycast
    auto* capGo = scene.CreateGameObject("CapsuleEntity");
    capGo->GetTransform()->position = { 10.0f, 0.0f, 5.0f };
    auto* capCol = capGo->AddComponent<Engine::CapsuleCollider2D>();
    capCol->Center1 = { 0.0f, -1.0f };
    capCol->Center2 = { 0.0f, 1.0f };
    capCol->Radius = 0.5f;
    capCol->CategoryBits = Engine::PhysicsLayer::Enemy;

    // Raycast from (5, 5) towards (15, 5) -> intersects capsule along horizontal ray at X approx 9.5
    hasHit = scene.RaycastClosestXZ(glm::vec2{ 5.0f, 5.0f }, glm::vec2{ 15.0f, 5.0f }, Engine::PhysicsLayer::Enemy, hit);
    EXPECT_TRUE(hasHit);
    EXPECT_EQ(hit.Entity, capGo);
    EXPECT_NEAR(hit.PointXZ.x, 9.5f, 0.05f);
    EXPECT_NEAR(hit.PointXZ.y, 5.0f, 0.05f);

    // 3. Tilemap with non-walkable tile
    auto* tmGo = scene.CreateGameObject("TilemapEntity");
    tmGo->GetTransform()->position = { 20.0f, 0.0f, 0.0f };
    auto* tm = tmGo->AddComponent<Engine::Tilemap>(4u, 4u);
    // Mark tile (1, 1) as non-walkable (centered at 20 + 1 = 21, 0 + 1 = 1)
    tm->SetTile(1, 1, 1, false);

    // Raycast from (21, -2) towards (21, 5) with Environment layer -> should hit tile (1, 1) at Z approx 0.5
    hasHit = scene.RaycastClosestXZ(glm::vec2{ 21.0f, -2.0f }, glm::vec2{ 21.0f, 5.0f }, Engine::PhysicsLayer::Environment, hit);
    EXPECT_TRUE(hasHit);
    EXPECT_EQ(hit.Entity, tmGo);
    EXPECT_NEAR(hit.PointXZ.x, 21.0f, 0.05f);
    EXPECT_NEAR(hit.PointXZ.y, 0.5f, 0.05f);
}

TEST(SpatialQueriesAndStasisTest, OverlapCircleXZ_ZeroAllocationDedupBelowAndAbove32) {
    Engine::Scene scene("OverlapDedupScaleScene");

    // 45 entities clustered near origin, each with TWO shapes (Box + Circle) to stress multi-shape dedup
    constexpr int kEntityCount = 45;
    for (int i = 0; i < kEntityCount; ++i) {
        auto* go = scene.CreateGameObject("ClusterTarget");
        // First 10 tightly inside radius 1.5m; remaining 35 inside radius 8.0m
        const float dist = (i < 10) ? (0.1f * static_cast<float>(i)) : (2.0f + 0.1f * static_cast<float>(i));
        go->GetTransform()->position = { dist, 0.0f, 0.0f };
        auto* rb = go->AddComponent<Engine::Rigidbody2D>();
        rb->Type = Engine::Rigidbody2D::BodyType::Static;
        auto* box = go->AddComponent<Engine::BoxCollider2D>();
        box->size = { 0.4f, 0.4f };
        box->CategoryBits = Engine::PhysicsLayer::Enemy;
        auto* circle = go->AddComponent<Engine::CircleCollider2D>();
        circle->Radius = 0.25f;
        circle->CategoryBits = Engine::PhysicsLayer::Enemy;
    }

    scene.OnRuntimeStart();

    std::vector<Engine::GameObject*> hits;
    hits.reserve(64);
    const auto* initialDataPtr = hits.data();
    const size_t initialCap = hits.capacity();

    // 1. Small query (<= 32 hits): linear scan dedup returns strictly unique entities with zero reallocation
    scene.OverlapCircleXZ(glm::vec2{ 0.0f, 0.0f }, 1.5f, Engine::PhysicsLayer::Enemy, hits);
    EXPECT_EQ(hits.size(), 10u);
    EXPECT_EQ(hits.data(), initialDataPtr);
    EXPECT_EQ(hits.capacity(), initialCap);

    // 2. Overflow query (> 32 hits): transitions seamlessly to thread-local overflow set & dedups all 45 multi-shape bodies
    scene.OverlapCircleXZ(glm::vec2{ 0.0f, 0.0f }, 10.0f, Engine::PhysicsLayer::Enemy, hits);
    EXPECT_EQ(hits.size(), static_cast<size_t>(kEntityCount));
    EXPECT_EQ(hits.data(), initialDataPtr);
    EXPECT_EQ(hits.capacity(), initialCap);

    scene.OnRuntimeStop();
}

TEST(SpatialQueriesAndStasisTest, OverlapArcXZ_InPlaceFilteringReusesCallerBuffer) {
    Engine::Scene scene("OverlapArcInPlaceScene");

    // Front target (+Z), Coincident target (0,0), Rear target (-Z)
    auto* front = scene.CreateGameObject("FrontTarget");
    front->GetTransform()->position = { 0.0f, 0.0f, 2.0f };
    front->AddComponent<Engine::CircleCollider2D>()->CategoryBits = Engine::PhysicsLayer::Enemy;

    auto* coincident = scene.CreateGameObject("CoincidentTarget");
    coincident->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    coincident->AddComponent<Engine::CircleCollider2D>()->CategoryBits = Engine::PhysicsLayer::Enemy;

    auto* rear = scene.CreateGameObject("RearTarget");
    rear->GetTransform()->position = { 0.0f, 0.0f, -2.0f };
    rear->AddComponent<Engine::CircleCollider2D>()->CategoryBits = Engine::PhysicsLayer::Enemy;

    scene.OnRuntimeStart();

    std::vector<Engine::GameObject*> hits;
    hits.reserve(32);
    const auto* initialDataPtr = hits.data();
    const size_t initialCap = hits.capacity();

    scene.OverlapArcXZ(glm::vec2{ 0.0f, 0.0f }, glm::vec2{ 0.0f, 1.0f }, 5.0f, 45.0f, Engine::PhysicsLayer::Enemy, hits);

    EXPECT_EQ(hits.data(), initialDataPtr);
    EXPECT_EQ(hits.capacity(), initialCap);
    EXPECT_EQ(hits.size(), 2u);

    const bool hasFront = std::find(hits.begin(), hits.end(), front) != hits.end();
    const bool hasCoincident = std::find(hits.begin(), hits.end(), coincident) != hits.end();
    const bool hasRear = std::find(hits.begin(), hits.end(), rear) != hits.end();
    EXPECT_TRUE(hasFront);
    EXPECT_TRUE(hasCoincident);
    EXPECT_FALSE(hasRear);

    scene.OnRuntimeStop();
}

} // namespace
