#include <gtest/gtest.h>
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/WorldBiomeGenerator.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Time.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

TEST(BiomeAndScatterTest, NoiseGeneratorDeterminismRangeAndDomainWarp) {
    const Engine::NoiseGenerator noiseA(42ULL);
    const Engine::NoiseGenerator noiseB(42ULL);
    const Engine::NoiseGenerator noiseDiff(1337ULL);

    bool sawDifferenceWithSeed = false;
    for (int z = -10; z <= 10; ++z) {
        for (int x = -10; x <= 10; ++x) {
            const float px = static_cast<float>(x) * 3.7f;
            const float pz = static_cast<float>(z) * 4.3f;

            const float sA = noiseA.Simplex2D(px * 0.1f, pz * 0.1f);
            const float sB = noiseB.Simplex2D(px * 0.1f, pz * 0.1f);
            EXPECT_GE(sA, -1.0f);
            EXPECT_LE(sA, 1.0f);
            EXPECT_FLOAT_EQ(sA, sB);

            const float fA = noiseA.FBm2D(px * 0.02f, pz * 0.02f);
            const float fB = noiseB.FBm2D(px * 0.02f, pz * 0.02f);
            EXPECT_GE(fA, -1.0f);
            EXPECT_LE(fA, 1.0f);
            EXPECT_FLOAT_EQ(fA, fB);

            const glm::vec2 wA = noiseA.DomainWarp2D(px, pz);
            const glm::vec2 wB = noiseB.DomainWarp2D(px, pz);
            EXPECT_FLOAT_EQ(wA.x, wB.x);
            EXPECT_FLOAT_EQ(wA.y, wB.y);

            if (std::abs(sA - noiseDiff.Simplex2D(px * 0.1f, pz * 0.1f)) > 1e-4f) {
                sawDifferenceWithSeed = true;
            }
        }
    }
    EXPECT_TRUE(sawDifferenceWithSeed);
}

TEST(BiomeAndScatterTest, DeterministicRepeatabilityAndOriginSafeClearing) {
    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = false;
    cfg.Seed = 42ULL;

    Engine::Scene sceneA("GenA");
    auto* goA = sceneA.CreateGameObject("TerrainA");
    auto* mgrA = goA->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);
    mgrA->ConfigureProceduralWorld(cfg);
    mgrA->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    mgrA->FlushPendingGenerations();

    Engine::Scene sceneB("GenB");
    auto* goB = sceneB.CreateGameObject("TerrainB");
    auto* mgrB = goB->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);
    mgrB->ConfigureProceduralWorld(cfg);
    mgrB->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    mgrB->FlushPendingGenerations();

    ASSERT_EQ(mgrA->GetActiveChunkCount(), 49u);
    ASSERT_EQ(mgrB->GetActiveChunkCount(), 49u);

    size_t totalProps = 0;
    size_t totalPOIs = 0;
    for (int32_t cz = -3; cz <= 3; ++cz) {
        for (int32_t cx = -3; cx <= 3; ++cx) {
            const auto* chunkA = mgrA->GetChunk({ cx, cz });
            const auto* chunkB = mgrB->GetChunk({ cx, cz });
            ASSERT_NE(chunkA, nullptr);
            ASSERT_NE(chunkB, nullptr);

            EXPECT_EQ(chunkA->SplatData, chunkB->SplatData);
            EXPECT_EQ(chunkA->BiomeMap, chunkB->BiomeMap);
            ASSERT_EQ(chunkA->Props.size(), chunkB->Props.size());
            ASSERT_EQ(chunkA->POIs.size(), chunkB->POIs.size());

            for (size_t i = 0; i < chunkA->Props.size(); ++i) {
                EXPECT_EQ(chunkA->Props[i].Kind, chunkB->Props[i].Kind);
                EXPECT_EQ(chunkA->Props[i].WorldPosition, chunkB->Props[i].WorldPosition);
                const glm::vec2 xz{ chunkA->Props[i].WorldPosition.x, chunkA->Props[i].WorldPosition.z };
                if (chunkA->Props[i].Blocking) {
                    EXPECT_GE(glm::length(xz), 4.0f);
                    EXPECT_GE(glm::length(xz), cfg.OriginSafeRadius - 1e-4f);
                }
                ++totalProps;
            }
            for (size_t i = 0; i < chunkA->POIs.size(); ++i) {
                EXPECT_EQ(chunkA->POIs[i].Kind, chunkB->POIs[i].Kind);
                EXPECT_EQ(chunkA->POIs[i].WorldPosition, chunkB->POIs[i].WorldPosition);
                const glm::vec2 xz{ chunkA->POIs[i].WorldPosition.x, chunkA->POIs[i].WorldPosition.z };
                EXPECT_GE(glm::length(xz), 4.0f);
                ++totalPOIs;
            }
        }
    }
    EXPECT_GT(totalProps, 0u);
    EXPECT_GE(totalPOIs, 4u); // At least 2x2 super-cells overlap the 7x7 window

    // Origin safe clearing blends SanctuaryCobble + AshBarrens cleanly around (0, 0)
    const Engine::BiomeSample centerBio = mgrA->SampleBiomeAtXZ(0.0f, 0.0f);
    const float safeWeight = centerBio.Weights[static_cast<size_t>(Engine::BiomeType::SanctuaryCobble)] +
                             centerBio.Weights[static_cast<size_t>(Engine::BiomeType::AshBarrens)];
    EXPECT_GT(safeWeight, 0.99f);
    EXPECT_EQ(centerBio.DominantBiome, Engine::BiomeType::SanctuaryCobble);

    // Adjacent chunks generated with NoiseGenerator share bit-exact edge texels
    const auto* c00 = mgrA->GetChunk({ 0, 0 });
    const auto* c10 = mgrA->GetChunk({ 1, 0 });
    const auto* c01 = mgrA->GetChunk({ 0, 1 });
    for (uint32_t i = 0; i < 65u; ++i) {
        EXPECT_EQ(c00->GetSplatTexel(64u, i), c10->GetSplatTexel(0u, i));
        EXPECT_EQ(c00->GetSplatTexel(i, 64u), c01->GetSplatTexel(i, 0u));
    }
}

