#include <gtest/gtest.h>
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/TerrainChunkManager.h"
#include "Engine/Scene/WorldBiomeGenerator.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Time.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Scene/CameraComponent.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

TEST(TerrainSplatmapTest, InitializationRadialFalloffAndWeightNormalization) {
    Engine::TerrainComponent terrain(16.0f, 16.0f, 64u, 64u);

    // 1. Default initialization fills 100% Layer 0 (Meadow Grass: R=255, G=0, B=0, A=0)
    EXPECT_EQ(terrain.GetSplatTexel(0u, 0u), glm::u8vec4(255, 0, 0, 0));
    EXPECT_EQ(terrain.GetSplatTexel(32u, 32u), glm::u8vec4(255, 0, 0, 0));

    // 2. Paint a soft radial stroke of Layer 1 (Dirt) at world center (8.0, 8.0) with radius 4.0m
    const glm::vec3 origin = { 0.0f, 0.0f, 0.0f };
    const glm::vec3 scale  = { 1.0f, 1.0f, 1.0f };
    terrain.PaintWorldBrush(8.0f, 8.0f, origin, scale, /*layerIndex=*/1u, /*radiusWorld=*/4.0f, /*falloff=*/0.8f, /*strength=*/1.0f);

    const glm::u8vec4 centerTexel  = terrain.GetSplatTexel(32u, 32u); // ~0.18m from center (inside inner plateau)
    const glm::u8vec4 falloffTexel = terrain.GetSplatTexel(42u, 32u); // ~2.6m from center (in smoothstep falloff band)
    const glm::u8vec4 outsideTexel = terrain.GetSplatTexel(58u, 32u); // ~6.6m from center (> 4.0m radius)

    EXPECT_EQ(centerTexel.g, 255u);
    EXPECT_GT(falloffTexel.g, 0u);
    EXPECT_LT(falloffTexel.g, centerTexel.g);
    EXPECT_EQ(outsideTexel, glm::u8vec4(255, 0, 0, 0));

    // 3. Paint overlapping Layer 2 (Cobble) & Layer 3 (Soil) strokes and verify exact R+G+B+A == 255 everywhere
    terrain.PaintWorldSegment({ 4.0f, 4.0f }, { 12.0f, 12.0f }, origin, scale, 2u, 3.0f, 0.65f, 0.75f);
    terrain.PaintWorldBrush(9.5f, 7.0f, origin, scale, 3u, 2.5f, 0.7f, 0.6f);

    for (uint32_t pz = 0; pz < terrain.GetSplatHeight(); ++pz) {
        for (uint32_t px = 0; px < terrain.GetSplatWidth(); ++px) {
            const glm::u8vec4 w = terrain.GetSplatTexel(px, pz);
            const int sum = static_cast<int>(w.r) + static_cast<int>(w.g) + static_cast<int>(w.b) + static_cast<int>(w.a);
            ASSERT_EQ(sum, 255) << "Unnormalized splat texel at (" << px << ", " << pz << ")";
        }
    }
}

TEST(TerrainSplatmapTest, PngSaveAndLoadRoundTripWithoutFlipInversion) {
    Engine::TerrainComponent srcTerrain(14.0f, 14.0f, 32u, 32u);
    // Paint an asymmetric marker in the North-West corner (px=2, pz=3) to catch any vertical/horizontal flip bugs
    srcTerrain.SetSplatTexel(2u, 3u, { 10, 40, 180, 25 });
    const glm::u8vec4 expectedMarker = srcTerrain.GetSplatTexel(2u, 3u);

    const std::filesystem::path tempPng = std::filesystem::temp_directory_path() / "engine_splat_flip_check.png";
    ASSERT_TRUE(srcTerrain.SaveSplatmapToPNG(tempPng.string()));
    EXPECT_FALSE(srcTerrain.HasUnsavedMemoryEdits());

    Engine::TerrainComponent loadedTerrain(14.0f, 14.0f, 8u, 8u);
    ASSERT_TRUE(loadedTerrain.LoadSplatmapFromPNG(tempPng.string()));
    EXPECT_EQ(loadedTerrain.GetSplatWidth(), 32u);
    EXPECT_EQ(loadedTerrain.GetSplatHeight(), 32u);
    EXPECT_EQ(loadedTerrain.GetSplatTexel(2u, 3u), expectedMarker);
    EXPECT_EQ(loadedTerrain.GetSplatTexel(2u, 28u), glm::u8vec4(255, 0, 0, 0));

    std::error_code ec;
    std::filesystem::remove(tempPng, ec);
}

