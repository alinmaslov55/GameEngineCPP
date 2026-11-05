#include <gtest/gtest.h>
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"

namespace {

TEST(BoxCollider2DTest, DefaultValuesAndGroundedCheck) {
    Engine::BoxCollider2D collider;
    EXPECT_EQ(collider.size, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(collider.offset, glm::vec2(0.0f, 0.0f));
    EXPECT_FALSE(collider.isDynamic);
    EXPECT_FLOAT_EQ(collider.density, 1.0f);
    EXPECT_FLOAT_EQ(collider.friction, 0.5f);
    EXPECT_FLOAT_EQ(collider.restitution, 0.0f);
    EXPECT_FLOAT_EQ(collider.groundLevel, 0.0f);
    EXPECT_FLOAT_EQ(collider.verticalVelocity, 0.0f);
    EXPECT_TRUE(collider.IsGrounded(0.0f));
    EXPECT_FALSE(collider.IsGrounded(1.0f));

    // If moving upward, it's not grounded even if at ground level
    collider.verticalVelocity = 2.0f;
    EXPECT_FALSE(collider.IsGrounded(0.0f));
}

TEST(Rigidbody2DTest, DefaultsAndType) {
    Engine::Rigidbody2D rb;
    EXPECT_EQ(rb.Type, Engine::Rigidbody2D::BodyType::Dynamic);
    EXPECT_FALSE(rb.FixedRotation);
    EXPECT_EQ(rb.RuntimeBody, nullptr);
}

TEST(IsometricPhysicsTest, JumpArcRisesAndReturnsToGroundLevel) {
    Engine::Scene scene("GravityScene");
    auto* player = scene.CreateGameObject("Player");
    auto* transform = player->GetTransform();
    transform->position = {2.0f, 0.0f, 2.0f};

    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->useGravity = true;
    collider->gravity = -10.0f;
    collider->groundLevel = 0.0f;

    EXPECT_TRUE(collider->IsGrounded(transform->position.y));

    // Apply upward jump impulse
    collider->verticalVelocity = 5.0f;
    scene.Update(0.1f);
    EXPECT_GT(transform->position.y, 0.0f);
    EXPECT_FALSE(collider->IsGrounded(transform->position.y));

    // Simulate enough frames for the jump arc to complete and clamp to groundLevel
    for (int step = 0; step < 30; ++step) {
        scene.Update(0.05f);
    }

    EXPECT_FLOAT_EQ(transform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(collider->verticalVelocity, 0.0f);
    EXPECT_TRUE(collider->IsGrounded(transform->position.y));
}

TEST(IsometricPhysicsTest, NonWalkableTileBlocksDynamicColliderWhileWalkableAllowsPassage) {
    Engine::Scene scene("WalkabilityScene");

    auto* mapObj = scene.CreateGameObject("LevelGrid");
    auto* tilemap = mapObj->AddComponent<Engine::Tilemap>(5, 5);
    for (uint32_t z = 0; z < 5; ++z) {
        for (uint32_t x = 0; x < 5; ++x) {
            tilemap->SetTile(x, z, 1, true); // Walkable grass
        }
    }
    // Place an impassable wall tile at (2, 2), spanning X=[1.5..2.5], Z=[1.5..2.5]
    tilemap->SetTile(2, 2, 2, false);

    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->size = {0.6f, 0.6f};
    collider->useGravity = false;

    // 1. Walkable tile at (1, 2): player stays unimpeded
    playerTransform->position = {1.0f, 0.0f, 2.0f};
    scene.Update(0.016f);
    EXPECT_NEAR(playerTransform->position.x, 1.0f, 1e-5f);
    EXPECT_NEAR(playerTransform->position.z, 2.0f, 1e-5f);

    // 2. Move into non-walkable wall at (2, 2) from the left (X = 1.4 -> right edge 1.7 overlaps wall left edge 1.5)
    playerTransform->position = {1.4f, 0.0f, 2.0f};
    scene.Update(0.016f);

    // Player right bound (pos.x + 0.3) must be pushed out to <= wall left bound (1.5)
    EXPECT_NEAR(playerTransform->position.x, 1.2f, 1e-4f);
    EXPECT_NEAR(playerTransform->position.z, 2.0f, 1e-4f);
}

TEST(Box2DPhysicsTest, DynamicBodyFallsUnderGravityDuringRuntime) {
    Engine::Scene scene("Box2DScene");

    auto* ground = scene.CreateGameObject("Ground");
    ground->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* groundRb = ground->AddComponent<Engine::Rigidbody2D>();
    groundRb->Type = Engine::Rigidbody2D::BodyType::Static;
    auto* groundBox = ground->AddComponent<Engine::BoxCollider2D>();
    groundBox->size = { 10.0f, 1.0f };

    auto* ball = scene.CreateGameObject("Ball");
    ball->GetTransform()->position = { 0.0f, 10.0f, 0.0f };
    auto* ballRb = ball->AddComponent<Engine::Rigidbody2D>();
    ballRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    ballRb->FixedRotation = true;
    auto* ballBox = ball->AddComponent<Engine::BoxCollider2D>();
    ballBox->size = { 1.0f, 1.0f };
    ballBox->density = 1.0f;
    ballBox->restitution = 0.5f;

    // Prior to OnRuntimeStart, Box2D body handles are nullptr
    EXPECT_EQ(ballRb->RuntimeBody, nullptr);
    EXPECT_EQ(groundRb->RuntimeBody, nullptr);

    scene.OnRuntimeStart();
    EXPECT_NE(ballRb->RuntimeBody, nullptr);
    EXPECT_NE(groundRb->RuntimeBody, nullptr);
    EXPECT_NE(ballBox->RuntimeFixture, nullptr);

    const float initialY = ball->GetTransform()->position.y;
    EXPECT_FLOAT_EQ(initialY, 10.0f);

    // Step the physics simulation
    for (int step = 0; step < 20; ++step) {
        scene.UpdateRuntime(0.016f);
    }

    // Ball should have accelerated and fallen downward
    EXPECT_LT(ball->GetTransform()->position.y, initialY);

    scene.OnRuntimeStop();
    EXPECT_EQ(ballRb->RuntimeBody, nullptr);
    EXPECT_EQ(groundRb->RuntimeBody, nullptr);
    EXPECT_EQ(ballBox->RuntimeFixture, nullptr);
}

TEST(SpatialGrid2DTest, CellKeyGeneration) {
    uint64_t keyOrigin = Engine::Scene::GetCellKey(0, 0);
    uint64_t keyNeg = Engine::Scene::GetCellKey(-1, -1);
    uint64_t keyPos = Engine::Scene::GetCellKey(10, 20);

    EXPECT_NE(keyOrigin, keyNeg);
    EXPECT_NE(keyOrigin, keyPos);
    EXPECT_NE(keyNeg, keyPos);

    EXPECT_EQ(Engine::Scene::GetCellKey(5, -7), Engine::Scene::GetCellKey(5, -7));
}

TEST(SpatialGrid2DTest, DynamicVsStaticCollisionResolution) {
    Engine::Scene scene("SpatialCollisionScene");

    // Static obstacle at (2.0, 0.0, 2.0), size (1.0, 1.0) -> bounds X: [1.5..2.5], Z: [1.5..2.5]
    auto* wall = scene.CreateGameObject("Wall");
    wall->GetTransform()->position = { 2.0f, 0.0f, 2.0f };
    auto* wallCollider = wall->AddComponent<Engine::BoxCollider2D>();
    wallCollider->isDynamic = false;
    wallCollider->size = { 1.0f, 1.0f };

    // Dynamic player at (1.4, 0.0, 2.0), size (1.0, 1.0) -> right edge 1.9 penetrates wall left edge 1.5
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 1.4f, 0.0f, 2.0f };
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->isDynamic = true;
    playerCollider->useGravity = false;
    playerCollider->size = { 1.0f, 1.0f };

    EXPECT_TRUE(scene.IsSpatialGridDirty());
    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1);
    EXPECT_GT(scene.GetStaticGridCellCount(), 0);

    // Player should be pushed to the left along X so player right bound <= wall left bound (1.5)
    // player center.x + 0.5 <= 1.5 -> center.x <= 1.0
    EXPECT_LE(playerTransform->position.x, 1.0001f);
    EXPECT_NEAR(playerTransform->position.z, 2.0f, 1e-4f);
    // Wall must not move
    EXPECT_FLOAT_EQ(wall->GetTransform()->position.x, 2.0f);
    EXPECT_FLOAT_EQ(wall->GetTransform()->position.z, 2.0f);
}

TEST(SpatialGrid2DTest, StaticColliderInDifferentCellDoesNotInterfere) {
    Engine::Scene scene("SpatialIsolationScene");

    // Static obstacle placed far away at (50.0, 0.0, 50.0)
    auto* farWall = scene.CreateGameObject("FarWall");
    farWall->GetTransform()->position = { 50.0f, 0.0f, 50.0f };
    auto* wallCollider = farWall->AddComponent<Engine::BoxCollider2D>();
    wallCollider->isDynamic = false;
    wallCollider->size = { 1.0f, 1.0f };

    // Dynamic player at origin (0.0, 0.0, 0.0)
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 0.0f, 0.0f, 0.0f };
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->isDynamic = true;
    playerCollider->useGravity = false;
    playerCollider->size = { 1.0f, 1.0f };

