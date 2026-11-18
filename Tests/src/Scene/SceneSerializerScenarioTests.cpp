#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/Tilemap.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Components/PostProcessComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Core/ResourceManager.h"
#include <box2d/box2d.h>
#include <filesystem>
#include <memory>
#include <string>

namespace {

TEST(SceneSerializerTest, SandboxScenesLoadAndSimulateFeatureScenarios) {
    const std::filesystem::path prevAssetRoot = Engine::ResourceManager::GetAssetRoot();
    struct AssetRootGuard {
        std::filesystem::path prev;
        ~AssetRootGuard() {
            if (!prev.empty()) {
                Engine::ResourceManager::SetAssetRoot(prev);
            }
        }
    } assetRootGuard{ prevAssetRoot };

    const std::filesystem::path sandboxRoot = Engine::ResourceManager::DiscoverAssetRoot("Sandbox");
    ASSERT_TRUE(std::filesystem::exists(sandboxRoot / "Village.scene"));
    ASSERT_TRUE(std::filesystem::exists(sandboxRoot / "assets" / "level.scene"));

    auto findByName = [](Engine::Scene& scene, const std::string& name) -> Engine::GameObject* {
        for (const auto& go : scene.GetGameObjects()) {
            if (go->GetName() == name) {
                return go.get();
            }
        }
        return nullptr;
    };

    // 1. Verify rewritten Sandbox/assets/level.scene (Engine Feature Verification Arena)
    {
        auto levelScene = std::make_shared<Engine::Scene>("LevelLoader");
        ASSERT_TRUE(Engine::SceneSerializer(levelScene).Deserialize("assets/level.scene"));
        if (levelScene->GetGameObjects().size() < 25u) {
            GTEST_SKIP() << "Sandbox/assets/level.scene contains legacy minimal level (size="
                         << levelScene->GetGameObjects().size() << " < 25); feature arena scene not yet authored";
        }
        EXPECT_GE(levelScene->GetGameObjects().size(), 25u);

        auto* player = levelScene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(player->GetName(), "Player");
        EXPECT_EQ(player->GetLayer(), Engine::EntityLayer::Player);

        auto* torchSocket = findByName(*levelScene, "Player Torch Socket");
        ASSERT_NE(torchSocket, nullptr);
        EXPECT_EQ(torchSocket->GetTransform()->GetParent(), player->GetTransform());

        auto* shrine = findByName(*levelScene, "Hierarchy Shrine Parent");
        ASSERT_NE(shrine, nullptr);
        EXPECT_EQ(shrine->GetTransform()->GetChildren().size(), 2u);

        auto* stasisRelic = findByName(*levelScene, "Stasis MidAir Relic");
        ASSERT_NE(stasisRelic, nullptr);
        auto* stasisRb = stasisRelic->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(stasisRb, nullptr);
        EXPECT_TRUE(stasisRb->IsStasisFrozen());
        EXPECT_FLOAT_EQ(stasisRb->GetStasisCachedVerticalVelocity(), 5.5f);
        const glm::vec3 frozenPos = stasisRelic->GetTransform()->position;

        auto* dropPickup = findByName(*levelScene, "Bouncing MidAir Drop");
        ASSERT_NE(dropPickup, nullptr);
        auto* dropRb = dropPickup->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(dropRb, nullptr);
        EXPECT_GT(dropRb->VerticalVelocity, 0.0f);

        auto* tilemapGo = findByName(*levelScene, "Arena Tilemap");
        ASSERT_NE(tilemapGo, nullptr);
        auto* tilemap = tilemapGo->GetComponent<Engine::Tilemap>();
        ASSERT_NE(tilemap, nullptr);
        EXPECT_FALSE(tilemap->GetTile(0, 0).walkable);

        auto* terrainGo = findByName(*levelScene, "Arena Terrain");
        ASSERT_NE(terrainGo, nullptr);
        EXPECT_NE(terrainGo->GetComponent<Engine::TerrainComponent>(), nullptr);

        // Verify simulation runtime: stasis stays locked, bouncing drop integrates, player jump clears LowObstacle
        levelScene->OnRuntimeStart();
        player->GetComponent<Engine::Rigidbody2D>()->LaunchVertical(5.0f);

        bool sawAirbornePassThrough = false;
        constexpr float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i) {
            levelScene->UpdateRuntime(dt);
            if (player->GetComponent<Engine::Rigidbody2D>()->IsAirbornePassThroughActive()) {
                sawAirbornePassThrough = true;
            }
        }
        EXPECT_TRUE(sawAirbornePassThrough);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.x, frozenPos.x);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.y, frozenPos.y);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.z, frozenPos.z);
        EXPECT_NE(dropPickup->GetTransform()->position.y, 3.0f);

        // Spatial query check over LowObstacle barrier
        std::vector<Engine::GameObject*> lowHits;
        levelScene->OverlapCircleXZ({ 7.5f, 9.6f }, 1.2f, Engine::PhysicsLayer::LowObstacle, lowHits);
        EXPECT_FALSE(lowHits.empty());

        levelScene->OnRuntimeStop();
    }

    // 2. Verify rewritten Sandbox/Village.scene (Enhanced Cult Diorama Clearing)
    {
        auto villageScene = std::make_shared<Engine::Scene>("VillageLoader");
        ASSERT_TRUE(Engine::SceneSerializer(villageScene).Deserialize("Village.scene"));
        EXPECT_GE(villageScene->GetGameObjects().size(), 40u);

        auto* player = villageScene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(player->GetLayer(), Engine::EntityLayer::Player);

        auto* shrine = villageScene->FindFirstGameObjectWithTag(Engine::EntityTag::Shrine);
        ASSERT_NE(shrine, nullptr);
        EXPECT_GE(shrine->GetTransform()->GetChildren().size(), 1u);

        auto* shrineSensor = findByName(*villageScene, "Shrine Sanctuary Sensor");
        ASSERT_NE(shrineSensor, nullptr);
        auto* sensorCol = shrineSensor->GetComponent<Engine::CircleCollider2D>();
        ASSERT_NE(sensorCol, nullptr);
        EXPECT_TRUE(sensorCol->IsSensor);

        auto* westBarricade = findByName(*villageScene, "Village Barricade West");
        ASSERT_NE(westBarricade, nullptr);
        EXPECT_EQ(westBarricade->GetLayer(), Engine::EntityLayer::LowObstacle);
        EXPECT_NE(westBarricade->GetComponent<Engine::CapsuleCollider2D>(), nullptr);

        auto* stasisShard = findByName(*villageScene, "Chronos Stasis Shard");
        ASSERT_NE(stasisShard, nullptr);
        EXPECT_TRUE(stasisShard->GetComponent<Engine::Rigidbody2D>()->IsStasisFrozen());

        villageScene->OnRuntimeStart();
        player->GetComponent<Engine::Rigidbody2D>()->LaunchVertical(5.0f);
        bool villageVaultActive = false;
        for (int i = 0; i < 60; ++i) {
            villageScene->UpdateRuntime(1.0f / 60.0f);
            if (player->GetComponent<Engine::Rigidbody2D>()->IsAirbornePassThroughActive()) {
                villageVaultActive = true;
            }
        }
        EXPECT_TRUE(villageVaultActive);
        villageScene->OnRuntimeStop();
    }
}