TEST(TerrainComponentTest, DimensionsAndParameters) {
    Engine::TerrainComponent terrain(20.0f, 30.0f, 16u, 24u);
    EXPECT_FLOAT_EQ(terrain.GetWidth(), 20.0f);
    EXPECT_FLOAT_EQ(terrain.GetDepth(), 30.0f);
    EXPECT_EQ(terrain.GetSplatWidth(), 16u);
    EXPECT_EQ(terrain.GetSplatHeight(), 24u);

    terrain.SetBlendSharpness(0.85f);
    EXPECT_FLOAT_EQ(terrain.GetBlendSharpness(), 0.85f);

    terrain.SetEdgeDarkening(0.35f);
    EXPECT_FLOAT_EQ(terrain.GetEdgeDarkening(), 0.35f);

    terrain.SetLayerTilingScale(1u, 2.5f);
    EXPECT_FLOAT_EQ(terrain.GetLayer(1u).TilingScale, 2.5f);
}

TEST(TerrainChunkManagerTest, WorldToChunkAndActiveWindowCounts) {
    Engine::Scene scene("ChunkWindowTest");
    auto* go = scene.CreateGameObject("InfiniteTerrain");
    auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);

    EXPECT_EQ(mgr->WorldToChunk(0.0f, 0.0f), (Engine::ChunkCoord{ 0, 0 }));
    EXPECT_EQ(mgr->WorldToChunk(15.99f, 15.99f), (Engine::ChunkCoord{ 0, 0 }));
    EXPECT_EQ(mgr->WorldToChunk(16.0f, 16.0f), (Engine::ChunkCoord{ 1, 1 }));
    EXPECT_EQ(mgr->WorldToChunk(-0.01f, -0.01f), (Engine::ChunkCoord{ -1, -1 }));
    EXPECT_EQ(mgr->WorldToChunk(-40.0f, 80.0f), (Engine::ChunkCoord{ -3, 5 }));

    mgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    mgr->FlushPendingGenerations();
    EXPECT_EQ(mgr->GetActiveChunkCount(), 49u);

    const auto* centerChunk = mgr->GetChunk({ 0, 0 });
    ASSERT_NE(centerChunk, nullptr);
    EXPECT_EQ(centerChunk->SplatResolution, 65u);
    EXPECT_FLOAT_EQ(centerChunk->ChunkSizeMeters, 16.0f);
    EXPECT_TRUE(centerChunk->IsResident());
    EXPECT_EQ(centerChunk->GetSplatTexel(0u, 0u), glm::u8vec4(255, 0, 0, 0));
}

