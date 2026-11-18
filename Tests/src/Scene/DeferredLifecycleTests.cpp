#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Component.h"
#include "LifecycleTestHelpers.h"

#include <string>
#include <vector>

namespace {

using Engine::TestHelpers::CallbackLifecycleComponent;
using Engine::TestHelpers::LifecycleTrackerComponent;

TEST(DeferredLifecycleTest, DeferredSpawnInsideUpdateLoop) {
    Engine::Scene scene("DeferredSpawnTest");

    int spawnCountOnUpdate = 10;
    std::vector<Engine::GameObject*> spawnedEntities;

    auto* spawnerObj = scene.CreateGameObject("SpawnerEntity");
    auto* spawnerComp = spawnerObj->AddComponent<CallbackLifecycleComponent>();
    spawnerComp->onUpdate = [&](Engine::GameObject*, float) {
        if (spawnCountOnUpdate > 0) {
            for (int i = 0; i < spawnCountOnUpdate; ++i) {
                auto* spawned = scene.CreateGameObject("SpawnedMidFrame_" + std::to_string(i));
                spawned->AddComponent<LifecycleTrackerComponent>();
                spawnedEntities.push_back(spawned);
            }
            spawnCountOnUpdate = 0;
        }
    };

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);

    // Run Scene::Update - Spawner will create 10 GameObjects mid-iteration
    scene.Update(0.016f);

    // After Update and FlushEntityLifecycleQueues, total should be 1 + 10 = 11
    EXPECT_EQ(scene.GetGameObjects().size(), 11u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 11u);
    EXPECT_EQ(spawnedEntities.size(), 10u);

    // Verify all spawned entities are valid and searchable by UUID
    for (auto* entity : spawnedEntities) {
        ASSERT_NE(entity, nullptr);
        EXPECT_EQ(scene.FindGameObjectByUUID(entity->GetUUID()), entity);
        auto* tracker = entity->GetComponent<LifecycleTrackerComponent>();
        ASSERT_NE(tracker, nullptr);
        EXPECT_EQ(tracker->awakeCount, 1);
        EXPECT_EQ(tracker->enableCount, 1);
        EXPECT_EQ(tracker->startCount, 1);
    }
}

TEST(DeferredLifecycleTest, DeferredDestroyInsideUpdateLoop) {
    Engine::Scene scene("DeferredDestroyTest");

    std::vector<Engine::GameObject*> victims;
    std::vector<int> externalDestroyCounts(5, 0);
    for (int i = 0; i < 5; ++i) {
        auto* obj = scene.CreateGameObject("Victim_" + std::to_string(i));
        victims.push_back(obj);
        auto* tracker = obj->AddComponent<LifecycleTrackerComponent>();
        tracker->externalDestroyCount = &externalDestroyCounts[i];
    }

    std::vector<Engine::GameObject*> targetsToDestroy = { victims[1], victims[3] };
    auto* destroyerObj = scene.CreateGameObject("Destroyer");
    auto* destroyerComp = destroyerObj->AddComponent<CallbackLifecycleComponent>();
    destroyerComp->onUpdate = [&](Engine::GameObject*, float) {
        for (auto* target : targetsToDestroy) {
            scene.DestroyGameObject(target);
        }
        targetsToDestroy.clear();
    };

    EXPECT_EQ(scene.GetGameObjects().size(), 6u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 6u);

    // Execute update where victims are destroyed mid-frame
    scene.Update(0.016f);

    // After flush, victims 1 and 3 must be destroyed, remaining count: 4
    EXPECT_EQ(scene.GetGameObjects().size(), 4u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 4u);

    // Destroyed entities must return nullptr in UUID lookups
    EXPECT_EQ(scene.FindGameObjectByUUID(victims[1]->GetUUID()), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(victims[3]->GetUUID()), nullptr);

    // Surviving entities must be intact
    EXPECT_EQ(scene.FindGameObjectByUUID(victims[0]->GetUUID()), victims[0]);
    EXPECT_EQ(scene.FindGameObjectByUUID(victims[2]->GetUUID()), victims[2]);
    EXPECT_EQ(scene.FindGameObjectByUUID(victims[4]->GetUUID()), victims[4]);

    // OnDestroy() must have been called exactly once for destroyed entities
    EXPECT_EQ(externalDestroyCounts[1], 1);
    EXPECT_EQ(externalDestroyCounts[3], 1);
    EXPECT_EQ(externalDestroyCounts[0], 0);
    EXPECT_EQ(externalDestroyCounts[2], 0);
    EXPECT_EQ(externalDestroyCounts[4], 0);
}