TEST(BiomeAndScatterTest, CrossChunkPoissonMinSeparationAndSuperCellPOIs) {
    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = false;
    cfg.Seed = 42ULL;

    Engine::Scene scene("SeparationTest");
    auto* go = scene.CreateGameObject("Terrain");
    auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 4, 8);
    mgr->ConfigureProceduralWorld(cfg);

    // Explore 9x9 chunks centered on (2, 2) to cover multiple Blackwood regions and full super-cell (0, 0)
    mgr->UpdateStreaming({ 32.0f, 0.0f, 32.0f });
    mgr->FlushPendingGenerations();

    std::vector<glm::vec2> pinePositions;
    std::vector<glm::vec2> shrubPositions;
    std::vector<glm::vec2> grassPositions;
    std::vector<Engine::POISpawnDesc> superCell00POIs;

    for (const auto& [coord, chunk] : mgr->GetChunks()) {
        for (const auto& prop : chunk.Props) {
            const glm::vec2 xz{ prop.WorldPosition.x, prop.WorldPosition.z };
            if (prop.Kind == Engine::ProceduralPropKind::ConiferPine) {
                pinePositions.push_back(xz);
            } else if (prop.Kind == Engine::ProceduralPropKind::BerryShrub) {
                shrubPositions.push_back(xz);
            } else if (prop.Kind == Engine::ProceduralPropKind::TallGrassCluster) {
                grassPositions.push_back(xz);
            }
        }
        for (const auto& poi : chunk.POIs) {
            if (poi.SuperCellX == 0 && poi.SuperCellZ == 0) {
                superCell00POIs.push_back(poi);
            }
            // POI plaza base blends to SanctuaryCobble
            const Engine::BiomeSample baseBio = mgr->SampleBiomeAtXZ(poi.WorldPosition.x, poi.WorldPosition.z);
            EXPECT_EQ(baseBio.DominantBiome, Engine::BiomeType::SanctuaryCobble);
        }
        (void)coord;
    }

    // 1. Exactly 1 guaranteed POI inside 3x3 chunk super-cell (0, 0)
    ASSERT_EQ(superCell00POIs.size(), 1u);

    // 2. Pairwise min-distance invariants hold both within chunks AND across chunk borders
    for (size_t i = 0; i < pinePositions.size(); ++i) {
        for (size_t j = i + 1; j < pinePositions.size(); ++j) {
            EXPECT_GE(glm::length(pinePositions[i] - pinePositions[j]), 3.2f - 1e-4f);
        }
    }
    for (size_t i = 0; i < shrubPositions.size(); ++i) {
        for (size_t j = i + 1; j < shrubPositions.size(); ++j) {
            EXPECT_GE(glm::length(shrubPositions[i] - shrubPositions[j]), 2.0f - 1e-4f);
        }
    }
    for (size_t i = 0; i < grassPositions.size(); ++i) {
        for (size_t j = i + 1; j < grassPositions.size(); ++j) {
            EXPECT_GE(glm::length(grassPositions[i] - grassPositions[j]), 0.9f - 1e-4f);
        }
    }
}