TEST(TerrainChunkManagerTest, StreamWindowCachingAndLRUEvictionAcrossWorldTravel) {
    const bool jobWasInitialized = Engine::JobSystem::IsInitialized();
    if (!jobWasInitialized) {
        Engine::JobSystem::Init(2);
    }

    {
        Engine::Scene scene("ChunkTravelTest");
        auto* go = scene.CreateGameObject("InfiniteTerrain");
        auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);

        // Stream starting at (-40m, -40m) -> chunk (-3, -3)
        mgr->UpdateStreaming({ -40.0f, 0.0f, -40.0f });
        mgr->FlushPendingGenerations();
        EXPECT_EQ(mgr->GetActiveChunkCount(), 49u);

        // Paint a recognizable marker in chunk (-3, -3)
        auto* startChunk = mgr->GetChunk({ -3, -3 });
        ASSERT_NE(startChunk, nullptr);
        startChunk->SetSplatTexel(10u, 12u, { 15, 210, 20, 10 });
        const glm::u8vec4 paintedMarker = startChunk->GetSplatTexel(10u, 12u);

        // Move smoothly from (-40, -40) to (+80, +80) across multiple frames
        for (int step = 1; step <= 12; ++step) {
            const float t = static_cast<float>(step) / 12.0f;
            const float wx = -40.0f + t * 120.0f;
            const float wz = -40.0f + t * 120.0f;
            mgr->UpdateStreaming({ wx, 0.0f, wz });
            mgr->FlushPendingGenerations();
            EXPECT_EQ(mgr->GetActiveChunkCount(), 49u);
        }

        // At (+80, +80) -> chunk (5, 5), Chebyshev distance to (-3, -3) is 8 > CacheRetentionRadius (6): LRU evicted
        EXPECT_EQ(mgr->GetChunk({ -3, -3 }), nullptr);
        EXPECT_NE(mgr->GetChunk({ 5, 5 }), nullptr);

        // Check CacheRetentionRadius retention: step to (-16m, -16m) from (0,0) and verify edit preserved in RAM cache
        mgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
        mgr->FlushPendingGenerations();
        auto* cachedCandidate = mgr->GetChunk({ 0, 0 });
        ASSERT_NE(cachedCandidate, nullptr);
        cachedCandidate->SetSplatTexel(20u, 22u, { 0, 255, 0, 0 });

        // Move 5 chunks east to (80, 0) -> dist 5 is inside CacheRetentionRadius (6), outside ActiveRadius (3)
        mgr->UpdateStreaming({ 80.0f, 0.0f, 0.0f });
        mgr->FlushPendingGenerations();
        auto* cachedInRam = mgr->GetChunk({ 0, 0 });
        ASSERT_NE(cachedInRam, nullptr);
        EXPECT_EQ(cachedInRam->State, Engine::ChunkLoadState::CachedInRam);
        EXPECT_FALSE(cachedInRam->HasGpuTexture());
        EXPECT_EQ(cachedInRam->GetSplatTexel(20u, 22u), glm::u8vec4(0, 255, 0, 0));

        // Return to (0, 0) -> promoted back cleanly with edited weights intact
        mgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
        mgr->FlushPendingGenerations();
        auto* restored = mgr->GetChunk({ 0, 0 });
        ASSERT_NE(restored, nullptr);
        EXPECT_TRUE(restored->IsResident());
        EXPECT_EQ(restored->GetSplatTexel(20u, 22u), glm::u8vec4(0, 255, 0, 0));
    }

    if (!jobWasInitialized) {
        Engine::JobSystem::Shutdown();
    }
}

TEST(TerrainChunkManagerTest, FourChunkCornerBrushStitchingBitExactContinuity) {
    Engine::Scene scene("CornerStitchTest");
    auto* go = scene.CreateGameObject("InfiniteTerrain");
    auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);

    mgr->UpdateStreaming({ 16.0f, 0.0f, 16.0f });
    mgr->FlushPendingGenerations();

    // Paint a circular brush and overlapping segment directly over the 4-chunk corner (16.0, 16.0)
    mgr->PaintWorldBrush(16.0f, 16.0f, /*layerIndex=*/1u, /*radiusWorld=*/3.5f, /*falloff=*/0.7f, /*strength=*/0.85f);
    mgr->PaintWorldSegment({ 14.5f, 16.0f }, { 17.5f, 16.0f }, /*layerIndex=*/2u, /*radiusWorld=*/2.2f, /*falloff=*/0.6f, /*strength=*/0.75f);

    const auto* c00 = mgr->GetChunk({ 0, 0 });
    const auto* c10 = mgr->GetChunk({ 1, 0 });
    const auto* c01 = mgr->GetChunk({ 0, 1 });
    const auto* c11 = mgr->GetChunk({ 1, 1 });
    ASSERT_NE(c00, nullptr);
    ASSERT_NE(c10, nullptr);
    ASSERT_NE(c01, nullptr);
    ASSERT_NE(c11, nullptr);

    // Corner texel (64,64), (0,64), (64,0), (0,0) must be bit-exact identical and non-default
    const glm::u8vec4 w00 = c00->GetSplatTexel(64u, 64u);
    const glm::u8vec4 w10 = c10->GetSplatTexel(0u, 64u);
    const glm::u8vec4 w01 = c01->GetSplatTexel(64u, 0u);
    const glm::u8vec4 w11 = c11->GetSplatTexel(0u, 0u);
    EXPECT_EQ(w00, w10);
    EXPECT_EQ(w00, w01);
    EXPECT_EQ(w00, w11);
    EXPECT_GT(static_cast<int>(w00.g) + static_cast<int>(w00.b), 180);

    // Every texel along shared East-West and North-South edges must match bit-for-bit and sum to 255
    for (uint32_t i = 0; i < 65u; ++i) {
        const glm::u8vec4 westSeamA = c00->GetSplatTexel(64u, i);
        const glm::u8vec4 westSeamB = c10->GetSplatTexel(0u, i);
        EXPECT_EQ(westSeamA, westSeamB) << "X-seam mismatch at pz=" << i;
        EXPECT_EQ(static_cast<int>(westSeamA.r) + westSeamA.g + westSeamA.b + westSeamA.a, 255);

        const glm::u8vec4 northSeamA = c00->GetSplatTexel(i, 64u);
        const glm::u8vec4 northSeamB = c01->GetSplatTexel(i, 0u);
        EXPECT_EQ(northSeamA, northSeamB) << "Z-seam mismatch at px=" << i;
    }

    // Newly streamed neighbor (with previously painted border on existing chunk) inherits shared edge
    mgr->UpdateStreaming({ 64.0f, 0.0f, 64.0f });
    mgr->FlushPendingGenerations(); // Evicts (0,0) beyond CacheRetentionRadius if >6? (0,0) dist from (4,4) is 4 <= 6 (CachedInRam)
    mgr->UpdateStreaming({ 16.0f, 0.0f, 16.0f });
    mgr->FlushPendingGenerations();
    EXPECT_EQ(mgr->GetChunk({ 0, 0 })->GetSplatTexel(64u, 64u), w00);
}

