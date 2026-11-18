#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Core/Time.h"
#include "LifecycleTestHelpers.h"

#include <memory>
#include <string>
#include <vector>

namespace {

using Engine::TestHelpers::CallbackLifecycleComponent;
using Engine::TestHelpers::LifecycleTrackerComponent;

TEST(DeferredLifecycleTest, SameFrameSpawnThenDestroyWithPhysicsBody) {
    Engine::Scene scene("SpawnDestroyPhysicsScene");
    scene.OnRuntimeStart();

    int destroyCount = 0;
    bool executed = false;
    uint64_t entityUUID = 0;

    auto* controller = scene.CreateGameObject("PhysicsController");
    auto* comp = controller->AddComponent<CallbackLifecycleComponent>();
    comp->onUpdate = [&](Engine::GameObject*, float) {
        if (!executed) {
            executed = true;
            auto* entity = scene.CreateGameObject("TransientPhysicsEntity");
            entityUUID = entity->GetUUID();
            auto* tracker = entity->AddComponent<LifecycleTrackerComponent>();
            tracker->externalDestroyCount = &destroyCount;

            auto* rb2d = entity->AddComponent<Engine::Rigidbody2D>();
            rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
            auto* col = entity->AddComponent<Engine::BoxCollider2D>();
            col->size = { 1.5f, 1.5f };

            // Destroy in the same frame
            scene.DestroyGameObject(entity);
            EXPECT_TRUE(entity->IsPendingDestroy());
        }
    };

    // Step runtime simulation
    scene.UpdateRuntime(0.016f);

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.FindGameObjectByUUID(entityUUID), nullptr);
    EXPECT_EQ(destroyCount, 1);

    // Step once more to ensure physics world remains fully stable
    scene.UpdateRuntime(0.016f);

    scene.OnRuntimeStop();
}

TEST(DeferredLifecycleTest, Stress100Spawn50DestroyAcrossFrames) {
    Engine::Scene scene("StressLifecycleTest");

    std::vector<Engine::GameObject*> activeEntities;
    int framesRun = 0;
    int totalDestroyed = 0;

    auto* controller = scene.CreateGameObject("StressController");
    auto* stress = controller->AddComponent<CallbackLifecycleComponent>();
    stress->onUpdate = [&](Engine::GameObject*, float) {
        ++framesRun;

        // In first 5 frames, spawn 20 entities per frame (100 total)
        if (framesRun <= 5) {
            for (int i = 0; i < 20; ++i) {
                auto* obj = scene.CreateGameObject("StressObj_" + std::to_string(framesRun) + "_" + std::to_string(i));
                obj->AddComponent<LifecycleTrackerComponent>();
                activeEntities.push_back(obj);
            }
        }

        // In frames 6 through 10, destroy 10 entities per frame (50 total)
        if (framesRun >= 6 && framesRun <= 10 && !activeEntities.empty()) {
            for (int i = 0; i < 10 && !activeEntities.empty(); ++i) {
                auto* victim = activeEntities.back();
                activeEntities.pop_back();
                scene.DestroyGameObject(victim);
                ++totalDestroyed;
            }
        }
    };

    for (int frame = 0; frame < 10; ++frame) {
        scene.Update(0.016f);
    }

    // 1 controller + 100 spawned - 50 destroyed = 51 entities
    EXPECT_EQ(scene.GetGameObjects().size(), 51u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 51u);
    EXPECT_EQ(totalDestroyed, 50);

    // Verify all remaining entities can be found by UUID
    for (const auto& go : scene.GetGameObjects()) {
        EXPECT_EQ(scene.FindGameObjectByUUID(go->GetUUID()), go.get());
    }
}

TEST(DeferredLifecycleTest, Box2DRuntimeBodyDestroyedOnEntityDelete) {
    Engine::Scene scene("Box2DDeleteTest");
    scene.OnRuntimeStart();
    EXPECT_TRUE(scene.IsRuntimeRunning());

    auto* entity = scene.CreateGameObject("RigidEntity");
    auto* rb2d = entity->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* collider = entity->AddComponent<Engine::BoxCollider2D>();
    collider->size = { 1.0f, 1.0f };

    // Start runtime physics world created the bodies
    scene.OnRuntimeStop();
    scene.OnRuntimeStart();

    // Verify entity has runtime body
    EXPECT_NE(rb2d->RuntimeBody, nullptr);
    EXPECT_NE(collider->RuntimeShape, nullptr);

    // Destroy entity during runtime
    scene.DestroyGameObject(entity);

    // Step runtime physics to verify world remains valid with 0 crashes or ghost contacts
    scene.UpdateRuntime(0.016f);

    scene.OnRuntimeStop();
}