    scene.Update(0.016f);

    // Player position must remain unaffected
    EXPECT_FLOAT_EQ(playerTransform->position.x, 0.0f);
    EXPECT_FLOAT_EQ(playerTransform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(playerTransform->position.z, 0.0f);
}

TEST(SpatialGrid2DTest, MultiCellSpanningStaticColliderDeduplication) {
    Engine::Scene scene("SpanningWallScene");

    // Static wall spanning X = [-2.0..2.0] (width 4.0), Z = [-0.5..0.5] (depth 1.0)
    auto* longWall = scene.CreateGameObject("LongWall");
    longWall->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* wallCollider = longWall->AddComponent<Engine::BoxCollider2D>();
    wallCollider->isDynamic = false;
    wallCollider->size = { 4.0f, 1.0f };

    // Dynamic player at (0.1, 0.0, 0.4), size (0.6, 0.6) -> overlaps the wall in multiple cells
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 0.1f, 0.0f, 0.4f };
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->isDynamic = true;
    playerCollider->useGravity = false;
    playerCollider->size = { 0.6f, 0.6f };

    scene.Update(0.016f);

    // Wall crosses at least 2 spatial cells along X
    EXPECT_GE(scene.GetStaticGridCellCount(), 2);

    // Player should be resolved cleanly without duplicate pushes
    EXPECT_GE(playerTransform->position.z, 0.799f);
}

TEST(SpatialGrid2DTest, GridInvalidationAndDynamicRebuild) {
    Engine::Scene scene("InvalidationScene");

    auto* wall1 = scene.CreateGameObject("Wall1");
    wall1->GetTransform()->position = { 2.0f, 0.0f, 2.0f };
    auto* col1 = wall1->AddComponent<Engine::BoxCollider2D>();
    col1->isDynamic = false;

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1);

    // Adding another GameObject invalidates the grid
    auto* wall2 = scene.CreateGameObject("Wall2");
    wall2->GetTransform()->position = { 6.0f, 0.0f, 6.0f };
    auto* col2 = wall2->AddComponent<Engine::BoxCollider2D>();
    col2->isDynamic = false;

    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 2);

    // Explicit invalidation
    scene.InvalidateSpatialGrid();
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
}

} // namespace