TEST(TerrainChunkManagerTest, UploadRingThrottlesToTwoChunksPerFrameAndDestructorIsSafe) {
    const bool jobWasInitialized = Engine::JobSystem::IsInitialized();
    if (!jobWasInitialized) {
        Engine::JobSystem::Init(4);
    }

    {
        Engine::Scene scene("UploadRingTest");
        auto* go = scene.CreateGameObject("InfiniteTerrain");
        auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);
        mgr->MaxGpuUploadsPerFrame = 2;

        mgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
        mgr->FlushPendingGenerations();

        // UpdateStreaming drained 2 on initial commit or FlushPendingGenerations queued remaining
        EXPECT_LE(mgr->GetLastFrameUploadCount(), 2u);
        size_t guardFrames = 0;
        while (mgr->GetPendingUploadCount() > 0 && guardFrames < 40) {
            const uint32_t processed = mgr->ProcessGpuUploadRing();
            EXPECT_LE(processed, 2u);
            ++guardFrames;
        }
        EXPECT_EQ(mgr->GetPendingUploadCount(), 0u);
    }

    // Immediate teardown with unawaited async jobs must join cleanly without crash
    {
        Engine::Scene destroyScene("ImmediateTeardownScene");
        auto* go = destroyScene.CreateGameObject("TornDownTerrain");
        auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);
        mgr->UpdateStreaming({ 512.0f, 0.0f, 512.0f });
    }

    if (!jobWasInitialized) {
        Engine::JobSystem::Shutdown();
    }
    SUCCEED();
}

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