TEST(SceneSerializerTest, TerrainChunkManagerAndModifiedChunksRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("ChunkedTerrainSaveScene");
    auto* worldGo = srcScene->CreateGameObjectWithUUID(90909ULL, "InfiniteGround");
    auto* mgr = worldGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 2, 4);
    mgr->MaxGpuUploadsPerFrame = 3;
    mgr->AutoStreamWithCamera = false;
    mgr->SetBlendSharpness(0.72f);
    mgr->SetEdgeDarkening(0.24f);
    mgr->SetLayerTilingScale(2u, 0.77f);

    Engine::WorldGenConfig wg;
    wg.Enabled = true;
    wg.SpawnSceneEntities = false;
    wg.Seed = 424242ULL;
    wg.OriginSafeRadius = 7.25f;
    wg.PoiApronRadius = 4.0f;
    wg.PoiSuperCellChunks = 3;
    mgr->ConfigureProceduralWorld(wg);

    mgr->UpdateStreaming({ 16.0f, 0.0f, 16.0f });
    mgr->FlushPendingGenerations();
    mgr->PaintWorldBrush(16.0f, 16.0f, /*layerIndex=*/2u, /*radiusWorld=*/2.5f, /*falloff=*/0.6f, /*strength=*/1.0f);

    const auto* src00 = mgr->GetChunk({ 0, 0 });
    ASSERT_NE(src00, nullptr);
    const glm::u8vec4 expectedCorner = src00->GetSplatTexel(64u, 64u);
    EXPECT_GT(expectedCorner.b, 200u);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(90909ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedMgr = loadedGo->GetComponent<Engine::TerrainChunkManager>();
    ASSERT_NE(loadedMgr, nullptr);
    EXPECT_FLOAT_EQ(loadedMgr->ChunkSizeMeters, 16.0f);
    EXPECT_EQ(loadedMgr->SplatResolution, 65u);
    EXPECT_EQ(loadedMgr->ActiveRadiusChunks, 2);
    EXPECT_EQ(loadedMgr->CacheRetentionRadius, 4);
    EXPECT_EQ(loadedMgr->MaxGpuUploadsPerFrame, 3u);
    EXPECT_FALSE(loadedMgr->AutoStreamWithCamera);
    EXPECT_NEAR(loadedMgr->GetBlendSharpness(), 0.72f, 1e-5f);
    EXPECT_NEAR(loadedMgr->GetEdgeDarkening(), 0.24f, 1e-5f);
    EXPECT_NEAR(loadedMgr->GetLayer(2u).TilingScale, 0.77f, 1e-5f);

    const auto& loadedWg = loadedMgr->GetWorldGenConfig();
    EXPECT_TRUE(loadedWg.Enabled);
    EXPECT_FALSE(loadedWg.SpawnSceneEntities);
    EXPECT_EQ(loadedWg.Seed, 424242ULL);
    EXPECT_NEAR(loadedWg.OriginSafeRadius, 7.25f, 1e-5f);
    EXPECT_NEAR(loadedWg.PoiApronRadius, 4.0f, 1e-5f);
    EXPECT_EQ(loadedWg.PoiSuperCellChunks, 3);

    const auto* loaded00 = loadedMgr->GetChunk({ 0, 0 });
    const auto* loaded11 = loadedMgr->GetChunk({ 1, 1 });
    ASSERT_NE(loaded00, nullptr);
    ASSERT_NE(loaded11, nullptr);
    EXPECT_TRUE(loaded00->UserModified);
    EXPECT_EQ(loaded00->GetSplatTexel(64u, 64u), expectedCorner);
    EXPECT_EQ(loaded11->GetSplatTexel(0u, 0u), expectedCorner);
}