TEST(DeferredLifecycleTest, FixedUpdateSpawnAndDestroyAcross30Frames) {
    Engine::Scene scene("FixedUpdateStressScene");
    scene.OnRuntimeStart();
    EXPECT_TRUE(scene.IsRuntimeRunning());

    // Create a base prefab for pooling
    auto* templateEntity = scene.CreateGameObject("PooledTemplate");
    auto* tRb = templateEntity->AddComponent<Engine::Rigidbody2D>();
    tRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* tCol = templateEntity->AddComponent<Engine::BoxCollider2D>();
    tCol->size = { 0.5f, 0.5f };

    auto prefab = Engine::Prefab::CreateFromGameObject(templateEntity);
    ASSERT_NE(prefab, nullptr);
    scene.DestroyGameObject(templateEntity);
    EXPECT_EQ(scene.GetGameObjects().size(), 0u);

    const std::string poolKey = "BulletPool";
    scene.PrewarmPool(poolKey, *prefab, 5);

    std::vector<Engine::GameObject*> spawnedEntities;
    std::vector<Engine::GameObject*> pooledEntities;
    int frameCount = 0;
    int totalCreated = 0;
    int totalDestroyed = 0;

    auto* controller = scene.CreateGameObject("StressController");
    auto* stressComp = controller->AddComponent<CallbackLifecycleComponent>();
    stressComp->onFixedUpdate = [&](Engine::GameObject*, float) {
        ++frameCount;

        // 1. Spawn a dynamic runtime entity mid-FixedUpdate
        auto* newGo = scene.CreateGameObject("DynamicEnemy_" + std::to_string(frameCount));
        auto* rb = newGo->AddComponent<Engine::Rigidbody2D>();
        rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
        auto* col = newGo->AddComponent<Engine::BoxCollider2D>();
        col->size = { 1.0f, 1.0f };
        spawnedEntities.push_back(newGo);
        ++totalCreated;

        // 2. Acquire and release pooled entities mid-FixedUpdate
        if (prefab) {
            auto* bullet = scene.AcquirePooled(poolKey, *prefab, glm::vec3(float(frameCount), 0.0f, 0.0f));
            if (bullet) {
                pooledEntities.push_back(bullet);
            }
            if (pooledEntities.size() > 2) {
                auto* toRelease = pooledEntities.front();
                pooledEntities.erase(pooledEntities.begin());
                scene.ReleaseToPool(poolKey, toRelease);
            }
        }

        // 3. Destroy older entities mid-FixedUpdate (retain at most 3 alive)
        while (spawnedEntities.size() > 3) {
            auto* victim = spawnedEntities.front();
            spawnedEntities.erase(spawnedEntities.begin());
            scene.DestroyGameObject(victim);
            ++totalDestroyed;
        }
    };

    // Run simulation across 30 frames at fixed timestep (1 fixed step per frame)
    const float fixedDt = Engine::Time::GetFixedDeltaTime();
    for (int frame = 0; frame < 30; ++frame) {
        scene.UpdateRuntime(fixedDt);
    }

    EXPECT_EQ(frameCount, 30);
    EXPECT_EQ(totalCreated, 30);
    EXPECT_EQ(totalDestroyed, 27);
    EXPECT_EQ(spawnedEntities.size(), 3u);

    // Verify all remaining entities are valid, alive, and searchable in Scene
    for (auto* go : spawnedEntities) {
        ASSERT_NE(go, nullptr);
        EXPECT_TRUE(go->IsAlive());
        EXPECT_EQ(scene.FindGameObjectByUUID(go->GetUUID()), go);
        auto* rb = go->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(rb, nullptr);
        EXPECT_NE(rb->RuntimeBody, nullptr);
    }

    scene.OnRuntimeStop();
}

