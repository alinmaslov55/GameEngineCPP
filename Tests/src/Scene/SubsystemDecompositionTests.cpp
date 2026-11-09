#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Renderer/SpriteRenderer.h"

namespace {

TEST(SubsystemDecompositionTest, DirectPhysicsWorldSubsystem) {
    Engine::Scene scene("SubsystemPhysicsTest");
    auto& physics = scene.GetPhysicsWorld();

    EXPECT_FALSE(physics.IsRuntimeRunning());
    EXPECT_EQ(physics.GetPhysicsWorldId(), 0u);

    physics.OnRuntimeStart();
    EXPECT_TRUE(physics.IsRuntimeRunning());
    EXPECT_NE(physics.GetPhysicsWorldId(), 0u);
    EXPECT_TRUE(scene.IsRuntimeRunning()); // Facade matches

    // Verify task slots alignment and size
    EXPECT_EQ(physics.GetBox2DTaskSlotCount(), 16u);
    const auto* slots = physics.GetBox2DTaskSlots();
    ASSERT_NE(slots, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(slots) % 64, 0u);
    static_assert(alignof(Engine::PhysicsWorld2D::Box2DTask) == 64, "PhysicsWorld2D::Box2DTask must be 64-byte aligned");
    static_assert(sizeof(Engine::PhysicsWorld2D::Box2DTask) == 64, "PhysicsWorld2D::Box2DTask size must equal 64 bytes");

    // Spawn an entity and register into physics world
    auto* entity = scene.CreateGameObject("PhysicsTarget");
    entity->GetTransform()->position = { 5.0f, 0.0f, 5.0f };
    auto* rb = entity->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* col = entity->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };

    physics.RegisterRuntimeBody(entity);
    EXPECT_TRUE(rb->RuntimeBody.IsValid());

    // Spatial query directly on PhysicsWorld2D
    std::vector<Engine::GameObject*> hits;
    physics.OverlapCircleXZ({ 5.0f, 5.0f }, 2.0f, Engine::PhysicsLayer::All, hits);
    EXPECT_EQ(hits.size(), 1u);
    if (!hits.empty()) {
        EXPECT_EQ(hits[0], entity);
    }

    physics.OnRuntimeStop();
    EXPECT_FALSE(physics.IsRuntimeRunning());
    EXPECT_FALSE(scene.IsRuntimeRunning());
}

TEST(SubsystemDecompositionTest, DirectPoolManagerSubsystem) {
    Engine::Scene scene("SubsystemPoolTest");
    auto& poolMgr = scene.GetPoolManager();

    // Create a template GameObject and snapshot to Prefab
    auto* templateGo = scene.CreateGameObject("TemplateArrow");
    templateGo->AddComponent<Engine::SpriteRenderer>();
    auto prefab = Engine::Prefab::CreateFromGameObject(templateGo, true);
    ASSERT_NE(prefab, nullptr);
    scene.DestroyGameObject(templateGo);
    scene.FlushEntityLifecycleQueues();

    // Prewarm via pool manager
    const std::string poolKey = "SubsystemArrowPool";
    poolMgr.PrewarmPool(poolKey, *prefab, 4);

    EXPECT_EQ(poolMgr.GetPoolFreeCount(poolKey), 4u);
    EXPECT_EQ(poolMgr.GetPoolTotalCount(poolKey), 4u);
    EXPECT_EQ(scene.GetPoolFreeCount(poolKey), 4u); // Facade delegation check

    // Acquire an instance
    auto* inst1 = poolMgr.AcquirePooled(poolKey, *prefab, { 10.0f, 0.0f, 15.0f });
    ASSERT_NE(inst1, nullptr);
    EXPECT_TRUE(inst1->IsActive());
    EXPECT_EQ(inst1->GetTransform()->position.x, 10.0f);
    EXPECT_EQ(poolMgr.GetPoolFreeCount(poolKey), 3u);

    // Release back
    poolMgr.ReleaseToPool(poolKey, inst1);
    EXPECT_FALSE(inst1->IsActive());
    EXPECT_EQ(poolMgr.GetPoolFreeCount(poolKey), 4u);

    // Clear pool
    poolMgr.ClearPool(poolKey);
    EXPECT_EQ(poolMgr.GetPoolFreeCount(poolKey), 0u);
    EXPECT_EQ(poolMgr.GetPoolTotalCount(poolKey), 0u);
}

TEST(SubsystemDecompositionTest, DirectRendererSubsystem) {
    Engine::Scene scene("SubsystemRendererTest");
    auto& renderer = scene.GetRenderer();

    // Verify orthographic size configuration
    EXPECT_FLOAT_EQ(renderer.GetOrthographicSize(), 5.0f);
    renderer.SetOrthographicSize(9.25f);
    EXPECT_FLOAT_EQ(renderer.GetOrthographicSize(), 9.25f);
    EXPECT_FLOAT_EQ(scene.GetOrthographicSize(), 9.25f); // Facade delegation check

    // Viewport resize
    renderer.OnViewportResize(scene, 1920, 1080);
    EXPECT_EQ(renderer.GetViewportWidth(), 1920u);
    EXPECT_EQ(renderer.GetViewportHeight(), 1080u);

    const auto& camera = renderer.GetCamera();
    EXPECT_NEAR(camera.GetAspectRatio(), 1920.0f / 1080.0f, 1e-4f);
    EXPECT_NEAR(scene.GetCamera().GetAspectRatio(), 1920.0f / 1080.0f, 1e-4f);
}

TEST(SubsystemDecompositionTest, SceneFacadeBackwardCompatibility) {
    Engine::Scene scene("FacadeCompatTest");

    // Test that existing Scene facade signatures work seamlessly
    scene.SetOrthographicSize(7.0f);
    EXPECT_FLOAT_EQ(scene.GetOrthographicSize(), 7.0f);

    scene.OnRuntimeStart();
    EXPECT_TRUE(scene.IsRuntimeRunning());
    EXPECT_NE(scene.GetPhysicsWorldId(), 0u);

    scene.ResetPhysicsAccumulator();
    EXPECT_FLOAT_EQ(scene.GetPhysicsAlpha(), 0.0f);

    scene.OnRuntimeStop();
    EXPECT_FALSE(scene.IsRuntimeRunning());
}

} // namespace