TEST(TerrainArraySplatTest, EightLayerNormalizationAndCrossChunkCobblestoneStitching) {
    // 1. Direct NormalizeWeights8 exact invariant tests across arbitrary distributions
    const std::array<std::array<float, 8>, 4> samples = {{
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.72f, 0.28f, 0.0f, 0.0f },
        { 0.13f, 0.07f, 0.19f, 0.03f, 0.31f, 0.11f, 0.09f, 0.07f },
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
    }};
    for (uint32_t sIdx = 0; sIdx < samples.size(); ++sIdx) {
        const auto norm = Engine::TerrainComponent::NormalizeWeights8(samples[sIdx], sIdx % 8u);
        int sum = 0;
        for (uint8_t v : norm) {
            sum += static_cast<int>(v);
        }
        EXPECT_EQ(sum, 255);
    }

    // 2. Finite TerrainComponent: Paint overlapping layers 0..7 including 5th Cobblestone (4) & Scorched (5)
    Engine::TerrainComponent terrain(16.0f, 16.0f, 32u, 32u);
    const glm::vec3 origin{ 0.0f, 0.0f, 0.0f };
    const glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    terrain.PaintWorldBrush(4.0f, 4.0f, origin, scale, /*layerIndex=*/2u, 3.0f, 0.7f, 0.9f);
    terrain.PaintWorldBrush(8.0f, 8.0f, origin, scale, /*layerIndex=*/4u, 3.5f, 0.6f, 1.0f);
    terrain.PaintWorldBrush(11.0f, 11.0f, origin, scale, /*layerIndex=*/5u, 2.5f, 0.6f, 0.85f);
    terrain.PaintWorldBrush(13.0f, 3.0f, origin, scale, /*layerIndex=*/7u, 2.0f, 0.6f, 0.75f);

    const auto cobbleCenter = terrain.GetSplatTexel8(16u, 16u);
    EXPECT_GT(cobbleCenter[4], 200u);

    for (uint32_t pz = 0; pz < terrain.GetSplatHeight(); ++pz) {
        for (uint32_t px = 0; px < terrain.GetSplatWidth(); ++px) {
            const auto w8 = terrain.GetSplatTexel8(px, pz);
            int sum = 0;
            for (uint8_t v : w8) {
                sum += static_cast<int>(v);
            }
            ASSERT_EQ(sum, 255) << "Unnormalized 8-layer texel at (" << px << ", " << pz << ")";
        }
    }

    // 3. Infinite TerrainChunkManager: painting Layer 4 (Cobblestone) across shared corner (16, 16)
    Engine::Scene scene("Chunk8LayerPaint");
    auto* go = scene.CreateGameObject("ChunkTerrain");
    auto* mgr = go->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 1, 2);
    mgr->UpdateStreaming({ 8.0f, 0.0f, 8.0f });
    mgr->FlushPendingGenerations();

    mgr->PaintWorldBrush(16.0f, 16.0f, /*layerIndex=*/4u, /*radiusWorld=*/3.0f, /*falloff=*/0.65f, /*strength=*/1.0f);

    const auto* c00 = mgr->GetChunk({ 0, 0 });
    const auto* c10 = mgr->GetChunk({ 1, 0 });
    const auto* c01 = mgr->GetChunk({ 0, 1 });
    const auto* c11 = mgr->GetChunk({ 1, 1 });
    ASSERT_NE(c00, nullptr);
    ASSERT_NE(c10, nullptr);
    ASSERT_NE(c01, nullptr);
    ASSERT_NE(c11, nullptr);

    EXPECT_GT(c00->GetSplatTexel1(64u, 64u).r, 200u);
    for (uint32_t i = 0; i < 65u; ++i) {
        EXPECT_EQ(c00->GetSplatTexel1(64u, i), c10->GetSplatTexel1(0u, i));
        EXPECT_EQ(c00->GetSplatTexel1(i, 64u), c01->GetSplatTexel1(i, 0u));
    }
    EXPECT_EQ(c00->GetSplatTexel8(64u, 64u), c11->GetSplatTexel8(0u, 0u));
}