TEST(BiomeAndScatterTest, PooledEntityStreamingLifecycleAndSafeHarvestDestroy) {
    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = true;
    cfg.Seed = 42ULL;

    Engine::Scene scene("PooledPropsTest");
    auto* go = scene.CreateGameObject("Terrain");
    auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 1, 2);
    mgr->ConfigureProceduralWorld(cfg);

    // Stream 3x3 active window around (0, 0)
    mgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    mgr->FlushPendingGenerations();

    size_t spawnedUUIDCount = 0;
    uint64_t sampleDestroyUUID = 0;
    for (const auto& [coord, chunk] : mgr->GetChunks()) {
        (void)coord;
        spawnedUUIDCount += chunk.SpawnedEntityUUIDs.size();
        if (sampleDestroyUUID == 0 && !chunk.SpawnedEntityUUIDs.empty()) {
            sampleDestroyUUID = chunk.SpawnedEntityUUIDs.front();
        }
    }
    EXPECT_GT(spawnedUUIDCount, 0u);

    // Simulate chopping down a procedural tree before chunk demotion
    if (sampleDestroyUUID != 0) {
        if (Engine::GameObject* victim = scene.FindGameObjectByUUID(sampleDestroyUUID)) {
            scene.DestroyGameObject(victim);
        }
    }

    // Move out so original chunks leave ActiveRadius (1) and enter RAM Cache / Eviction -> pooled props return safely
    mgr->UpdateStreaming({ 80.0f, 0.0f, 80.0f });
    mgr->FlushPendingGenerations();

    const size_t totalFreePooled =
        scene.GetPoolManager().GetPoolFreeCount("proc_conifer_pine") +
        scene.GetPoolManager().GetPoolFreeCount("proc_berry_shrub") +
        scene.GetPoolManager().GetPoolFreeCount("proc_tall_grass") +
        scene.GetPoolManager().GetPoolFreeCount("proc_dead_snag") +
        scene.GetPoolManager().GetPoolFreeCount("proc_bog_bubble") +
        scene.GetPoolManager().GetPoolFreeCount("proc_broken_fence") +
        scene.GetPoolManager().GetPoolFreeCount("proc_bone_pile") +
        scene.GetPoolManager().GetPoolFreeCount("proc_poi_dead_brazier") +
        scene.GetPoolManager().GetPoolFreeCount("proc_poi_umbral_shrine");
    EXPECT_GT(totalFreePooled, 0u);
    SUCCEED();
}