TEST(DeferredLifecycleTest, SpawnAndDestroyInSameFrame) {
    Engine::Scene scene("SameFrameSpawnDestroy");

    bool done = false;
    Engine::GameObject* tempChild = nullptr;
    int childDestroyCount = 0;

    auto* parent = scene.CreateGameObject("ParentEntity");
    auto* comp = parent->AddComponent<CallbackLifecycleComponent>();
    comp->onUpdate = [&](Engine::GameObject*, float) {
        if (!done) {
            tempChild = scene.CreateGameObject("TransientEntity");
            auto* tracker = tempChild->AddComponent<LifecycleTrackerComponent>();
            // Immediately destroy it in the same iteration
            scene.DestroyGameObject(tempChild);
            done = true;
            childDestroyCount = tracker->destroyCount;
        }
    };

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    scene.Update(0.016f);

    // After update, transient child should not be added to m_GameObjects
    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_NE(tempChild, nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(tempChild->GetUUID()), nullptr);
    EXPECT_EQ(childDestroyCount, 0);
}

TEST(DeferredLifecycleTest, SameFrameSpawnThenDestroyParentAndChildHierarchy) {
    Engine::Scene scene("ParentChildSameFrameDestroyScene");

    int parentDestroyCount = 0;
    int childDestroyCount = 0;
    int grandchildDestroyCount = 0;
    bool executed = false;
    uint64_t parentUUID = 0;
    uint64_t childUUID = 0;
    uint64_t grandchildUUID = 0;

    auto* controller = scene.CreateGameObject("LifecycleController");
    auto* comp = controller->AddComponent<CallbackLifecycleComponent>();
    comp->onUpdate = [&](Engine::GameObject*, float) {
        if (!executed) {
            executed = true;

            auto* parent = scene.CreateGameObject("TransientParent");
            parentUUID = parent->GetUUID();
            auto* pTracker = parent->AddComponent<LifecycleTrackerComponent>();
            pTracker->externalDestroyCount = &parentDestroyCount;

            auto* child = scene.CreateGameObject("TransientChild");
            childUUID = child->GetUUID();
            auto* cTracker = child->AddComponent<LifecycleTrackerComponent>();
            cTracker->externalDestroyCount = &childDestroyCount;
            child->GetTransform()->SetParent(parent->GetTransform(), false);

            auto* grandchild = scene.CreateGameObject("TransientGrandchild");
            grandchildUUID = grandchild->GetUUID();
            auto* gTracker = grandchild->AddComponent<LifecycleTrackerComponent>();
            gTracker->externalDestroyCount = &grandchildDestroyCount;
            grandchild->GetTransform()->SetParent(child->GetTransform(), false);

            // Both parent, child, and grandchild are in m_PendingSpawnQueue
            EXPECT_TRUE(parent->IsPendingSpawn());
            EXPECT_TRUE(child->IsPendingSpawn());
            EXPECT_TRUE(grandchild->IsPendingSpawn());

            // Destroy the root parent in the exact same frame!
            scene.DestroyGameObject(parent);

            // All 3 must transition to PendingDestroy immediately
            EXPECT_TRUE(parent->IsPendingDestroy());
            EXPECT_TRUE(child->IsPendingDestroy());
            EXPECT_TRUE(grandchild->IsPendingDestroy());
        }
    };

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);

    // Run Scene::Update which triggers spawn + destroy in same frame and flushes queues
    scene.Update(0.016f);

    // Only controller should remain in the scene
    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.FindGameObjectByUUID(controller->GetUUID()), controller);

    // Parent, child, and grandchild must not be found
    EXPECT_EQ(scene.FindGameObjectByUUID(parentUUID), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(childUUID), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(grandchildUUID), nullptr);

    // Each entity's OnDestroy() must have been invoked exactly ONCE (zero double-destroy and zero UAF)
    EXPECT_EQ(parentDestroyCount, 1);
    EXPECT_EQ(childDestroyCount, 1);
    EXPECT_EQ(grandchildDestroyCount, 1);
}

