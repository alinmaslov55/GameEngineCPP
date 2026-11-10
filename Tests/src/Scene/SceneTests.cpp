#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Physics/PhysicsWorld2D.h"

#include <thread>
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

} // namespace

