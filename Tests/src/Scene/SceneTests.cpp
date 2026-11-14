#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/TerrainComponent.h"

#include <thread>
#include <type_traits>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {

TEST(SceneTest, CreationAndObjectManagement) {
    auto scene = std::make_shared<Engine::Scene>("VillageScene");
    EXPECT_EQ(scene->GetName(), "VillageScene");
    EXPECT_TRUE(scene->GetGameObjects().empty());

    auto* obj1 = scene->CreateGameObject("House");
    auto* obj2 = scene->CreateGameObject("Well");
    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    EXPECT_EQ(scene->GetGameObjects().size(), 2u);

    // Explicit UUID creation
    auto* objWithUUID = scene->CreateGameObjectWithUUID(0x123456789ABCDEF0ULL, "Shrine");
    EXPECT_EQ(objWithUUID->GetUUID(), 0x123456789ABCDEF0ULL);
    EXPECT_EQ(scene->GetGameObjects().size(), 3u);

    // Destroy object
    scene->DestroyGameObject(obj1);
    EXPECT_EQ(scene->GetGameObjects().size(), 2u);
}

TEST(SceneManagerTest, ActiveSceneLoadingAndRetrieval) {
    EXPECT_EQ(Engine::SceneManager::GetActiveScene(), nullptr);

    auto sceneA = std::make_shared<Engine::Scene>("Forest");
    auto sceneB = std::make_shared<Engine::Scene>("Dungeon");

    Engine::SceneManager::LoadScene(sceneA);
    EXPECT_EQ(Engine::SceneManager::GetActiveScene(), sceneA);
    EXPECT_EQ(Engine::SceneManager::GetActiveScene()->GetName(), "Forest");

    Engine::SceneManager::LoadScene(sceneB);
    EXPECT_EQ(Engine::SceneManager::GetActiveScene(), sceneB);
    EXPECT_EQ(Engine::SceneManager::GetActiveScene()->GetName(), "Dungeon");

    Engine::SceneManager::Shutdown();
    EXPECT_EQ(Engine::SceneManager::GetActiveScene(), nullptr);
}

TEST(SceneRuntimeTest, RuntimeLifecycleState) {
    auto scene = std::make_shared<Engine::Scene>("PhysicsWorld");
    EXPECT_FALSE(scene->IsRuntimeRunning());

    scene->OnRuntimeStart();
    EXPECT_TRUE(scene->IsRuntimeRunning());

    scene->UpdateRuntime(0.016f);

    scene->OnRuntimeStop();
    EXPECT_FALSE(scene->IsRuntimeRunning());
}

TEST(SceneTest, OwnerThreadAffinityTrackingAndGuard) {
    Engine::Scene scene("ThreadAffinityScene");
    auto* existing = scene.CreateGameObject("MainThreadEntity");
    ASSERT_NE(existing, nullptr);

    EXPECT_TRUE(scene.IsOnOwnerThread());
    EXPECT_TRUE(scene.GetPhysicsWorld().IsOnOwnerThread());

    bool workerSawSceneOwner = true;
    bool workerSawPhysicsOwner = true;
    std::thread inspectorThread([&]() {
        workerSawSceneOwner = scene.IsOnOwnerThread();
        workerSawPhysicsOwner = scene.GetPhysicsWorld().IsOnOwnerThread();
    });
    inspectorThread.join();

#ifndef NDEBUG
    EXPECT_FALSE(workerSawSceneOwner);
    EXPECT_FALSE(workerSawPhysicsOwner);
#ifdef GTEST_HAS_DEATH_TEST
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    auto suppressCrtDialogs = []() {
#ifdef _MSC_VER
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    };
    EXPECT_DEATH(
        {
            suppressCrtDialogs();
            std::thread worker([&]() {
                scene.CreateGameObject("IllegalWorkerSpawn");
            });
            worker.join();
        },
        "Main-thread affinity violation");
    EXPECT_DEATH(
        {
            suppressCrtDialogs();
            std::thread worker([&]() {
                scene.DestroyGameObject(existing);
            });
            worker.join();
        },
        "Main-thread affinity violation");
#endif
#else
    EXPECT_TRUE(workerSawSceneOwner);
    EXPECT_TRUE(workerSawPhysicsOwner);
#endif
}