TEST(DeferredLifecycleTest, OnDestroyRecursiveDestroyAliveEntity) {
    Engine::Scene scene("RecursiveDestroyScene");

    int destroyCountA = 0;
    int destroyCountB = 0;

    auto* objB = scene.CreateGameObject("EntityB");
    auto* trackerB = objB->AddComponent<LifecycleTrackerComponent>();
    trackerB->externalDestroyCount = &destroyCountB;

    auto* objA = scene.CreateGameObject("EntityA");
    auto* destroyerA = objA->AddComponent<CallbackLifecycleComponent>();
    destroyerA->onDestroy = [&](Engine::GameObject*) {
        ++destroyCountA;
        scene.DestroyGameObject(objB);
    };

    bool executed = false;
    auto* controller = scene.CreateGameObject("Controller");
    auto* trig = controller->AddComponent<CallbackLifecycleComponent>();
    trig->onUpdate = [&](Engine::GameObject*, float) {
        if (!executed) {
            executed = true;
            scene.DestroyGameObject(objA);
        }
    };

    EXPECT_EQ(scene.GetGameObjects().size(), 3u);

    // Run Scene::Update: objA is destroyed, and in A's OnDestroy(), objB is destroyed
    scene.Update(0.016f);

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.FindGameObjectByUUID(objA->GetUUID()), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(objB->GetUUID()), nullptr);
    EXPECT_EQ(destroyCountA, 1);
    EXPECT_EQ(destroyCountB, 1);
}

TEST(DeferredLifecycleTest, ActiveAndEnabledHierarchy) {
    Engine::Scene scene("ActiveTest");

    auto* obj = scene.CreateGameObject("ToggleEntity");
    auto* tracker = obj->AddComponent<LifecycleTrackerComponent>();

    EXPECT_TRUE(obj->IsActive());
    EXPECT_TRUE(obj->IsActiveInHierarchy());
    EXPECT_TRUE(tracker->IsEnabled());
    EXPECT_EQ(tracker->awakeCount, 1);
    EXPECT_EQ(tracker->enableCount, 1);
    EXPECT_EQ(tracker->disableCount, 0);

    // Update 1: should update
    scene.Update(0.016f);
    EXPECT_EQ(tracker->startCount, 1);
    EXPECT_EQ(tracker->updateCount, 1);

    // Deactivate GameObject
    obj->SetActive(false);
    EXPECT_FALSE(obj->IsActive());
    EXPECT_FALSE(obj->IsActiveInHierarchy());
    EXPECT_EQ(tracker->disableCount, 1);

    // Update 2: inactive entity must not receive Update()
    scene.Update(0.016f);
    EXPECT_EQ(tracker->updateCount, 1);

    // Reactivate GameObject
    obj->SetActive(true);
    EXPECT_TRUE(obj->IsActive());
    EXPECT_TRUE(obj->IsActiveInHierarchy());
    EXPECT_EQ(tracker->enableCount, 2);

    // Update 3: active entity receives Update()
    scene.Update(0.016f);
    EXPECT_EQ(tracker->updateCount, 2);

    // Test Component-level SetEnabled
    tracker->SetEnabled(false);
    EXPECT_FALSE(tracker->IsEnabled());
    EXPECT_EQ(tracker->disableCount, 2);

    scene.Update(0.016f);
    EXPECT_EQ(tracker->updateCount, 2); // skipped

    tracker->SetEnabled(true);
    EXPECT_TRUE(tracker->IsEnabled());
    EXPECT_EQ(tracker->enableCount, 3);

    scene.Update(0.016f);
    EXPECT_EQ(tracker->updateCount, 3);
}

