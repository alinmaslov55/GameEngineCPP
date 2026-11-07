#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include <box2d/box2d.h>

namespace {

TEST(GroundPlanePhysicsTest, XZGroundPlaneTranslationAndFloorClamping) {
    Engine::Scene scene("XZTranslationScene");

    auto* bodyObj = scene.CreateGameObject("Brawler");
    auto* transform = bodyObj->GetTransform();
    transform->position = { 2.0f, 0.0f, 5.0f };

    auto* rb2d = bodyObj->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->GroundLevelY = 0.0f;
    rb2d->UseGravityY = true;

    auto* col = bodyObj->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    scene.OnRuntimeStart();
    EXPECT_TRUE(scene.IsRuntimeRunning());

    // Apply +Z velocity along the top-down ground plane
    rb2d->SetLinearVelocityXZ(glm::vec2{ 0.0f, 5.0f });

    // Step 60 fixed physics frames (1.0 second)
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Body should translate forward along +Z, preserve X = 2.0f, and remain pinned to Y = 0.0f (no falling through floor)
    EXPECT_GT(transform->position.z, 5.0f);
    EXPECT_NEAR(transform->position.x, 2.0f, 1e-4f);
    EXPECT_FLOAT_EQ(transform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(rb2d->VerticalVelocity, 0.0f);
    EXPECT_FALSE(rb2d->IsAirborne());
    EXPECT_TRUE(rb2d->IsGrounded());

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, VerticalYArcDecoupledElevationAndRestitutionBounces) {
    Engine::Scene scene("DecoupledElevationScene");

    auto* logPickup = scene.CreateGameObject("WoodLog");
    auto* transform = logPickup->GetTransform();
    transform->position = { 0.0f, 0.0f, 0.0f };

    auto* rb2d = logPickup->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->GroundLevelY = 0.0f;
    rb2d->GravityY = -22.0f;
    rb2d->GroundRestitutionY = 0.45f;
    rb2d->UseGravityY = true;

    scene.OnRuntimeStart();

    // Launch vertically with upward impulse
    rb2d->LaunchVertical(10.0f);
    EXPECT_TRUE(rb2d->IsAirborne());

    float maxApex1 = 0.0f;
    float maxApex2 = 0.0f;
    bool hadFirstBounce = false;

    constexpr float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 180; ++frame) {
        scene.UpdateRuntime(dt);

        const float y = transform->position.y;
        if (!hadFirstBounce) {
            if (y > maxApex1) {
                maxApex1 = y;
            }
            if (y <= 0.0f && frame > 5) {
                hadFirstBounce = true;
            }
        } else {
            if (y > maxApex2) {
                maxApex2 = y;
            }
        }
    }

    // Apex 1 should reach approximately v^2 / (2*g) = 100 / 44 ≈ 2.27m
    EXPECT_GT(maxApex1, 1.8f);
    EXPECT_LT(maxApex1, 2.6f);

    // Second bounce apex must be strictly lower than the first apex due to 0.45 restitution
    EXPECT_GT(maxApex2, 0.1f);
    EXPECT_LT(maxApex2, maxApex1 * 0.45f);

    // By frame 180, the body must have fully settled on the ground
    EXPECT_FLOAT_EQ(transform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(rb2d->VerticalVelocity, 0.0f);
    EXPECT_FALSE(rb2d->IsAirborne());
    EXPECT_TRUE(rb2d->IsGrounded());

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, StandeeFixedRotationMaintainsYawZero) {
    Engine::Scene scene("FixedRotationScene");

    auto* standee = scene.CreateGameObject("HeroStandee");
    auto* transform = standee->GetTransform();
    transform->position = { 0.0f, 0.0f, 0.0f };
    transform->rotation = { 0.0f, 0.0f, 0.0f };

    auto* rb2d = standee->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;

    scene.OnRuntimeStart();

    // Directly set angular velocity in Box2D
    b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody));
    ASSERT_TRUE(b2Body_IsValid(bodyId));
    b2Body_SetAngularVelocity(bodyId, 5.0f);

    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    // FixedRotation must keep Transform ground yaw rotation.y locked at 0.0f
    EXPECT_FLOAT_EQ(transform->rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(transform->rotation.z, 0.0f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, BoxColliderDimensionsScaleByXandZ) {
    Engine::Scene scene("ColliderScalingScene");

    auto* prop = scene.CreateGameObject("LargeProp");
    auto* transform = prop->GetTransform();
    transform->position = { 0.0f, 0.0f, 0.0f };
    transform->scale = { 2.0f, 10.0f, 3.0f }; // Large standee height scale.y = 10.0f

    auto* rb2d = prop->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Static;

    auto* col = prop->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    scene.OnRuntimeStart();

    b2ShapeId shapeId = b2LoadShapeId(reinterpret_cast<uintptr_t>(col->RuntimeFixture));
    ASSERT_TRUE(b2Shape_IsValid(shapeId));

    b2Polygon poly = b2Shape_GetPolygon(shapeId);
    EXPECT_EQ(poly.count, 4);

    // Find min and max bounds of the polygon
    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f; // Box2D Y is World Z
    for (int i = 0; i < poly.count; ++i) {
        minX = std::min(minX, poly.vertices[i].x);
        maxX = std::max(maxX, poly.vertices[i].x);
        minY = std::min(minY, poly.vertices[i].y);
        maxY = std::max(maxY, poly.vertices[i].y);
    }

    // Width should be scale.x * size.x = 2.0 * 1.0 = 2.0 -> half-width 1.0
    EXPECT_NEAR(maxX - minX, 2.0f, 1e-4f);
    // Depth (Box2D Y) should be scale.z * size.y = 3.0 * 1.0 = 3.0 -> half-depth 1.5
    // It must NOT be 10.0 (scale.y)!
    EXPECT_NEAR(maxY - minY, 3.0f, 1e-4f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, TilemapNonWalkableMergedStaticColliders) {
    Engine::Scene scene("TilemapStaticColliderScene");

    auto* mapObj = scene.CreateGameObject("LevelGrid");
    auto* tilemap = mapObj->AddComponent<Engine::Tilemap>(5, 5);
    for (uint32_t z = 0; z < 5; ++z) {
        for (uint32_t x = 0; x < 5; ++x) {
            tilemap->SetTile(x, z, 1, true); // Walkable grass
        }
    }
    // Place a horizontal wall of 3 non-walkable tiles at row Z = 2 (X = [1, 2, 3])
    tilemap->SetTile(1, 2, 2, false);
    tilemap->SetTile(2, 2, 2, false);
    tilemap->SetTile(3, 2, 2, false);

    // Player starts at (2.0, 0.0, 0.5) moving towards Z = 2.0
    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 2.0f, 0.0f, 0.5f };

    auto* rb2d = player->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->InitialLinearVelocityXZ = { 0.0f, 4.0f };

    auto* col = player->AddComponent<Engine::BoxCollider2D>();
    col->size = { 0.6f, 0.6f }; // Half-depth 0.3

    scene.OnRuntimeStart();
    EXPECT_NE(tilemap->RuntimeBody, nullptr);

    // Step 60 frames (1.0 second)
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    // Player should have been stopped by the merged Box2D static wall at Z = 2.0
    // Wall left edge along Z is 2.0 - 0.5 = 1.5. Player front edge is pos.z + 0.3.
    // Therefore player center pos.z must not penetrate past ~1.20
    EXPECT_LT(playerTransform->position.z, 1.25f);
    EXPECT_GT(playerTransform->position.z, 0.5f);

    scene.OnRuntimeStop();
    EXPECT_EQ(tilemap->RuntimeBody, nullptr);
}

TEST(GroundPlanePhysicsTest, SolverGatingSkipsAdHocSolversDuringRuntime) {
    Engine::Scene scene("SolverGatingScene");

    auto* wall = scene.CreateGameObject("Wall");
    wall->GetTransform()->position = { 2.0f, 0.0f, 2.0f };
    auto* wallCol = wall->AddComponent<Engine::BoxCollider2D>();
    wallCol->isDynamic = false;
    wallCol->size = { 1.0f, 1.0f };

    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 1.4f, 0.0f, 2.0f };
    auto* playerCol = player->AddComponent<Engine::BoxCollider2D>();
    playerCol->isDynamic = true;
    playerCol->useGravity = false;
    playerCol->size = { 1.0f, 1.0f };

    // In Edit mode (!m_IsRuntimeRunning), Scene::Update executes spatial grid resolution
    EXPECT_TRUE(scene.IsSpatialGridDirty());
    scene.Update(0.016f);
    EXPECT_FALSE(scene.IsSpatialGridDirty());
    EXPECT_EQ(scene.GetStaticColliderCount(), 1);

    // Now start runtime
    scene.OnRuntimeStart();
    EXPECT_TRUE(scene.IsRuntimeRunning());

    // In Runtime mode, Scene::Update skips spatial grid rebuilds and ad-hoc resolution
    scene.InvalidateSpatialGrid();
    EXPECT_TRUE(scene.IsSpatialGridDirty());
    scene.Update(0.016f);
    // Since solvers are gated out during runtime, the spatial grid remains untouched (dirty)
    EXPECT_TRUE(scene.IsSpatialGridDirty());

    scene.OnRuntimeStop();
}

} // namespace