TEST(TerrainArraySplatTest, CpuChannelPackersCompanionResolutionAndHeadlessArray) {
    // 1. RGB (2x2) + Height (4x4 resampled to 4x4) -> RGBA8
    const uint8_t rgbDiffuse[2 * 2 * 3] = {
        10,  20,  30,   40,  50,  60,
        70,  80,  90,  100, 110, 120
    };
    const uint8_t heightMap[4 * 4] = {
        12,  12,  84,  84,
        12,  12,  84,  84,
        150, 150, 240, 240,
        150, 150, 240, 240
    };

    std::vector<uint8_t> packedAH;
    ASSERT_TRUE(Engine::Texture2DArray::PackAlbedoHeightRGBA8(
        rgbDiffuse, 3, 2u, 2u,
        heightMap, 1, 4u, 4u,
        4u, 4u,
        packedAH
    ));
    ASSERT_EQ(packedAH.size(), 4u * 4u * 4u);
    // Corners match exact RGB diffuse and packed height in Alpha
    EXPECT_EQ(packedAH[0], 10u);
    EXPECT_EQ(packedAH[1], 20u);
    EXPECT_EQ(packedAH[2], 30u);
    EXPECT_EQ(packedAH[3], 12u);
    const size_t lastIdx = (4u * 4u - 1u) * 4u;
    EXPECT_EQ(packedAH[lastIdx + 0], 100u);
    EXPECT_EQ(packedAH[lastIdx + 1], 110u);
    EXPECT_EQ(packedAH[lastIdx + 2], 120u);
    EXPECT_EQ(packedAH[lastIdx + 3], 240u);

    // Without height map, fallbackHeight < 0 synthesizes luminance into A
    std::vector<uint8_t> packedAutoLum;
    ASSERT_TRUE(Engine::Texture2DArray::PackAlbedoHeightRGBA8(
        rgbDiffuse, 3, 2u, 2u,
        nullptr, 0, 0u, 0u,
        2u, 2u,
        packedAutoLum
    ));
    ASSERT_EQ(packedAutoLum.size(), 2u * 2u * 4u);
    const uint8_t expectedLum0 = static_cast<uint8_t>(std::lround(0.299f * 10.0f + 0.587f * 20.0f + 0.114f * 30.0f));
    EXPECT_EQ(packedAutoLum[3], expectedLum0);

    // 2. Normal + Glossiness packer (null normal -> flat (128,128,255), Tar gloss=0.85 -> 217)
    std::vector<uint8_t> packedTarNR;
    ASSERT_TRUE(Engine::Texture2DArray::PackNormalGlossRGBA8(
        nullptr, 0, 0u, 0u,
        /*defaultGlossiness=*/0.85f,
        nullptr, 0, 0u, 0u,
        4u, 4u,
        packedTarNR
    ));
    ASSERT_EQ(packedTarNR.size(), 4u * 4u * 4u);
    EXPECT_EQ(packedTarNR[0], 128u);
    EXPECT_EQ(packedTarNR[1], 128u);
    EXPECT_EQ(packedTarNR[2], 255u);
    EXPECT_EQ(packedTarNR[3], 217u);

    // Custom non-unit normal normalizes cleanly & preserves matte glossiness (Ash = 0.08 -> 20)
    const uint8_t tiltedNormal[3] = { 200u, 128u, 200u };
    std::vector<uint8_t> packedAshNR;
    ASSERT_TRUE(Engine::Texture2DArray::PackNormalGlossRGBA8(
        tiltedNormal, 3, 1u, 1u,
        /*defaultGlossiness=*/0.08f,
        nullptr, 0, 0u, 0u,
        2u, 2u,
        packedAshNR
    ));
    ASSERT_EQ(packedAshNR.size(), 2u * 2u * 4u);
    EXPECT_GT(packedAshNR[0], 128u);
    EXPECT_EQ(packedAshNR[3], 20u);

    // 3. Companion _d / _h / _n path resolution and end-to-end PNG file packing
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "ter03_companion_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path diffPath = tempDir / "tex_cobble_d.png";
    const std::filesystem::path heightPath = tempDir / "tex_cobble_h.png";
    const std::filesystem::path normPath = tempDir / "tex_cobble_n.png";

    Engine::TerrainComponent pngGen(4.0f, 4.0f, 8u, 8u);
    ASSERT_TRUE(pngGen.SaveSplatmapToPNG(diffPath.string()));
    ASSERT_TRUE(pngGen.SaveSplatmapToPNG(heightPath.string()));
    ASSERT_TRUE(pngGen.SaveSplatmapToPNG(normPath.string()));

    const auto resolved = Engine::Texture2DArray::ResolveTerrainMapCompanions(diffPath.string());
    EXPECT_EQ(std::filesystem::path(resolved.DiffusePath).filename().string(), "tex_cobble_d.png");
    EXPECT_EQ(std::filesystem::path(resolved.HeightPath).filename().string(), "tex_cobble_h.png");
    EXPECT_EQ(std::filesystem::path(resolved.NormalPath).filename().string(), "tex_cobble_n.png");

    std::vector<uint8_t> fileSliceAH;
    std::vector<uint8_t> fileSliceNR;
    ASSERT_TRUE(Engine::Texture2DArray::BuildAlbedoHeightSliceFromFiles(
        resolved.DiffusePath, resolved.HeightPath, 16u, 16u, { 100, 100, 100, 255 }, fileSliceAH));
    ASSERT_TRUE(Engine::Texture2DArray::BuildNormalGlossSliceFromFiles(
        resolved.NormalPath, 0.35f, 16u, 16u, fileSliceNR));
    EXPECT_EQ(fileSliceAH.size(), 16u * 16u * 4u);
    EXPECT_EQ(fileSliceNR.size(), 16u * 16u * 4u);

    // 4. Headless Texture2DArray construction & upload safety (no GL context)
    Engine::Texture2DArray headlessArray(16u, 16u, 8u, true, true);
    EXPECT_EQ(headlessArray.GetWidth(), 16u);
    EXPECT_EQ(headlessArray.GetHeight(), 16u);
    EXPECT_EQ(headlessArray.GetLayers(), 8u);
    headlessArray.SetLayerData(4u, fileSliceAH.data());
    EXPECT_TRUE(headlessArray.HasUploadedLayer(4u));

    std::filesystem::remove_all(tempDir, ec);
}

