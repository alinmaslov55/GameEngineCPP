#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsWorld2D.h"
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
    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
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

    b2ShapeId shapeId = b2LoadShapeId(col->RuntimeShape.Value);
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
    wallCol->size = { 1.0f, 1.0f };

    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 1.4f, 0.0f, 2.0f };
    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = false;
    auto* playerCol = player->AddComponent<Engine::BoxCollider2D>();
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

class MockMovementScript : public Engine::Component {
public:
    glm::vec2 DesiredVelocity{ 0.0f, 0.0f };

    void FixedUpdate(float fixedDt) override {
        if (auto* rb = GetGameObject()->GetComponent<Engine::Rigidbody2D>()) {
            rb->SetVelocityXZ(DesiredVelocity);
        }
    }
};

TEST(GroundPlanePhysicsTest, ContinuousRuntimeControllerMovementUpdatesTransformAndStopsCleanly) {
    Engine::Scene scene("ControllerMovementScene");

    auto* player = scene.CreateGameObject("Player");
    auto* transform = player->GetTransform();
    transform->position = { 0.0f, 0.0f, 0.0f };

    auto* rb2d = player->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;

    auto* col = player->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    auto* controller = player->AddComponent<MockMovementScript>();
    controller->DesiredVelocity = { 3.0f, 4.0f }; // speed = 5.0 m/s

    scene.OnRuntimeStart();

    // Step 30 frames (0.5s) at 5.0 m/s -> should move ~2.5m total (X ~ 1.5, Z ~ 2.0)
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    EXPECT_NEAR(transform->position.x, 1.5f, 0.15f);
    EXPECT_NEAR(transform->position.z, 2.0f, 0.15f);

    // Stop moving
    controller->DesiredVelocity = { 0.0f, 0.0f };
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    const float stoppedX = transform->position.x;
    const float stoppedZ = transform->position.z;

    // Advance another 30 frames: should stay in place when stopped
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    EXPECT_FLOAT_EQ(transform->position.x, stoppedX);
    EXPECT_FLOAT_EQ(transform->position.z, stoppedZ);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, ParentedRigidbody2D_SpawnsAtWorldPositionAndRotation) {
    Engine::Scene scene("ParentedSpawnScene");

    auto* parent = scene.CreateGameObject("ParentRig");
    parent->GetTransform()->position = { 10.0f, 2.0f, 5.0f };
    parent->GetTransform()->rotation = { 0.0f, 45.0f, 0.0f };

    auto* child = scene.CreateGameObject("ChildBody");
    child->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    child->GetTransform()->rotation = { 0.0f, 0.0f, 0.0f };
    child->GetTransform()->SetParent(parent->GetTransform(), /*keepWorldTransform=*/false);

    auto* rb2d = child->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = false;

    auto* col = child->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    scene.OnRuntimeStart();
    ASSERT_TRUE(rb2d->RuntimeBody.IsValid());

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    ASSERT_TRUE(b2Body_IsValid(bodyId));

    b2Vec2 pos = b2Body_GetPosition(bodyId);
    b2Rot rot = b2Body_GetRotation(bodyId);

    // Box2D body must spawn at the parented entity's composite WORLD position and rotation
    EXPECT_NEAR(pos.x, 10.0f, 1e-3f);
    EXPECT_NEAR(pos.y, 5.0f, 1e-3f);
    EXPECT_NEAR(glm::degrees(b2Rot_GetAngle(rot)), 45.0f, 1e-2f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, ParentedRigidbody2D_SimulationStepsSyncWorldPositionWithoutCorruptingParent) {
    Engine::Scene scene("ParentedSimScene");

    auto* parent = scene.CreateGameObject("ParentAnchor");
    parent->GetTransform()->position = { 5.0f, 1.0f, 5.0f };

    auto* child = scene.CreateGameObject("ChildRunner");
    child->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    child->GetTransform()->SetParent(parent->GetTransform(), /*keepWorldTransform=*/false);

    auto* rb2d = child->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->FixedRotation = true;
    rb2d->LinearDamping = 0.0f; // Frictionless for clean ballistic distance measurement
    rb2d->InitialLinearVelocityXZ = { 0.0f, 4.0f }; // 4 m/s in +Z

    auto* col = child->AddComponent<Engine::BoxCollider2D>();
    col->size = { 0.5f, 0.5f };

    scene.OnRuntimeStart();

    // Step 30 frames (0.5s) -> should move ~2.0m along +Z in world space
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Parent position must remain untouched
    EXPECT_FLOAT_EQ(parent->GetTransform()->position.x, 5.0f);
    EXPECT_FLOAT_EQ(parent->GetTransform()->position.y, 1.0f);
    EXPECT_FLOAT_EQ(parent->GetTransform()->position.z, 5.0f);

    // Child world position should be ~ (5.0, 1.0, 7.0)
    glm::vec3 childWorldPos = child->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(childWorldPos.x, 5.0f, 0.1f);
    EXPECT_FLOAT_EQ(childWorldPos.y, 1.0f);
    EXPECT_NEAR(childWorldPos.z, 7.0f, 0.2f);

    // Child local position should be relative to parent ~ (0.0, 0.0, 2.0)
    EXPECT_NEAR(child->GetTransform()->position.x, 0.0f, 0.1f);
    EXPECT_FLOAT_EQ(child->GetTransform()->position.y, 0.0f);
    EXPECT_NEAR(child->GetTransform()->position.z, 2.0f, 0.2f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, ParentedRigidbody2D_SetPositionXZRespectsHierarchy) {
    Engine::Scene scene("ParentedSetPosScene");

    auto* parent = scene.CreateGameObject("ParentRig");
    parent->GetTransform()->position = { 10.0f, 3.0f, 20.0f };

    auto* child = scene.CreateGameObject("ChildEntity");
    child->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    child->GetTransform()->SetParent(parent->GetTransform(), /*keepWorldTransform=*/false);

    auto* rb2d = child->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* col = child->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    scene.OnRuntimeStart();

    // Reposition child in world XZ
    rb2d->SetPositionXZ(glm::vec2{ 15.0f, 26.0f });

    // Child world position must reflect requested world position
    glm::vec3 worldPos = child->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(worldPos.x, 15.0f, 1e-4f);
    EXPECT_FLOAT_EQ(worldPos.y, 3.0f); // inherits parent Y
    EXPECT_NEAR(worldPos.z, 26.0f, 1e-4f);

    // Child local position must be correctly inverted relative to parent
    EXPECT_NEAR(child->GetTransform()->position.x, 5.0f, 1e-4f);
    EXPECT_FLOAT_EQ(child->GetTransform()->position.y, 0.0f);
    EXPECT_NEAR(child->GetTransform()->position.z, 6.0f, 1e-4f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, ChildCollider_InheritsParentScale) {
    Engine::Scene scene("ParentedScaleScene");

    auto* parent = scene.CreateGameObject("ParentScaleRig");
    parent->GetTransform()->scale = { 2.0f, 1.0f, 3.0f };

    auto* child = scene.CreateGameObject("ChildBox");
    child->GetTransform()->SetParent(parent->GetTransform(), /*keepWorldTransform=*/false);

    auto* rb2d = child->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Static;

    auto* col = child->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f }; // base size 1x1

    scene.OnRuntimeStart();
    ASSERT_TRUE(col->RuntimeShape.IsValid());

    b2ShapeId shapeId = b2LoadShapeId(col->RuntimeShape.Value);
    ASSERT_TRUE(b2Shape_IsValid(shapeId));

    b2Polygon poly = b2Shape_GetPolygon(shapeId);
    EXPECT_EQ(poly.count, 4);

    // Verify half-dimensions reflect composite parent world scale (2.0 along X, 3.0 along Z)
    float maxX = 0.0f;
    float maxZ = 0.0f;
    for (int i = 0; i < poly.count; ++i) {
        maxX = std::max(maxX, std::abs(poly.vertices[i].x));
        maxZ = std::max(maxZ, std::abs(poly.vertices[i].y));
    }

    // size.x * scale.x * 0.5 = 1.0 * 2.0 * 0.5 = 1.0
    // size.y * scale.z * 0.5 = 1.0 * 3.0 * 0.5 = 1.5
    EXPECT_NEAR(maxX, 1.0f, 1e-3f);
    EXPECT_NEAR(maxZ, 1.5f, 1e-3f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, DensePhysicsRegistry_ExcludesNonPhysicsAndStaticPropEntities) {
    Engine::Scene scene("DenseRegistryScene");

    // 300 visual-only standees / props (no Rigidbody2D, no Collider2D)
    for (int i = 0; i < 300; ++i) {
        auto* prop = scene.CreateGameObject("GrassTuft");
        prop->GetTransform()->position = { static_cast<float>(i % 20), 0.0f, static_cast<float>(i / 20) };
    }

    // 5 standalone static colliders (trees/fences with no Rigidbody2D)
    for (int i = 0; i < 5; ++i) {
        auto* staticProp = scene.CreateGameObject("StaticTree");
        staticProp->GetTransform()->position = { -10.0f - static_cast<float>(i), 0.0f, 0.0f };
        staticProp->AddComponent<Engine::BoxCollider2D>();
    }

    // 3 entities with Rigidbody2D (mixed Dynamic and Kinematic)
    std::vector<Engine::GameObject*> rbEntities;
    for (int i = 0; i < 3; ++i) {
        auto* actor = scene.CreateGameObject("PhysicsActor");
        actor->GetTransform()->position = { 0.0f, 0.0f, static_cast<float>(i) * 3.0f };
        auto* rb = actor->AddComponent<Engine::Rigidbody2D>();
        rb->Type = (i == 2) ? Engine::Rigidbody2D::BodyType::Kinematic : Engine::Rigidbody2D::BodyType::Dynamic;
        actor->AddComponent<Engine::BoxCollider2D>();
        rbEntities.push_back(actor);
    }

    EXPECT_EQ(scene.GetGameObjects().size(), 308u);

    scene.OnRuntimeStart();

    // Dense physics registry must track ONLY the 3 Rigidbody2D entities!
    auto& physics = scene.GetPhysicsWorld();
    EXPECT_EQ(physics.GetRegisteredPhysicsBodyCount(), 3u);

    // Destroy entity 0 -> swap_and_pop cleanly reduces count to 2
    scene.DestroyGameObject(rbEntities[0]);
    scene.FlushEntityLifecycleQueues();
    EXPECT_EQ(physics.GetRegisteredPhysicsBodyCount(), 2u);

    // Remove Rigidbody2D component from entity 1 (leaving BoxCollider2D as standalone static collider)
    EXPECT_TRUE(rbEntities[1]->RemoveComponent<Engine::Rigidbody2D>());
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(physics.GetRegisteredPhysicsBodyCount(), 1u);

    scene.OnRuntimeStop();
    EXPECT_EQ(physics.GetRegisteredPhysicsBodyCount(), 0u);
}

TEST(GroundPlanePhysicsTest, BodyMoveEvents_SyncsOnlyMovedDynamicBodiesAndLeavesSleepingUntouched) {
    Engine::Scene scene("BodyMoveEventsScene");

    // Stationary sleeping/resting dynamic body
    auto* sleeper = scene.CreateGameObject("RestingBox");
    sleeper->GetTransform()->position = { -5.0f, 0.0f, -5.0f };
    auto* sleeperRb = sleeper->AddComponent<Engine::Rigidbody2D>();
    sleeperRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    sleeper->AddComponent<Engine::BoxCollider2D>();

    // Actively moving dynamic body
    auto* runner = scene.CreateGameObject("MovingRunner");
    runner->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* runnerRb = runner->AddComponent<Engine::Rigidbody2D>();
    runnerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    runnerRb->FixedRotation = false;
    runnerRb->LinearDamping = 0.0f;
    runnerRb->InitialLinearVelocityXZ = { 6.0f, 0.0f };
    runner->AddComponent<Engine::BoxCollider2D>();

    scene.OnRuntimeStart();

    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(dt);
    }

    // Moving body synced cleanly to ~3.0m along X
    EXPECT_NEAR(runner->GetTransform()->position.x, 3.0f, 0.15f);
    EXPECT_NEAR(runner->GetTransform()->position.z, 0.0f, 1e-3f);

    // Stationary body remained completely undisturbed
    EXPECT_FLOAT_EQ(sleeper->GetTransform()->position.x, -5.0f);
    EXPECT_FLOAT_EQ(sleeper->GetTransform()->position.z, -5.0f);

    scene.OnRuntimeStop();
}

TEST(GroundPlanePhysicsTest, ParallelVerticalIntegration_BatchedExecutionAcrossManyBodies) {
    const bool weInitializedJobs = !Engine::JobSystem::IsInitialized();
    if (weInitializedJobs) {
        Engine::JobSystem::Init(4);
    }

    {
        Engine::Scene scene("ParallelVerticalScene");
        constexpr int kBodyCount = 80; // Above parallel threshold (64)
        std::vector<Engine::GameObject*> actors;
        actors.reserve(kBodyCount);

        for (int i = 0; i < kBodyCount; ++i) {
            auto* actor = scene.CreateGameObject("HopActor");
            actor->GetTransform()->position = { static_cast<float>(i % 10) * 2.0f, 0.0f, static_cast<float>(i / 10) * 2.0f };
            auto* rb = actor->AddComponent<Engine::Rigidbody2D>();
            rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
            rb->UseGravityY = true;
            rb->GravityY = -20.0f;
            rb->GroundLevelY = 0.0f;
            rb->EnableAirbornePassThrough = true;
            rb->AirborneClearanceHeight = 0.75f;
            auto* col = actor->AddComponent<Engine::BoxCollider2D>();
            col->CategoryBits = Engine::PhysicsLayer::Player;
            col->MaskBits = Engine::PhysicsLayer::All;
            actors.push_back(actor);
        }

        scene.OnRuntimeStart();
        ASSERT_EQ(scene.GetPhysicsWorld().GetRegisteredPhysicsBodyCount(), static_cast<size_t>(kBodyCount));

        // Even indices hop high enough to cross AirborneClearanceHeight; odd indices stay on ground
        for (int i = 0; i < kBodyCount; ++i) {
            if (i % 2 == 0) {
                actors[i]->GetComponent<Engine::Rigidbody2D>()->LaunchVertical(12.0f);
            }
        }

        // Step 15 frames (0.25s) -> even bodies are well above 0.75m
        constexpr float dt = 1.0f / 60.0f;
        for (int step = 0; step < 15; ++step) {
            scene.UpdateRuntime(dt);
        }

        for (int i = 0; i < kBodyCount; ++i) {
            auto* t = actors[i]->GetTransform();
            auto* rb = actors[i]->GetComponent<Engine::Rigidbody2D>();
            auto* col = actors[i]->GetComponent<Engine::BoxCollider2D>();
            ASSERT_TRUE(col->RuntimeShape.IsValid());
            b2Filter filter = b2Shape_GetFilter(b2LoadShapeId(col->RuntimeShape.Value));

            if (i % 2 == 0) {
                EXPECT_GT(t->position.y, 1.5f);
                EXPECT_TRUE(rb->IsAirbornePassThroughActive());
                EXPECT_EQ(filter.maskBits, Engine::PhysicsLayer::All & ~rb->AirbornePassThroughMask);
            } else {
                EXPECT_FLOAT_EQ(t->position.y, 0.0f);
                EXPECT_FALSE(rb->IsAirbornePassThroughActive());
                EXPECT_EQ(filter.maskBits, Engine::PhysicsLayer::All);
            }
        }

        // Step 60 more frames -> all even bodies land back at ground Y=0 and restore ground mask
        for (int step = 0; step < 60; ++step) {
            scene.UpdateRuntime(dt);
        }

        for (int i = 0; i < kBodyCount; ++i) {
            auto* t = actors[i]->GetTransform();
            auto* rb = actors[i]->GetComponent<Engine::Rigidbody2D>();
            auto* col = actors[i]->GetComponent<Engine::BoxCollider2D>();
            b2Filter filter = b2Shape_GetFilter(b2LoadShapeId(col->RuntimeShape.Value));
            EXPECT_FLOAT_EQ(t->position.y, 0.0f);
            EXPECT_FALSE(rb->IsAirbornePassThroughActive());
            EXPECT_EQ(filter.maskBits, Engine::PhysicsLayer::All);
        }

        scene.OnRuntimeStop();
    }

    if (weInitializedJobs) {
        Engine::JobSystem::Shutdown();
    }
}

} // namespace
