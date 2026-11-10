#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"

namespace {

TEST(BoxCollider2DTest, DefaultValuesAndMaterials) {
    Engine::BoxCollider2D collider;
    EXPECT_EQ(collider.size, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(collider.offset, glm::vec2(0.0f, 0.0f));
    EXPECT_FLOAT_EQ(collider.Density, 1.0f);
    EXPECT_FLOAT_EQ(collider.Friction, 0.5f);
    EXPECT_FLOAT_EQ(collider.Restitution, 0.0f);
    EXPECT_FLOAT_EQ(collider.RestitutionThreshold, 0.5f);
    EXPECT_FALSE(collider.IsSensor);
    EXPECT_TRUE(collider.EnableHitEvents);
    EXPECT_EQ(collider.CategoryBits, Engine::PhysicsLayer::Default);
    EXPECT_EQ(collider.MaskBits, Engine::PhysicsLayer::All);

    // Verify Rigidbody2D vertical grounding checks
    Engine::Rigidbody2D rb;
    rb.GroundLevelY = 0.0f;
    EXPECT_TRUE(rb.IsGrounded(0.0f));
    EXPECT_FALSE(rb.IsGrounded(1.0f));

    // If moving upward, it's airborne even if at ground level
    rb.VerticalVelocity = 2.0f;
    EXPECT_FALSE(rb.IsGrounded(0.0f));
}

TEST(Rigidbody2DTest, DefaultsAndType) {
    Engine::Rigidbody2D rb;
    EXPECT_EQ(rb.Type, Engine::Rigidbody2D::BodyType::Dynamic);
    EXPECT_TRUE(rb.FixedRotation);
    EXPECT_FLOAT_EQ(rb.LinearDamping, 4.5f);
    EXPECT_FLOAT_EQ(rb.AngularDamping, 6.0f);
    EXPECT_FLOAT_EQ(rb.GravityScaleY, 1.0f);
    EXPECT_FLOAT_EQ(rb.MassMultiplier, 1.0f);
    EXPECT_FALSE(rb.IsBullet);
    EXPECT_EQ(rb.RuntimeBody, nullptr);
}

TEST(IsometricPhysicsTest, JumpArcRisesAndReturnsToGroundLevel) {
    Engine::Scene scene("GravityScene");
    auto* player = scene.CreateGameObject("Player");
    auto* transform = player->GetTransform();
    transform->position = {2.0f, 0.0f, 2.0f};

    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->UseGravityY = true;
    rb->GravityY = -10.0f;
    rb->GravityScaleY = 1.0f;
    rb->GroundLevelY = 0.0f;

    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    EXPECT_TRUE(rb->IsGrounded(transform->position.y));

    // Apply upward jump impulse
    rb->VerticalVelocity = 5.0f;
    scene.Update(0.1f);
    EXPECT_GT(transform->position.y, 0.0f);
    EXPECT_FALSE(rb->IsGrounded(transform->position.y));

    // Simulate enough frames for the jump arc to complete and clamp to groundLevel
    for (int step = 0; step < 30; ++step) {
        scene.Update(0.05f);
    }

    EXPECT_FLOAT_EQ(transform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(rb->VerticalVelocity, 0.0f);
    EXPECT_TRUE(rb->IsGrounded(transform->position.y));
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
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = false;
    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->size = {0.6f, 0.6f};

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
    ground->GetTransform()->position = { 0.0f, 0.0f, -10.0f };
    auto* groundRb = ground->AddComponent<Engine::Rigidbody2D>();
    groundRb->Type = Engine::Rigidbody2D::BodyType::Static;
    auto* groundBox = ground->AddComponent<Engine::BoxCollider2D>();
    groundBox->size = { 10.0f, 10.0f };

    auto* ball = scene.CreateGameObject("Ball");
    ball->GetTransform()->position = { 0.0f, 10.0f, 0.0f };
    auto* ballRb = ball->AddComponent<Engine::Rigidbody2D>();
    ballRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    ballRb->FixedRotation = true;
    ballRb->InitialLinearVelocityXZ = { 2.0f, 3.0f };
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

    // Ball should have fallen downward under vertical gravity and translated across XZ
    EXPECT_LT(ball->GetTransform()->position.y, initialY);
    EXPECT_GT(ball->GetTransform()->position.x, 0.0f);
    EXPECT_GT(ball->GetTransform()->position.z, 0.0f);

    scene.OnRuntimeStop();
    EXPECT_EQ(ballRb->RuntimeBody, nullptr);
    EXPECT_EQ(groundRb->RuntimeBody, nullptr);
    EXPECT_EQ(ballBox->RuntimeFixture, nullptr);
}

TEST(Box2DPhysicsTest, MultithreadedStepWithWorkerPool) {
    // 1. Initialize JobSystem with 4 worker threads
    Engine::JobSystem::Shutdown();
    Engine::JobSystem::Init(4);
    EXPECT_TRUE(Engine::JobSystem::IsInitialized());
    EXPECT_EQ(Engine::JobSystem::GetWorkerCount(), 4u);

    {
        Engine::Scene scene("MultithreadedPhysicsScene");

        // Static ground floor
        auto* ground = scene.CreateGameObject("Ground");
        ground->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
        auto* groundRb = ground->AddComponent<Engine::Rigidbody2D>();
        groundRb->Type = Engine::Rigidbody2D::BodyType::Static;
        auto* groundBox = ground->AddComponent<Engine::BoxCollider2D>();
        groundBox->size = { 100.0f, 100.0f };

        // Create 40 dynamic bodies across different columns and heights to produce multiple islands
        constexpr int kNumBodies = 40;
        std::vector<Engine::GameObject*> bodies;
        bodies.reserve(kNumBodies);
        std::vector<float> initialY(kNumBodies);

        for (int i = 0; i < kNumBodies; ++i) {
            auto* ball = scene.CreateGameObject("DynamicBody_" + std::to_string(i));
            const float x = -30.0f + static_cast<float>(i) * 1.5f;
            const float y = 10.0f + static_cast<float>(i % 5) * 3.0f;
            const float z = -10.0f + static_cast<float>(i % 7) * 2.5f;
            ball->GetTransform()->position = { x, y, z };
            initialY[i] = y;

            auto* rb = ball->AddComponent<Engine::Rigidbody2D>();
            rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
            rb->FixedRotation = false;
            rb->InitialLinearVelocityXZ = { 1.0f, 1.5f };

            auto* box = ball->AddComponent<Engine::BoxCollider2D>();
            box->size = { 0.8f, 0.8f };
            box->density = 1.0f;
            box->friction = 0.3f;
            box->restitution = 0.2f;

            bodies.push_back(ball);
        }

        scene.OnRuntimeStart();

        for (int step = 0; step < 30; ++step) {
            scene.UpdateRuntime(0.016f);
        }

        // All dynamic bodies should have accelerated and fallen downward under gravity
        for (int i = 0; i < kNumBodies; ++i) {
            const float currentY = bodies[i]->GetTransform()->position.y;
            EXPECT_LT(currentY, initialY[i]);
            EXPECT_FALSE(std::isnan(currentY));
            EXPECT_FALSE(std::isnan(bodies[i]->GetTransform()->position.x));
        }

        // Box2D should have invoked our multithreaded task callbacks during simulation steps
        EXPECT_GT(scene.GetBox2DTotalTasks(), 0u);

        scene.OnRuntimeStop();
    }

    Engine::JobSystem::Shutdown();
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());
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
    wallCollider->size = { 1.0f, 1.0f };

    // Dynamic player at (1.4, 0.0, 2.0), size (1.0, 1.0) -> right edge 1.9 penetrates wall left edge 1.5
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 1.4f, 0.0f, 2.0f };
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = false;
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
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
    wallCollider->size = { 1.0f, 1.0f };

    // Dynamic player at origin (0.0, 0.0, 0.0)
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 0.0f, 0.0f, 0.0f };
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = false;
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
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
    wallCollider->size = { 4.0f, 1.0f };

