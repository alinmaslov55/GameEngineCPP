#include <gtest/gtest.h>
#include "Engine/Core/Layer.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include <filesystem>
#include <memory>

namespace {

TEST(LayerTest, CustomLayerLifecycleCallbacks) {
    class MockLayer : public Engine::Layer {
    public:
        MockLayer() : Engine::Layer("MockEditorLayer") {}
        int attachCount = 0;
        int detachCount = 0;
#ifdef ENGINE_BUILD_EDITOR
        int imguiCount = 0;
#endif
        float accumulatedDt = 0.0f;

        void OnAttach() override { ++attachCount; }
        void OnDetach() override { ++detachCount; }
        void OnUpdate(float deltaTime) override { accumulatedDt += deltaTime; }
#ifdef ENGINE_BUILD_EDITOR
        void OnImGuiRender() override { ++imguiCount; }
#endif
    };

    auto layer = std::make_unique<MockLayer>();
    EXPECT_EQ(layer->GetName(), "MockEditorLayer");

    layer->OnAttach();
    layer->OnUpdate(0.016f);
    layer->OnUpdate(0.032f);
#ifdef ENGINE_BUILD_EDITOR
    layer->OnImGuiRender();
#endif
    layer->OnDetach();

    EXPECT_EQ(layer->attachCount, 1);
    EXPECT_NEAR(layer->accumulatedDt, 0.048f, 1e-5f);
#ifdef ENGINE_BUILD_EDITOR
    EXPECT_EQ(layer->imguiCount, 1);
#endif
    EXPECT_EQ(layer->detachCount, 1);
}

TEST(BuildConfigurationTest, StandaloneSceneFileStartupAndMissingFileFailure) {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_dist_startup_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    Engine::ResourceManager::SetAssetRoot(tempDir);

    // 1. Missing startup scene must fail cleanly
    auto missingScene = std::make_shared<Engine::Scene>("Missing");
    EXPECT_FALSE(Engine::SceneSerializer(missingScene).Deserialize("nonexistent_startup.scene"));

    // 2. Author a portable startup scene on disk and boot it through the standalone runtime lifecycle
    {
        auto authoredScene = std::make_shared<Engine::Scene>("StandaloneLevel1");
        authoredScene->SetOrthographicSize(5.0f);

        auto* player = authoredScene->CreateGameObject("Player");
        player->GetTransform()->position = {2.0f, 3.0f, 4.0f};
        auto* rb = player->AddComponent<Engine::Rigidbody2D>();
        rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
        auto* bc = player->AddComponent<Engine::BoxCollider2D>();
        bc->size = {0.6f, 0.6f};

        auto* map = authoredScene->CreateGameObject("Map");
        auto* tilemap = map->AddComponent<Engine::Tilemap>(4, 4);
        tilemap->SetTile(1, 1, 2, false);

        ASSERT_TRUE(Engine::SceneSerializer(authoredScene).Serialize("level1.scene"));
    }

    auto activeScene = std::make_shared<Engine::Scene>("Empty");
    ASSERT_TRUE(Engine::SceneSerializer(activeScene).Deserialize("level1.scene"));
    EXPECT_EQ(activeScene->GetName(), "StandaloneLevel1");

    activeScene->OnViewportResize(1920, 1080);
    activeScene->OnRuntimeStart();
    EXPECT_TRUE(activeScene->IsRuntimeRunning());

    // Step simulation without any Framebuffer or ImGui context
    for (int i = 0; i < 10; ++i) {
        activeScene->UpdateRuntime(1.0f / 60.0f);
    }

    Engine::GameObject* loadedPlayer = nullptr;
    for (const auto& go : activeScene->GetGameObjects()) {
        if (go->GetName() == "Player") {
            loadedPlayer = go.get();
            break;
        }
    }
    ASSERT_NE(loadedPlayer, nullptr);
    EXPECT_LT(loadedPlayer->GetTransform()->position.y, 3.0f);

    activeScene->OnRuntimeStop();
    EXPECT_FALSE(activeScene->IsRuntimeRunning());

    Engine::ResourceManager::SetAssetRoot({});
    std::filesystem::remove_all(tempDir, ec);
}

} // namespace