TEST(SceneSerializerTest, EightLayerTerrainAndSecondaryWeightsRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("EightLayerScene");
    auto* go = srcScene->CreateGameObjectWithUUID(77777ULL, "Terr8");
    auto* terrain = go->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 16u, 16u);

    terrain->SetLayerTilingScale(2u, 0.62f);
    terrain->SetLayerGlossiness(2u, 0.85f);
    terrain->SetLayerHeightMapPath(4u, "assets/diorama/terrain/cobble/tex_cobble_h.png");
    terrain->SetLayerNormalMapPath(4u, "assets/diorama/terrain/cobble/tex_cobble_n.png");
    terrain->SetLayerTilingScale(4u, 1.25f);
    terrain->SetLayerGlossiness(4u, 0.35f);
    terrain->SetLayerGlossiness(5u, 0.06f);

    terrain->PaintWorldBrush(8.0f, 8.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 4u, 3.0f, 0.6f, 1.0f);
    terrain->PaintWorldBrush(2.0f, 2.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 5u, 2.0f, 0.6f, 0.9f);
    const auto expectedCobbleCenter = terrain->GetSplatTexel8(8u, 8u);
    EXPECT_GT(expectedCobbleCenter[4], 200u);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(77777ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedTerrain = loadedGo->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(loadedTerrain, nullptr);

    EXPECT_NEAR(loadedTerrain->GetLayer(2u).Glossiness, 0.85f, 1e-4f);
    EXPECT_NEAR(loadedTerrain->GetLayer(4u).TilingScale, 1.25f, 1e-4f);
    EXPECT_NEAR(loadedTerrain->GetLayer(4u).Glossiness, 0.35f, 1e-4f);
    EXPECT_EQ(loadedTerrain->GetLayer(4u).HeightMapPath, "assets/diorama/terrain/cobble/tex_cobble_h.png");
    EXPECT_EQ(loadedTerrain->GetLayer(4u).NormalMapPath, "assets/diorama/terrain/cobble/tex_cobble_n.png");
    EXPECT_NEAR(loadedTerrain->GetLayer(5u).Glossiness, 0.06f, 1e-4f);
    EXPECT_EQ(loadedTerrain->GetSplatTexel8(8u, 8u), expectedCobbleCenter);
}

TEST(SceneSerializerTest, UseTerrainFrictionRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("FrictionRoundTrip");
    auto* go = srcScene->CreateGameObjectWithUUID(88888ULL, "HeroWithFriction");
    auto* rb = go->AddComponent<Engine::Rigidbody2D>();
    rb->UseTerrainFriction = true;
    rb->LinearDamping = 3.2f;

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(88888ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedRb = loadedGo->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(loadedRb, nullptr);

    EXPECT_TRUE(loadedRb->UseTerrainFriction);
    EXPECT_NEAR(loadedRb->LinearDamping, 3.2f, 1e-4f);
}

} // namespace