    // Dynamic player at (0.1, 0.0, 0.4), size (0.6, 0.6) -> overlaps the wall in multiple cells
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 0.1f, 0.0f, 0.4f };
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = false;
    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
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

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1);

    // Adding another GameObject invalidates the grid
    auto* wall2 = scene.CreateGameObject("Wall2");
    wall2->GetTransform()->position = { 6.0f, 0.0f, 6.0f };
    auto* col2 = wall2->AddComponent<Engine::BoxCollider2D>();

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

TEST(SpatialGrid2DTest, NonPhysicsAndDynamicEntitiesNeverDirtyStaticGrid) {
    class VisualDummyComponent : public Engine::Component {};

    Engine::Scene scene("StaticDirtyHeuristicScene");

    // Seed an initial static wall and settle the grid
    auto* wall = scene.CreateGameObject("Wall");
    wall->GetTransform()->position = { 2.0f, 0.0f, 2.0f };
    wall->AddComponent<Engine::BoxCollider2D>();

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1u);

    // 1. Direct non-physics spawn & component attachment keep grid clean
    auto* visual = scene.CreateGameObject("VisualProp");
    visual->AddComponent<VisualDummyComponent>();
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // 2. Direct dynamic physics entity spawn keeps static grid clean
    auto* dynActor = scene.CreateGameObject("DynamicActor");
    auto* dynRb = dynActor->AddComponent<Engine::Rigidbody2D>();
    dynRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    dynActor->AddComponent<Engine::BoxCollider2D>();
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // 3. Deferred spawns & destroys inside iteration for visual + dynamic entities keep grid clean
    Engine::GameObject* deferredVisual = nullptr;
    Engine::GameObject* deferredDyn = nullptr;
    {
        Engine::Scene::ScopedIterationGuard guard(scene);

        deferredVisual = scene.CreateGameObject("DeferredVisual");
        deferredVisual->AddComponent<VisualDummyComponent>();

        // Even adding BoxCollider2D before Dynamic Rigidbody2D must stay clean once committed at flush
        deferredDyn = scene.CreateGameObject("DeferredDynamic");
        deferredDyn->AddComponent<Engine::BoxCollider2D>();
        auto* dRb = deferredDyn->AddComponent<Engine::Rigidbody2D>();
        dRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;

        scene.DestroyGameObject(visual);
        scene.DestroyGameObject(dynActor);
        EXPECT_FALSE(scene.IsSpatialGridDirty());
    }
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        scene.DestroyGameObject(deferredVisual);
        scene.DestroyGameObject(deferredDyn);
    }
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // 4. Deferred static collider spawn marks dirty ONLY after FlushEntityLifecycleQueues commits it
    Engine::GameObject* deferredWall = nullptr;
    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        deferredWall = scene.CreateGameObject("DeferredWall");
        deferredWall->GetTransform()->position = { 8.0f, 0.0f, 8.0f };
        deferredWall->AddComponent<Engine::BoxCollider2D>();
        EXPECT_FALSE(scene.IsSpatialGridDirty());
    }
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 2u);

    // 5. Same-frame spawn + destroy of a static collider never enters grid -> keeps grid clean
    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        auto* transientWall = scene.CreateGameObject("TransientWall");
        transientWall->AddComponent<Engine::BoxCollider2D>();
        scene.DestroyGameObject(transientWall);
    }
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // 6. Destroying an alive static wall or spawning a Tilemap invalidates the grid
    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        scene.DestroyGameObject(deferredWall);
    }
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1u);

    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        auto* mapObj = scene.CreateGameObject("DeferredMap");
        mapObj->AddComponent<Engine::Tilemap>(4u, 4u);
    }
    EXPECT_TRUE(scene.IsSpatialGridDirty());
}

