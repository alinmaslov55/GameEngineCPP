#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/GameObject.h"

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

} // namespace

