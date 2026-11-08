#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Component.h"
#include "Engine/Physics/PhysicsComponents.h"

#include <vector>
#include <memory>
#include <string>

namespace {

class LifecycleTrackerComponent : public Engine::Component {
public:
    int* externalDestroyCount = nullptr;
    int awakeCount = 0;
    int enableCount = 0;
    int disableCount = 0;
    int startCount = 0;
    int updateCount = 0;
    int fixedUpdateCount = 0;
    int lateUpdateCount = 0;
    int destroyCount = 0;

    void Awake() override {
        ++awakeCount;
    }

    void OnEnable() override {
        ++enableCount;
    }

    void OnDisable() override {
        ++disableCount;
    }

    void Start() override {
        ++startCount;
    }

    void Update(float deltaTime) override {
        (void)deltaTime;
        ++updateCount;
    }

    void FixedUpdate(float fixedDeltaTime) override {
        (void)fixedDeltaTime;
        ++fixedUpdateCount;
    }

    void LateUpdate(float deltaTime) override {
        (void)deltaTime;
        ++lateUpdateCount;
    }

    void OnDestroy() override {
        ++destroyCount;
        if (externalDestroyCount) {
            ++(*externalDestroyCount);
        }
    }
};

class MidFrameSpawnerComponent : public Engine::Component {
public:
    Engine::Scene* scene = nullptr;
    int spawnCountOnUpdate = 0;
    std::vector<Engine::GameObject*> spawnedEntities;

    void Update(float deltaTime) override {
        (void)deltaTime;
        if (scene && spawnCountOnUpdate > 0) {
            for (int i = 0; i < spawnCountOnUpdate; ++i) {
                auto* spawned = scene->CreateGameObject("SpawnedMidFrame_" + std::to_string(i));
                spawned->AddComponent<LifecycleTrackerComponent>();
                spawnedEntities.push_back(spawned);
            }
            spawnCountOnUpdate = 0; // Only spawn once
        }
    }
};

class MidFrameDestroyerComponent : public Engine::Component {
public:
    Engine::Scene* scene = nullptr;
    std::vector<Engine::GameObject*> targetsToDestroy;

    void Update(float deltaTime) override {
        (void)deltaTime;
        if (scene) {
            for (auto* target : targetsToDestroy) {
                scene->DestroyGameObject(target);
            }
            targetsToDestroy.clear();
        }
    }
};

TEST(DeferredLifecycleTest, DeferredSpawnInsideUpdateLoop) {
    Engine::Scene scene("DeferredSpawnTest");

    auto* spawnerObj = scene.CreateGameObject("SpawnerEntity");
    auto* spawnerComp = spawnerObj->AddComponent<MidFrameSpawnerComponent>();
    spawnerComp->scene = &scene;
    spawnerComp->spawnCountOnUpdate = 10;

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);

    // Run Scene::Update - Spawner will create 10 GameObjects mid-iteration
    scene.Update(0.016f);

    // After Update and FlushEntityLifecycleQueues, total should be 1 + 10 = 11
    EXPECT_EQ(scene.GetGameObjects().size(), 11u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 11u);
    EXPECT_EQ(spawnerComp->spawnedEntities.size(), 10u);

    // Verify all spawned entities are valid and searchable by UUID
    for (auto* entity : spawnerComp->spawnedEntities) {
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

    auto* destroyerObj = scene.CreateGameObject("Destroyer");
    auto* destroyerComp = destroyerObj->AddComponent<MidFrameDestroyerComponent>();
    destroyerComp->scene = &scene;
    // Queue victims 1 and 3 for destruction
    destroyerComp->targetsToDestroy = { victims[1], victims[3] };

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

    class SpawnAndSelfDestroyComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        bool done = false;
        Engine::GameObject* tempChild = nullptr;
        int childDestroyCount = 0;

        void Update(float deltaTime) override {
            (void)deltaTime;
            if (!done && scene) {
                tempChild = scene->CreateGameObject("TransientEntity");
                auto* tracker = tempChild->AddComponent<LifecycleTrackerComponent>();
                // Immediately destroy it in the same iteration
                scene->DestroyGameObject(tempChild);
                done = true;
                childDestroyCount = tracker->destroyCount;
            }
        }
    };

    auto* parent = scene.CreateGameObject("ParentEntity");
    auto* comp = parent->AddComponent<SpawnAndSelfDestroyComponent>();
    comp->scene = &scene;

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    scene.Update(0.016f);

    // After update, transient child should not be added to m_GameObjects
    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_NE(comp->tempChild, nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(comp->tempChild->GetUUID()), nullptr);
}