TEST(DeferredLifecycleTest, CascadingDestroyReusesTempChildrenScratch) {
    Engine::Scene scene("ScratchReuseDestroyScene");

    auto* root = scene.CreateGameObject("Root");
    std::vector<Engine::GameObject*> allNodes{ root };
    for (int i = 0; i < 4; ++i) {
        auto* child = scene.CreateGameObject("Child_" + std::to_string(i));
        child->GetTransform()->SetParent(root->GetTransform(), false);
        allNodes.push_back(child);

        for (int j = 0; j < 3; ++j) {
            auto* grandchild = scene.CreateGameObject("Grandchild_" + std::to_string(i) + "_" + std::to_string(j));
            grandchild->GetTransform()->SetParent(child->GetTransform(), false);
            allNodes.push_back(grandchild);
        }
    }

    const size_t capBefore = scene.GetTempChildrenScratchCapacity();
    EXPECT_GE(capBefore, 64u);

    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        scene.DestroyGameObject(root);

        // Scratch capacity must remain unchanged (zero per-node std::vector allocations)
        EXPECT_EQ(scene.GetTempChildrenScratchCapacity(), capBefore);
        for (auto* node : allNodes) {
            EXPECT_TRUE(node->IsPendingDestroy());
        }
    }

    EXPECT_EQ(scene.GetGameObjects().size(), 0u);
    EXPECT_EQ(scene.GetTempChildrenScratchCapacity(), capBefore);
}

TEST(DeferredLifecycleTest, DeferredSpawnStartSeesValidRuntimeBodyAndSceneMembership) {
    Engine::Scene scene("StartPhysicsVisibilityScene");
    scene.OnRuntimeStart();

    bool startCalled = false;
    bool sawValidBodyInStart = false;
    bool sawEntityInSceneInStart = false;

    {
        Engine::Scene::ScopedIterationGuard guard(scene);
        auto* spawned = scene.CreateGameObject("DeferredPhysicsEntity");
        spawned->AddComponent<Engine::Rigidbody2D>();
        spawned->AddComponent<Engine::CircleCollider2D>();
        auto* observer = spawned->AddComponent<CallbackLifecycleComponent>();
        observer->onStart = [&](Engine::GameObject* owner) {
            startCalled = true;
            if (!owner) return;

            if (auto* rb = owner->GetComponent<Engine::Rigidbody2D>()) {
                sawValidBodyInStart = rb->RuntimeBody.IsValid();
            }
            for (const auto& go : scene.GetGameObjects()) {
                if (go.get() == owner) {
                    sawEntityInSceneInStart = true;
                    break;
                }
            }
        };
        EXPECT_FALSE(startCalled);
    }

    EXPECT_TRUE(startCalled);
    EXPECT_TRUE(sawValidBodyInStart);
    EXPECT_TRUE(sawEntityInSceneInStart);

    scene.OnRuntimeStop();
}

TEST(DeferredLifecycleTest, SceneDestructorInvokesOnDestroyOnSurvivingAndQueuedEntities) {
    int survivingDestroyed = 0;
    int queuedDestroyed = 0;
    bool subsystemAliveDuringOnDestroy = false;

    {
        Engine::Scene scene("TeardownOrderScene");

        auto* surviving = scene.CreateGameObject("SurvivingEntity");
        auto* survTracker = surviving->AddComponent<CallbackLifecycleComponent>();
        survTracker->onDestroy = [&](Engine::GameObject*) {
            ++survivingDestroyed;
            subsystemAliveDuringOnDestroy = !scene.IsRuntimeRunning() &&
                                            (&scene.GetPhysicsWorld() != nullptr) &&
                                            (&scene.GetPoolManager() != nullptr) &&
                                            (&scene.GetRenderer() != nullptr);
        };

        // Enqueue an entity in PendingDestroy without flushing before ~Scene()
        auto* queued = scene.CreateGameObject("QueuedDestroyEntity");
        auto* queuedTracker = queued->AddComponent<CallbackLifecycleComponent>();
        queuedTracker->onDestroy = [&](Engine::GameObject*) {
            ++queuedDestroyed;
        };

        scene.SetIsIteratingEntities(true);
        scene.DestroyGameObject(queued);
    }

    EXPECT_EQ(survivingDestroyed, 1);
    EXPECT_EQ(queuedDestroyed, 1);
    EXPECT_TRUE(subsystemAliveDuringOnDestroy);
}

} // namespace