TEST(DeferredLifecycleTest, StartCalledOnceBeforeFirstUpdate) {
    Engine::Scene scene("StartTest");

    auto* inactiveObj = scene.CreateGameObject("InactiveStart");
    inactiveObj->SetActive(false);
    auto* tracker = inactiveObj->AddComponent<LifecycleTrackerComponent>();

    // Update with inactive entity: Start must NOT be called yet
    scene.Update(0.016f);
    EXPECT_EQ(tracker->startCount, 0);
    EXPECT_EQ(tracker->updateCount, 0);

    // Now activate: on next update, Start should be called once, then Update
    inactiveObj->SetActive(true);
    scene.Update(0.016f);
    EXPECT_EQ(tracker->startCount, 1);
    EXPECT_EQ(tracker->updateCount, 1);

    // Next update: Start count remains 1
    scene.Update(0.016f);
    EXPECT_EQ(tracker->startCount, 1);
    EXPECT_EQ(tracker->updateCount, 2);
}

TEST(DeferredLifecycleTest, ImmediateDestroyOutsideUpdatePreserved) {
    Engine::Scene scene("ImmediateDestroyTest");

    auto* a = scene.CreateGameObject("EntityA");
    auto* b = scene.CreateGameObject("EntityB");
    int destroyCountA = 0;
    auto* trackerA = a->AddComponent<LifecycleTrackerComponent>();
    trackerA->externalDestroyCount = &destroyCountA;

    EXPECT_EQ(scene.GetGameObjects().size(), 2u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 2u);

    // Direct destroy outside Update() must flush immediately for backward compatibility
    scene.DestroyGameObject(a);
    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.GetGameObjects()[0].get(), b);
    EXPECT_EQ(scene.FindGameObjectByUUID(a->GetUUID()), nullptr);
    EXPECT_EQ(destroyCountA, 1);
}

TEST(DeferredLifecycleTest, ReentrantIterationDepthTracking) {
    Engine::Scene scene("DepthTrackingScene");
    EXPECT_EQ(scene.GetIterationDepth(), 0u);
    EXPECT_FALSE(scene.IsIteratingEntities());

    {
        Engine::Scene::ScopedIterationGuard g1(scene);
        EXPECT_EQ(scene.GetIterationDepth(), 1u);
        EXPECT_TRUE(scene.IsIteratingEntities());

        auto* entity1 = scene.CreateGameObject("Entity1");
        EXPECT_TRUE(entity1->IsPendingSpawn());
        EXPECT_EQ(scene.GetGameObjects().size(), 0u);

        {
            Engine::Scene::ScopedIterationGuard g2(scene);
            EXPECT_EQ(scene.GetIterationDepth(), 2u);

            auto* entity2 = scene.CreateGameObject("Entity2");
            EXPECT_TRUE(entity2->IsPendingSpawn());
            EXPECT_EQ(scene.GetGameObjects().size(), 0u);

            {
                Engine::Scene::ScopedIterationGuard g3(scene);
                EXPECT_EQ(scene.GetIterationDepth(), 3u);
            }
            EXPECT_EQ(scene.GetIterationDepth(), 2u);
            EXPECT_EQ(scene.GetGameObjects().size(), 0u);
        }
        EXPECT_EQ(scene.GetIterationDepth(), 1u);
        EXPECT_EQ(scene.GetGameObjects().size(), 0u);
    }

    // Depth returns to 0 -> Auto-flushed!
    EXPECT_EQ(scene.GetIterationDepth(), 0u);
    EXPECT_FALSE(scene.IsIteratingEntities());
    EXPECT_EQ(scene.GetGameObjects().size(), 2u);
    EXPECT_TRUE(scene.GetGameObjects()[0]->IsAlive());
    EXPECT_TRUE(scene.GetGameObjects()[1]->IsAlive());
}

} // namespace