TEST(SpatialGrid2DTest, StaticColliderAndTilemapMovementInvalidatesGrid) {
    class VisualDummyComponent : public Engine::Component {};

    Engine::Scene scene("StaticMovementInvalidationScene");

    auto* wall = scene.CreateGameObject("StaticWall");
    wall->GetTransform()->position = { 2.0f, 0.0f, 2.0f };
    auto* wallCol = wall->AddComponent<Engine::BoxCollider2D>();
    wallCol->size = { 1.0f, 1.0f };

    auto* mapObj = scene.CreateGameObject("Map");
    mapObj->AddComponent<Engine::Tilemap>(4u, 4u);

    auto* visual = scene.CreateGameObject("Visual");
    visual->AddComponent<VisualDummyComponent>();

    auto* dyn = scene.CreateGameObject("DynamicMover");
    auto* dynRb = dyn->AddComponent<Engine::Rigidbody2D>();
    dynRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    dynRb->UseGravityY = false;
    auto* dynCol = dyn->AddComponent<Engine::BoxCollider2D>();
    dynCol->size = { 1.0f, 1.0f };

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // Moving visual or dynamic entities must never invalidate the static spatial grid
    visual->GetTransform()->Translate({ 5.0f, 0.0f, 0.0f });
    dyn->GetTransform()->SetWorldPosition({ -4.0f, 0.0f, -4.0f });
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // Explicit transform movement on static BoxCollider2D marks dirty immediately
    wall->GetTransform()->Translate({ 2.0f, 0.0f, 0.0f });
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // Explicit transform movement on Tilemap marks dirty immediately
    mapObj->GetTransform()->SetWorldPosition({ 10.0f, 0.0f, 10.0f });
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    // Ancestor movement propagates dirty to static collider descendants
    auto* parentGo = scene.CreateGameObject("WallParent");
    wall->GetTransform()->SetParent(parentGo->GetTransform(), false);
    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    parentGo->GetTransform()->Translate({ 1.0f, 0.0f, 0.0f });
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    // Unparent and verify direct public field mutation (.position = ...) rebuilds grid in Update()
    wall->GetTransform()->SetParent(nullptr, false);
    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());

    wall->GetTransform()->position = { 20.0f, 0.0f, 20.0f };
    dyn->GetTransform()->position = { 19.4f, 0.0f, 20.0f };
    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_LE(dyn->GetTransform()->position.x, 19.0001f);
}

} // namespace