TEST(SceneTest, NonCopyableAndNonMovableForPointerStability) {
    static_assert(!std::is_copy_constructible_v<Engine::Scene>, "Scene must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<Engine::Scene>, "Scene must not be copy-assignable");
    static_assert(!std::is_move_constructible_v<Engine::Scene>, "Scene must not be move-constructible");
    static_assert(!std::is_move_assignable_v<Engine::Scene>, "Scene must not be move-assignable");
    SUCCEED();
}

class FixedUpdateCountingScript : public Engine::Component {
public:
    uint32_t FixedUpdateCount = 0;
    float LastDt = 0.0f;

    void FixedUpdate(float fixedDeltaTime) override {
        ++FixedUpdateCount;
        LastDt = fixedDeltaTime;
    }
};

TEST(SceneFixedUpdateTest, OnlyScriptedAndPhysicsEntitiesIterated) {
    Engine::Scene scene("FixedUpdateScene");

    // 100 standee props without FixedUpdate
    for (int i = 0; i < 100; ++i) {
        auto* prop = scene.CreateGameObject("Prop");
        EXPECT_FALSE(prop->HasFixedUpdate());
    }
    EXPECT_TRUE(scene.GetFixedUpdateEntities().empty());

    // 2 Rigidbody2D entities
    auto* rbGo1 = scene.CreateGameObject("Actor1");
    rbGo1->AddComponent<Engine::Rigidbody2D>();
    EXPECT_TRUE(rbGo1->HasFixedUpdate());

    auto* rbGo2 = scene.CreateGameObject("Actor2");
    rbGo2->AddComponent<Engine::Rigidbody2D>();
    EXPECT_TRUE(rbGo2->HasFixedUpdate());

    // 1 custom script overriding FixedUpdate
    auto* scriptGo = scene.CreateGameObject("ScriptedEntity");
    auto* script = scriptGo->AddComponent<FixedUpdateCountingScript>();
    EXPECT_TRUE(scriptGo->HasFixedUpdate());

    // FixedUpdate entity list contains exactly the 3 entities
    EXPECT_EQ(scene.GetFixedUpdateEntities().size(), 3u);

    // Run a step
    scene.OnRuntimeStart();
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(script->FixedUpdateCount, 1u);
    EXPECT_NEAR(script->LastDt, 1.0f / 60.0f, 0.001f);

    // Remove Rigidbody2D from Actor1 -> unregisters from FixedUpdate
    EXPECT_TRUE(rbGo1->RemoveComponent<Engine::Rigidbody2D>());
    EXPECT_FALSE(rbGo1->HasFixedUpdate());
    EXPECT_EQ(scene.GetFixedUpdateEntities().size(), 2u);

    // Destroy ScriptedEntity and flush queues
    scene.DestroyGameObject(scriptGo);
    scene.FlushEntityLifecycleQueues();
    EXPECT_EQ(scene.GetFixedUpdateEntities().size(), 1u);

    scene.OnRuntimeStop();
}

TEST(SceneTerrainCacheTest, SampleSurfaceAtXZIsO1AndInvalidates) {
    Engine::Scene scene("TerrainCacheScene");

    // Initially no terrain
    auto emptySample = scene.SampleSurfaceAtXZ(10.0f, 10.0f);
    EXPECT_NEAR(emptySample.LinearDamping, 4.5f, 1e-4f);
    EXPECT_NEAR(emptySample.MaxSpeedMultiplier, 1.0f, 1e-4f);

    // Add terrain entity
    auto* terrainGo = scene.CreateGameObject("MainTerrain");
    auto* tc = terrainGo->AddComponent<Engine::TerrainComponent>();
    ASSERT_NE(tc, nullptr);

    // Modifying terrain data or querying should use cached terrain
    auto sample1 = scene.SampleSurfaceAtXZ(0.0f, 0.0f);
    EXPECT_TRUE(std::isfinite(sample1.LinearDamping));

    // Adding non-terrain entity invalidates dirty flag but resolves cleanly
    auto* prop = scene.CreateGameObject("Tree");
    (void)prop;
    auto sample2 = scene.SampleSurfaceAtXZ(0.0f, 0.0f);
    EXPECT_EQ(sample1.LinearDamping, sample2.LinearDamping);

    // Destroying terrain entity and flushing returns to default
    scene.DestroyGameObject(terrainGo);
    scene.FlushEntityLifecycleQueues();
    auto afterDestroy = scene.SampleSurfaceAtXZ(0.0f, 0.0f);
    EXPECT_NEAR(afterDestroy.LinearDamping, 4.5f, 1e-4f);
    EXPECT_NEAR(afterDestroy.MaxSpeedMultiplier, 1.0f, 1e-4f);
}

} // namespace