TEST(TerrainSurfaceSamplerTest, SurfacePropertiesAcrossPureBiomesAndBilinearBlend) {
    Engine::TerrainComponent terrain(16.0f, 16.0f, 128u, 128u);

    // 1. Pure Chalk (Layer 3)
    terrain.FillLayer(3u);
    Engine::SurfaceSample chalkSample = terrain.SampleSurfaceAtXZ(8.0f, 8.0f);
    EXPECT_EQ(chalkSample.DominantBiome, Engine::BiomeType::ChalkHighlands);
    EXPECT_NEAR(chalkSample.LinearDamping, 1.1f, 1e-4f);
    EXPECT_NEAR(chalkSample.MaxSpeedMultiplier, 0.95f, 1e-4f);
    EXPECT_NEAR(chalkSample.Flammability, 0.0f, 1e-4f);
    EXPECT_FALSE(chalkSample.IsBurning);

    // 2. Pure Tar (Layer 2)
    terrain.FillLayer(2u);
    Engine::SurfaceSample tarSample = terrain.SampleSurfaceAtXZ(8.0f, 8.0f);
    EXPECT_EQ(tarSample.DominantBiome, Engine::BiomeType::TarBogs);
    EXPECT_NEAR(tarSample.LinearDamping, 9.0f, 1e-4f);
    EXPECT_NEAR(tarSample.MaxSpeedMultiplier, 0.70f, 1e-4f);
    EXPECT_NEAR(tarSample.Flammability, 1.0f, 1e-4f);
    EXPECT_FALSE(tarSample.IsBurning);

    // 3. Pure Cobblestone (Layer 4)
    terrain.FillLayer(4u);
    Engine::SurfaceSample cobbleSample = terrain.SampleSurfaceAtXZ(8.0f, 8.0f);
    EXPECT_EQ(cobbleSample.DominantBiome, Engine::BiomeType::SanctuaryCobble);
    EXPECT_NEAR(cobbleSample.LinearDamping, 4.8f, 1e-4f);
    EXPECT_NEAR(cobbleSample.MaxSpeedMultiplier, 1.08f, 1e-4f);
    EXPECT_NEAR(cobbleSample.Flammability, 0.0f, 1e-4f);

    // 4. Pure Grass (Layer 0)
    terrain.FillLayer(0u);
    Engine::SurfaceSample grassSample = terrain.SampleSurfaceAtXZ(8.0f, 8.0f);
    EXPECT_EQ(grassSample.DominantBiome, Engine::BiomeType::BlackwoodThickets);
    EXPECT_NEAR(grassSample.LinearDamping, 5.5f, 1e-4f);
    EXPECT_NEAR(grassSample.MaxSpeedMultiplier, 0.92f, 1e-4f);

    // 5. 50% Chalk (Layer 3) + 50% Ash (Layer 1) -> blended linear damping ~ (1.1 + 4.5) / 2 = 2.8f
    terrain.FillLayer(1u); // Ash Barrens (4.5f)
    terrain.PaintWorldBrush(8.0f, 8.0f, glm::vec3(0.0f), glm::vec3(1.0f), 3u /*Chalk*/, 4.0f, 0.5f, 0.5f);
    Engine::SurfaceSample blendedSample = terrain.SampleSurfaceAtXZ(8.0f, 8.0f);
    EXPECT_GT(blendedSample.LinearDamping, 1.1f);
    EXPECT_LT(blendedSample.LinearDamping, 4.5f);
}

TEST(TerrainSurfaceSamplerTest, Rigidbody2DUseTerrainFrictionAdaptsLinearDamping) {
    auto scene = std::make_shared<Engine::Scene>("FrictionScene");
    auto* terrainGo = scene->CreateGameObject("Terrain");
    auto* terrain = terrainGo->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 64u, 64u);
    terrain->FillLayer(3u); // Chalk Highlands (Damping = 1.1f)

    auto* rbGo = scene->CreateGameObject("Hero");
    rbGo->GetTransform()->position = { 8.0f, 0.0f, 8.0f };
    auto* rb = rbGo->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->UseTerrainFriction = true;
    rb->LinearDamping = 4.5f;

    scene->OnRuntimeStart();
    scene->UpdateRuntime(1.0f / 60.0f);

    // Damping should have dynamically adapted to Chalk (1.1f)
    EXPECT_NEAR(rb->GetLinearDamping(), 1.1f, 0.05f);

    // Paint Tar under the entity (Damping = 9.0f)
    terrain->FillLayer(2u);
    scene->UpdateRuntime(1.0f / 60.0f);
    EXPECT_NEAR(rb->GetLinearDamping(), 9.0f, 0.05f);

    // Launch into the air: airborne entity must not apply ground friction
    rb->LaunchVertical(5.0f);
    terrain->FillLayer(3u); // Change ground to Chalk
    scene->UpdateRuntime(1.0f / 60.0f);
    // Entity is airborne (Y > 0), so it does not update to ground chalk damping
    EXPECT_GT(rbGo->GetTransform()->position.y, 0.01f);
    EXPECT_NEAR(rb->GetLinearDamping(), 9.0f, 0.05f);

    scene->OnRuntimeStop();
}