TEST(TerrainArraySplatTest, BiomeSampleMapsSanctuaryCobbleToFifthLayerChannel) {
    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = false;
    cfg.Seed = 42ULL;

    Engine::WorldBiomeGenerator gen(cfg);
    const Engine::BiomeSample originSample = gen.EvaluateBiomeAtXZ(0.0f, 0.0f, 16.0f);
    EXPECT_EQ(originSample.DominantBiome, Engine::BiomeType::SanctuaryCobble);

    const std::array<uint8_t, 8> w8 = originSample.ToSplat8();
    int sum = 0;
    for (uint8_t v : w8) {
        sum += static_cast<int>(v);
    }
    EXPECT_EQ(sum, 255);
    EXPECT_GT(w8[4], 160u); // SanctuaryCobble lands natively in Layer 4 (Cobblestone)!
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

TEST(TerrainFireGridTest, PropagatesAlongTarStripAcrossChunksAndScorches) {
    auto scene = std::make_shared<Engine::Scene>("FireScene");
    auto* tcmGo = scene->CreateGameObject("ChunkManager");
    auto* mgr = tcmGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 1, 2);

    // Stream chunks (0,0) [0..16] and (1,0) [16..32]
    mgr->UpdateStreaming({ 8.0f, 0.0f, 8.0f });
    mgr->FlushPendingGenerations();

    auto* c00 = mgr->GetChunk({ 0, 0 });
    auto* c10 = mgr->GetChunk({ 1, 0 });
    ASSERT_NE(c00, nullptr);
    ASSERT_NE(c10, nullptr);

    // Initialize both chunks with Ash Barrens (Layer 1, non-flammable)
    for (size_t i = 0; i < c00->SplatData.size(); i += 4u) {
        c00->SplatData[i + 0] = 0u;
        c00->SplatData[i + 1] = 255u;
        c00->SplatData[i + 2] = 0u;
        c00->SplatData[i + 3] = 0u;
    }
    for (size_t i = 0; i < c10->SplatData.size(); i += 4u) {
        c10->SplatData[i + 0] = 0u;
        c10->SplatData[i + 1] = 255u;
        c10->SplatData[i + 2] = 0u;
        c10->SplatData[i + 3] = 0u;
    }

    // Paint a 10m line of Tar (Layer 2) from (11, 8) to (21, 8) across the x=16.0 chunk boundary
    mgr->PaintWorldSegment({ 11.0f, 8.0f }, { 21.0f, 8.0f }, /*layerIndex=*/2u, /*radiusWorld=*/1.2f, /*falloff=*/0.4f, /*strength=*/1.0f);

    // Assert flammability along the tar strip is high, and off-strip is 0
    EXPECT_GT(mgr->SampleSurfaceAtXZ(11.0f, 8.0f).Flammability, 0.35f);
    EXPECT_GT(mgr->SampleSurfaceAtXZ(16.0f, 8.0f).Flammability, 0.35f);
    EXPECT_GT(mgr->SampleSurfaceAtXZ(21.0f, 8.0f).Flammability, 0.35f);
    EXPECT_LT(mgr->SampleSurfaceAtXZ(14.0f, 5.0f).Flammability, 0.05f);

    // Ignite at (11, 8)
    mgr->IgniteAtXZ({ 11.0f, 8.0f }, 0.6f);
    EXPECT_TRUE(mgr->IsBurningAtXZ(11.0f, 8.0f));
    EXPECT_FALSE(mgr->IsBurningAtXZ(16.0f, 8.0f)); // Not yet reached

    // Step 2.0s (20 ticks of 0.10s)
    for (int i = 0; i < 20; ++i) {
        mgr->UpdateFire(0.10f);
    }

    // Fire should have crossed the chunk boundary at x = 16.0m!
    EXPECT_TRUE(mgr->IsBurningAtXZ(16.0f, 8.0f));
    // Off-strip cells must not have caught fire
    EXPECT_FALSE(mgr->IsBurningAtXZ(14.0f, 5.0f));

    // Step an additional 3.0s (total elapsed 5.0s > 4.5s max burn duration)
    for (int i = 0; i < 30; ++i) {
        mgr->UpdateFire(0.10f);
    }

    // Origin cell at (11, 8) should have burned out, decayed to Scorched Ash (Layer 5)
    EXPECT_FALSE(mgr->IsBurningAtXZ(11.0f, 8.0f));
    const auto scorchedSample = mgr->SampleSurfaceAtXZ(11.0f, 8.0f);
    // Flammability has dropped below ignition threshold due to Scorched Ash replacement
    EXPECT_LT(scorchedSample.Flammability, 0.35f);
}

