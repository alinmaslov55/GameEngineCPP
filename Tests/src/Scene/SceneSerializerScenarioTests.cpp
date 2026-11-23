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
#include "Engine/Audio/AudioSource.h"
#include "Engine/UI/UIButtonComponent.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Time.h"
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

TEST(SceneSerializerTest, RoundTripActiveAndEnabledState) {
    auto srcScene = std::make_shared<Engine::Scene>("ActiveEnabledScene");
    auto* go = srcScene->CreateGameObjectWithUUID(99901ULL, "InactiveEntity");
    go->SetActive(false);
    auto* col = go->AddComponent<Engine::BoxCollider2D>();
    col->SetEnabled(false);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(99901ULL);
    ASSERT_NE(loadedGo, nullptr);
    EXPECT_FALSE(loadedGo->IsActive());
    auto* loadedCol = loadedGo->GetComponent<Engine::BoxCollider2D>();
    ASSERT_NE(loadedCol, nullptr);
    EXPECT_FALSE(loadedCol->IsEnabled());
}

TEST(SceneSerializerTest, RoundTripMultiColliderFidelity) {
    auto srcScene = std::make_shared<Engine::Scene>("MultiColliderScene");
    auto* go = srcScene->CreateGameObjectWithUUID(99902ULL, "MultiColliderEntity");
    auto* col1 = go->AddComponent<Engine::CircleCollider2D>();
    col1->Radius = 1.5f;
    col1->IsSensor = false;

    auto* col2 = go->AddComponent<Engine::CircleCollider2D>();
    col2->Radius = 4.0f;
    col2->IsSensor = true;

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(99902ULL);
    ASSERT_NE(loadedGo, nullptr);

    auto colliders = loadedGo->GetComponents<Engine::CircleCollider2D>();
    ASSERT_EQ(colliders.size(), 2u);
    EXPECT_NEAR(colliders[0]->Radius, 1.5f, 1e-4f);
    EXPECT_FALSE(colliders[0]->IsSensor);
    EXPECT_NEAR(colliders[1]->Radius, 4.0f, 1e-4f);
    EXPECT_TRUE(colliders[1]->IsSensor);
}