TEST(WorldBiomeGeneratorTest, BiomeMapMatchesDirectEvaluationAndPOIPruning) {
    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.Seed = 12345ULL;
    cfg.SpawnSceneEntities = false;

    Engine::WorldBiomeGenerator gen(cfg);
    std::vector<uint8_t> splat0, splat1;
    std::vector<Engine::BiomeType> biomeMap;
    std::vector<Engine::PropSpawnDesc> props;
    std::vector<Engine::POISpawnDesc> pois;

    const uint32_t res = 33u;
    const float chunkSize = 16.0f;
    gen.GenerateChunkData(1, 1, chunkSize, res, splat0, splat1, biomeMap, props, pois);

    ASSERT_EQ(biomeMap.size(), static_cast<size_t>(res) * res);

    const float step = chunkSize / static_cast<float>(res - 1u);
    for (uint32_t z = 0; z < res; z += 4) {
        for (uint32_t x = 0; x < res; x += 4) {
            const float wx = 1.0f * chunkSize + static_cast<float>(x) * step;
            const float wz = 1.0f * chunkSize + static_cast<float>(z) * step;
            const Engine::BiomeSample direct = gen.EvaluateBiomeAtXZ(wx, wz, chunkSize);
            EXPECT_EQ(biomeMap[z * res + x], direct.DominantBiome);
        }
    }
}

TEST(TerrainSurfaceSamplingTest, TerrainComponentAndChunkManagerShareSurfaceEvaluation) {
    Engine::Scene scene("SharedSurfaceEvalScene");

    // 1. Finite TerrainComponent filled uniformly with Tar (layer 2)
    auto* finiteGo = scene.CreateGameObject("FiniteTerrain");
    auto* finiteTerrain = finiteGo->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 16u, 16u);
    finiteTerrain->FillLayer(2u);

    // 2. Infinite TerrainChunkManager with chunk (0,0) filled uniformly with Tar (layer 2)
    auto* chunkGo = scene.CreateGameObject("InfiniteTerrain");
    auto* mgr = chunkGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 17u, 1, 2);
    Engine::TerrainChunk& chunk = mgr->EnsureEditableChunk({ 0, 0 });
    const std::array<uint8_t, Engine::TerrainComponent::MaxLayers> pureTar{ 0, 0, 255, 0, 0, 0, 0, 0 };
    for (uint32_t pz = 0; pz < chunk.SplatResolution; ++pz) {
        for (uint32_t px = 0; px < chunk.SplatResolution; ++px) {
            chunk.SetSplatTexel8(px, pz, pureTar);
        }
    }

    const Engine::SurfaceSample finiteSample = finiteTerrain->SampleSurfaceAtXZ(8.0f, 8.0f);
    const Engine::SurfaceSample chunkSample = mgr->SampleSurfaceAtXZ(8.0f, 8.0f);

    EXPECT_EQ(finiteSample.DominantBiome, Engine::BiomeType::TarBogs);
    EXPECT_EQ(chunkSample.DominantBiome, finiteSample.DominantBiome);
    EXPECT_FLOAT_EQ(chunkSample.LinearDamping, finiteSample.LinearDamping);
    EXPECT_FLOAT_EQ(chunkSample.MaxSpeedMultiplier, finiteSample.MaxSpeedMultiplier);
    EXPECT_FLOAT_EQ(chunkSample.Flammability, finiteSample.Flammability);
    EXPECT_FALSE(finiteSample.IsBurning);
    EXPECT_FALSE(chunkSample.IsBurning);
}

} // namespace