TEST(DeferredLifecycleTest, SameFrameSpawnThenDestroyParentAndChildHierarchy) {
    Engine::Scene scene("ParentChildSameFrameDestroyScene");

    int parentDestroyCount = 0;
    int childDestroyCount = 0;
    int grandchildDestroyCount = 0;

    class SpawnHierarchyAndDestroyComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        int* pDestroy = nullptr;
        int* cDestroy = nullptr;
        int* gDestroy = nullptr;
        bool executed = false;
        uint64_t parentUUID = 0;
        uint64_t childUUID = 0;
        uint64_t grandchildUUID = 0;

        void Update(float deltaTime) override {
            (void)deltaTime;
            if (!executed && scene) {
                executed = true;

                auto* parent = scene->CreateGameObject("TransientParent");
                parentUUID = parent->GetUUID();
                auto* pTracker = parent->AddComponent<LifecycleTrackerComponent>();
                pTracker->externalDestroyCount = pDestroy;

                auto* child = scene->CreateGameObject("TransientChild");
                childUUID = child->GetUUID();
                auto* cTracker = child->AddComponent<LifecycleTrackerComponent>();
                cTracker->externalDestroyCount = cDestroy;
                child->GetTransform()->SetParent(parent->GetTransform(), false);

                auto* grandchild = scene->CreateGameObject("TransientGrandchild");
                grandchildUUID = grandchild->GetUUID();
                auto* gTracker = grandchild->AddComponent<LifecycleTrackerComponent>();
                gTracker->externalDestroyCount = gDestroy;
                grandchild->GetTransform()->SetParent(child->GetTransform(), false);

                // Both parent, child, and grandchild are in m_PendingSpawnQueue
                EXPECT_TRUE(parent->IsPendingSpawn());
                EXPECT_TRUE(child->IsPendingSpawn());
                EXPECT_TRUE(grandchild->IsPendingSpawn());

                // Destroy the root parent in the exact same frame!
                scene->DestroyGameObject(parent);

                // All 3 must transition to PendingDestroy immediately
                EXPECT_TRUE(parent->IsPendingDestroy());
                EXPECT_TRUE(child->IsPendingDestroy());
                EXPECT_TRUE(grandchild->IsPendingDestroy());
            }
        }
    };

    auto* controller = scene.CreateGameObject("LifecycleController");
    auto* comp = controller->AddComponent<SpawnHierarchyAndDestroyComponent>();
    comp->scene = &scene;
    comp->pDestroy = &parentDestroyCount;
    comp->cDestroy = &childDestroyCount;
    comp->gDestroy = &grandchildDestroyCount;

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);

    // Run Scene::Update which triggers spawn + destroy in same frame and flushes queues
    scene.Update(0.016f);

    // Only controller should remain in the scene
    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.FindGameObjectByUUID(controller->GetUUID()), controller);

    // Parent, child, and grandchild must not be found
    EXPECT_EQ(scene.FindGameObjectByUUID(comp->parentUUID), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(comp->childUUID), nullptr);
    EXPECT_EQ(scene.FindGameObjectByUUID(comp->grandchildUUID), nullptr);

    // Each entity's OnDestroy() must have been invoked exactly ONCE (zero double-destroy and zero UAF)
    EXPECT_EQ(parentDestroyCount, 1);
    EXPECT_EQ(childDestroyCount, 1);
    EXPECT_EQ(grandchildDestroyCount, 1);
}

TEST(DeferredLifecycleTest, SameFrameSpawnThenDestroyWithPhysicsBody) {
    Engine::Scene scene("SpawnDestroyPhysicsScene");
    scene.OnRuntimeStart();

    int destroyCount = 0;

    class SpawnAndDestroyPhysicsComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        int* pDestroy = nullptr;
        bool executed = false;
        uint64_t entityUUID = 0;

        void Update(float deltaTime) override {
            (void)deltaTime;
            if (!executed && scene) {
                executed = true;
                auto* entity = scene->CreateGameObject("TransientPhysicsEntity");
                entityUUID = entity->GetUUID();
                auto* tracker = entity->AddComponent<LifecycleTrackerComponent>();
                tracker->externalDestroyCount = pDestroy;

                auto* rb2d = entity->AddComponent<Engine::Rigidbody2D>();
                rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
                auto* col = entity->AddComponent<Engine::BoxCollider2D>();
                col->size = { 1.5f, 1.5f };

                // Destroy in the same frame
                scene->DestroyGameObject(entity);
                EXPECT_TRUE(entity->IsPendingDestroy());
            }
        }
    };

    auto* controller = scene.CreateGameObject("PhysicsController");
    auto* comp = controller->AddComponent<SpawnAndDestroyPhysicsComponent>();
    comp->scene = &scene;
    comp->pDestroy = &destroyCount;

    // Step runtime simulation
    scene.UpdateRuntime(0.016f);

    EXPECT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 1u);
    EXPECT_EQ(scene.FindGameObjectByUUID(comp->entityUUID), nullptr);
    EXPECT_EQ(destroyCount, 1);

    // Step once more to ensure physics world remains fully stable
    scene.UpdateRuntime(0.016f);

    scene.OnRuntimeStop();
}