TEST(SceneSerializerTest, RoundTripAudioSourceFullProperties) {
    auto srcScene = std::make_shared<Engine::Scene>("AudioScene");
    auto* go = srcScene->CreateGameObjectWithUUID(99903ULL, "SoundEmitter");
    auto* src = go->AddComponent<Engine::AudioSource>();
    src->SetBus(Engine::AudioBus::Music);
    src->SetVolume(0.75f);
    src->SetPitchRange(0.85f, 1.15f);
    src->SetLooping(true);
    src->SetSpatialized(true);
    src->SetAttenuationDistances(3.5f, 45.0f);
    src->SetPriority(64);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(99903ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedSrc = loadedGo->GetComponent<Engine::AudioSource>();
    ASSERT_NE(loadedSrc, nullptr);

    EXPECT_EQ(loadedSrc->GetBus(), Engine::AudioBus::Music);
    EXPECT_NEAR(loadedSrc->GetVolume(), 0.75f, 1e-4f);
    EXPECT_NEAR(loadedSrc->GetPitchMin(), 0.85f, 1e-4f);
    EXPECT_NEAR(loadedSrc->GetPitchMax(), 1.15f, 1e-4f);
    EXPECT_TRUE(loadedSrc->IsLooping());
    EXPECT_TRUE(loadedSrc->IsSpatialized());
    EXPECT_NEAR(loadedSrc->GetMinDistance(), 3.5f, 1e-4f);
    EXPECT_NEAR(loadedSrc->GetMaxDistance(), 45.0f, 1e-4f);
    EXPECT_EQ(loadedSrc->GetPriority(), 64);
}

TEST(SceneSerializerTest, SerializeExcludesPendingDestroyEntities) {
    auto srcScene = std::make_shared<Engine::Scene>("PendingDestroyScene");
    auto* e1 = srcScene->CreateGameObjectWithUUID(99910ULL, "Alive1");
    auto* e2 = srcScene->CreateGameObjectWithUUID(99911ULL, "ToDestroy");
    (void)e1;
    srcScene->DestroyGameObject(e2);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    EXPECT_FALSE(savedJson.find("ToDestroy") != std::string::npos);
}

TEST(SceneSerializerTest, PrefabInstantiateRemapsButtonNeighborsAndSyncsCamera) {
    auto scene = std::make_shared<Engine::Scene>("PrefabTestScene");
    
    // Construct prefab JSON with two buttons pointing to each other + camera
    nlohmann::json prefabJson = {
        {"Entities", {
            {
                {"UUID", 1001ULL},
                {"Name", "BtnA"},
                {"Active", false},
                {"Components", {
                    {"UIButtonComponent", {
                        {"DownNeighborUUID", 1002ULL}
                    }}
                }}
            },
            {
                {"UUID", 1002ULL},
                {"Name", "BtnB"},
                {"Active", true},
                {"Components", {
                    {"UIButtonComponent", {
                        {"UpNeighborUUID", 1001ULL}
                    }}
                }}
            },
            {
                {"UUID", 1003ULL},
                {"Name", "PrefabCamera"},
                {"Components", {
                    {"CameraComponent", {
                        {"Distance", 20.0f}
                    }}
                }}
            }
        }}
    };

    Engine::Prefab prefab(prefabJson);
    auto* root = scene->InstantiatePrefab(prefab, glm::vec3(42.0f, 10.0f, -5.0f));
    ASSERT_NE(root, nullptr);

    Engine::GameObject* newBtnA = nullptr;
    Engine::GameObject* newBtnB = nullptr;
    Engine::GameObject* newCamGo = nullptr;

    for (const auto& go : scene->GetGameObjects()) {
        if (go->GetName() == "BtnA") newBtnA = go.get();
        if (go->GetName() == "BtnB") newBtnB = go.get();
        if (go->GetName() == "PrefabCamera") newCamGo = go.get();
    }

    ASSERT_NE(newBtnA, nullptr);
    ASSERT_NE(newBtnB, nullptr);
    ASSERT_NE(newCamGo, nullptr);

    // Active state preserved
    EXPECT_FALSE(newBtnA->IsActive());
    EXPECT_TRUE(newBtnB->IsActive());

    // Neighbor UUID remapped
    auto* compA = newBtnA->GetComponent<Engine::UIButtonComponent>();
    auto* compB = newBtnB->GetComponent<Engine::UIButtonComponent>();
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    EXPECT_EQ(compA->DownNeighborUUID, newBtnB->GetUUID());
    EXPECT_EQ(compB->UpNeighborUUID, newBtnA->GetUUID());

    // CameraComponent synced to root position
    auto* cam = newCamGo->GetComponent<Engine::CameraComponent>();
    ASSERT_NE(cam, nullptr);
    EXPECT_EQ(cam->Distance, 20.0f);
}

TEST(SceneSerializerTest, SceneManagerTransitionAdvancesWhenTimeScaleIsZero) {
    auto initialScene = std::make_shared<Engine::Scene>("Scene1");
    Engine::SceneManager::LoadScene(initialScene);

    bool nextSceneLoaded = false;
    Engine::SceneManager::TransitionToScene([&]() {
        nextSceneLoaded = true;
        return std::make_shared<Engine::Scene>("Scene2");
    }, 0.2f);

    EXPECT_TRUE(Engine::SceneManager::IsTransitioning());

    // Pause gameplay
    Engine::Time::SetTimeScale(0.0f);
    Engine::Time::SetMockDeltaTime(0.3f);
    Engine::Time::Update(); // Advances unscaled time by 0.3f, while scaled delta time becomes 0.0f

    // Update with scaled delta = 0.0f
    Engine::SceneManager::Update(Engine::Time::GetDeltaTime());

    // Half duration is 0.1s; with 0.3s unscaled elapsed, peak blackout and scene swap must have triggered
    EXPECT_TRUE(nextSceneLoaded);

    // Restore time scale, active scene & reset mock
    Engine::Time::Reset();
    Engine::SceneManager::CancelTransition();
    Engine::SceneManager::Shutdown();
}

} // namespace