TEST(TerrainChunkManagerTest, SceneRenderStreamsUncachedChunksWithPooledPropsWithoutReallocationUB) {
    const bool jobWasInitialized = Engine::JobSystem::IsInitialized();
    if (jobWasInitialized) {
        // Ensure UpdateStreaming runs inline on the render call itself to stress immediate push_back on m_GameObjects
        Engine::JobSystem::Shutdown();
    }

    Engine::Time::Reset();
    Engine::Time::SetMockDeltaTime(0.016f);
    Engine::Time::Update();

    Engine::Scene scene("RenderStreamSafetyScene");

    auto* camGo = scene.CreateGameObject("MainCamera");
    camGo->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* camComp = camGo->AddComponent<Engine::CameraComponent>();
    camComp->SmoothFollow = false;
    camComp->TraumaDecay = 1.4f;
    camComp->ResetToTarget({ 0.0f, 0.0f, 0.0f });

    auto* terrainGo = scene.CreateGameObject("InfiniteTerrain");
    auto* mgr = terrainGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 33u, 1, 3);
    mgr->AutoStreamWithCamera = true;

    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = true;
    cfg.Seed = 42ULL;
    mgr->ConfigureProceduralWorld(cfg);

    // Traverse across multiple uncached chunk regions while calling GatherRenderContext / Render
    const std::array<glm::vec3, 4> waypoints = {
        glm::vec3{   0.0f, 0.0f,   0.0f },
        glm::vec3{  48.0f, 0.0f,   0.0f },
        glm::vec3{  96.0f, 0.0f,  48.0f },
        glm::vec3{ 144.0f, 0.0f, -32.0f }
    };

    const size_t initialEntityCount = scene.GetGameObjects().size();
    for (const glm::vec3& wp : waypoints) {
        Engine::Time::Update();
        camGo->GetTransform()->position = wp;
        camComp->AddTrauma(0.5f);

        // Two render context gathers in the same frame (multi-pass safe + no vector reallocation UB)
        const auto ctx1 = scene.GetRenderer().GatherRenderContext(scene);
        const auto ctx2 = scene.GetRenderer().GatherRenderContext(scene);
        EXPECT_EQ(ctx1.activeCam, &camComp->Camera);
        EXPECT_EQ(ctx2.activeCam, &camComp->Camera);
        EXPECT_FALSE(scene.IsIteratingEntities());
        EXPECT_NEAR(camComp->GetTrauma(), 0.5f - 1.4f * 0.016f, 1e-5f);
        camComp->ResetToTarget(wp);
    }

    EXPECT_GT(scene.GetGameObjects().size(), initialEntityCount);
    EXPECT_GE(mgr->GetActiveChunkCount(), 9u);

    Engine::Time::Reset();
    if (jobWasInitialized) {
        Engine::JobSystem::Init(4);
    }
}

} // namespace