TEST(DeferredLifecycleTest, OnDestroyRecursiveDestroyAliveEntity) {
    Engine::Scene scene("RecursiveDestroyScene");

    int destroyCountA = 0;
    int destroyCountB = 0;

    class SecondaryDestroyerComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        Engine::GameObject* target = nullptr;
        int* pDestroyCount = nullptr;

        void OnDestroy() override {
            if (pDestroyCount) {
                ++(*pDestroyCount);
            }
            if (scene && target) {
                scene->DestroyGameObject(target);
            }
        }
    };

    auto* objB = scene.CreateGameObject("EntityB");
    auto* trackerB = objB->AddComponent<LifecycleTrackerComponent>();
    trackerB->externalDestroyCount = &destroyCountB;

    auto* objA = scene.CreateGameObject("EntityA");
    auto* destroyerA = objA->AddComponent<SecondaryDestroyerComponent>();
    destroyerA->scene = &scene;
    destroyerA->target = objB;
    destroyerA->pDestroyCount = &destroyCountA;

    class TriggerDestroyComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        Engine::GameObject* victim = nullptr;
        bool executed = false;

        void Update(float deltaTime) override {
            (void)deltaTime;
            if (!executed && scene && victim) {
                executed = true;
                scene->DestroyGameObject(victim);
            }
        }
    };

    auto* controller = scene.CreateGameObject("Controller");
    auto* trig = controller->AddComponent<TriggerDestroyComponent>();
    trig->scene = &scene;
    trig->victim = objA;

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

TEST(DeferredLifecycleTest, Stress100Spawn50DestroyAcrossFrames) {
    Engine::Scene scene("StressLifecycleTest");

    class StressTesterComponent : public Engine::Component {
    public:
        Engine::Scene* scene = nullptr;
        std::vector<Engine::GameObject*> activeEntities;
        int framesRun = 0;
        int totalDestroyed = 0;

        void Update(float deltaTime) override {
            (void)deltaTime;
            ++framesRun;
            if (!scene) return;

            // In first 5 frames, spawn 20 entities per frame (100 total)
            if (framesRun <= 5) {
                for (int i = 0; i < 20; ++i) {
                    auto* obj = scene->CreateGameObject("StressObj_" + std::to_string(framesRun) + "_" + std::to_string(i));
                    obj->AddComponent<LifecycleTrackerComponent>();
                    activeEntities.push_back(obj);
                }
            }

            // In frames 6 through 10, destroy 10 entities per frame (50 total)
            if (framesRun >= 6 && framesRun <= 10 && !activeEntities.empty()) {
                for (int i = 0; i < 10 && !activeEntities.empty(); ++i) {
                    auto* victim = activeEntities.back();
                    activeEntities.pop_back();
                    scene->DestroyGameObject(victim);
                    ++totalDestroyed;
                }
            }
        }
    };

    auto* controller = scene.CreateGameObject("StressController");
    auto* stress = controller->AddComponent<StressTesterComponent>();
    stress->scene = &scene;

    for (int frame = 0; frame < 10; ++frame) {
        scene.Update(0.016f);
    }

    // 1 controller + 100 spawned - 50 destroyed = 51 entities
    EXPECT_EQ(scene.GetGameObjects().size(), 51u);
    EXPECT_EQ(scene.GetEntityUUIDMapCount(), 51u);
    EXPECT_EQ(stress->totalDestroyed, 50);

    // Verify all remaining entities can be found by UUID
    for (const auto& go : scene.GetGameObjects()) {
        EXPECT_EQ(scene.FindGameObjectByUUID(go->GetUUID()), go.get());
    }
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
    EXPECT_NE(collider->RuntimeFixture, nullptr);

    // Destroy entity during runtime
    scene.DestroyGameObject(entity);

    // Step runtime physics to verify world remains valid with 0 crashes or ghost contacts
    scene.UpdateRuntime(0.016f);

    scene.OnRuntimeStop();
}

} // namespace
