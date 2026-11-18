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

TEST(TerrainChunkManagerTest, EvictionExtinguishesOnlyEvictedChunkLeavingNeighborFireIntact) {
    Engine::TerrainChunkManager mgr(16.0f, 33u, 1, 1);

    auto& c0 = mgr.EnsureEditableChunk({ 0, 0 });
    auto& c1 = mgr.EnsureEditableChunk({ 1, 0 });

    // Initialize both chunks with Ash Barrens (non-flammable Layer 1)
    for (size_t i = 0; i < c0.SplatData.size(); i += 4u) {
        c0.SplatData[i + 0] = 0u;
        c0.SplatData[i + 1] = 255u;
        c0.SplatData[i + 2] = 0u;
        c0.SplatData[i + 3] = 0u;
    }
    for (size_t i = 0; i < c1.SplatData.size(); i += 4u) {
        c1.SplatData[i + 0] = 0u;
        c1.SplatData[i + 1] = 255u;
        c1.SplatData[i + 2] = 0u;
        c1.SplatData[i + 3] = 0u;
    }

    // Paint Tar (Layer 2) across border from x=13 to x=19 along z=8
    mgr.PaintWorldSegment({ 13.0f, 8.0f }, { 19.0f, 8.0f }, 2u, 1.2f, 0.4f, 1.0f);
    EXPECT_GT(mgr.SampleSurfaceAtXZ(14.0f, 8.0f).Flammability, 0.35f);
    EXPECT_GT(mgr.SampleSurfaceAtXZ(18.0f, 8.0f).Flammability, 0.35f);

    // Ignite fire in chunk (0, 0) at x=14 and in chunk (1, 0) at x=18
    mgr.IgniteAtXZ({ 14.0f, 8.0f }, 0.6f);
    mgr.IgniteAtXZ({ 18.0f, 8.0f }, 0.6f);
    EXPECT_TRUE(mgr.IsBurningAtXZ(14.0f, 8.0f));
    EXPECT_TRUE(mgr.IsBurningAtXZ(18.0f, 8.0f));

    // Stream viewer to x=40.0m (chunk cx=2). With retention radius 1:
    // Chunk (0, 0) has distance |0 - 2| = 2 > 1 -> evicted!
    // Chunk (1, 0) has distance |1 - 2| = 1 <= 1 -> retained!
    mgr.UpdateStreaming(glm::vec3(40.0f, 0.0f, 8.0f));

    EXPECT_EQ(mgr.GetChunk({ 0, 0 }), nullptr);
    EXPECT_NE(mgr.GetChunk({ 1, 0 }), nullptr);

    // Fire in evicted chunk (0, 0) must be extinguished
    EXPECT_FALSE(mgr.IsBurningAtXZ(14.0f, 8.0f));
    // Fire in neighboring resident chunk (1, 0) MUST REMAIN INTACT!
    EXPECT_TRUE(mgr.IsBurningAtXZ(18.0f, 8.0f));
}

TEST(TerrainChunkManagerTest, UserModifiedChunkIsNotOverwrittenByAsyncPayloadDrain) {
    const bool jobWasInit = Engine::JobSystem::IsInitialized();
    if (!jobWasInit) {
        Engine::JobSystem::Init(2);
    }

    Engine::TerrainChunkManager mgr(16.0f, 33u, 1, 2);

    // Dispatch background generation for chunk (3, 3)
    mgr.DispatchChunkGeneration({ 3, 3 });
    auto* initialChunk = mgr.GetChunk({ 3, 3 });
    ASSERT_NE(initialChunk, nullptr);
    EXPECT_EQ(initialChunk->State, Engine::ChunkLoadState::Generating);

    // User modifies chunk while background generation is in-flight
    auto& editable = mgr.EnsureEditableChunk({ 3, 3 });
    editable.UserModified = true;
    for (size_t i = 0; i < editable.SplatData.size(); i += 4u) {
        editable.SplatData[i + 0] = 0u;
        editable.SplatData[i + 1] = 0u;
        editable.SplatData[i + 2] = 0u;
        editable.SplatData[i + 3] = 255u; // Distinct custom layer 3
    }

    // Wait for in-flight async jobs to complete and drain payloads: must NOT overwrite editable.UserModified chunk!
    mgr.FlushPendingGenerations();

    auto* verifiedChunk = mgr.GetChunk({ 3, 3 });
    ASSERT_NE(verifiedChunk, nullptr);
    EXPECT_TRUE(verifiedChunk->UserModified);
    EXPECT_EQ(verifiedChunk->SplatData[3], 255u);
    EXPECT_EQ(verifiedChunk->SplatData[0], 0u);

    if (!jobWasInit) {
        Engine::JobSystem::Shutdown();
    }
}

TEST(TerrainChunkManagerTest, OnDestroyReleasesAllTarFirewallEntities) {
    Engine::Scene scene("FirewallCleanupScene");

    auto* terrainGo = scene.CreateGameObject("Terrain");
    auto* mgr = terrainGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 33u, 1, 2);

    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = true;
    mgr->ConfigureProceduralWorld(cfg);

    mgr->EnsureEditableChunk({ 0, 0 });
    // Paint tar at (8.0, 8.0)
    mgr->PaintMaterialWorld({ 8.0f, 8.0f }, 2u, 3.0f, 1.0f);
    mgr->IgniteAtXZ({ 8.0f, 8.0f }, 1.5f);

    EXPECT_GT(mgr->GetFireGrid().GetActiveFireCount(), 0u);

    size_t spawnedFirewalls = 0;
    for (const auto& go : scene.GetGameObjects()) {
        if (go && go->GetName() == "TarFirewall" && go->IsActive()) {
            ++spawnedFirewalls;
        }
    }
    EXPECT_GT(spawnedFirewalls, 0u);

    // Destroy the TerrainChunkManager component: all firewalls returned to pool
    mgr->OnDestroy();

    size_t remainingActiveFirewalls = 0;
    for (const auto& go : scene.GetGameObjects()) {
        if (go && go->GetName() == "TarFirewall" && go->IsActive()) {
            ++remainingActiveFirewalls;
        }
    }
    EXPECT_EQ(remainingActiveFirewalls, 0u);
    EXPECT_EQ(scene.GetPoolManager().GetPoolFreeCount("pool_tar_firewall"), spawnedFirewalls);
    EXPECT_EQ(mgr->GetFireGrid().GetActiveFireCount(), 0u);
}

TEST(SceneRendererOptimizationTest, FoliageGridReusesCapacityAndNoHeapThrashing) {
    Engine::Scene scene("FoliageOptScene");
    auto* windObj = scene.CreateGameObject("Wind");
    auto* wind = windObj->AddComponent<Engine::WindComponent>();
    wind->InteractiveRustle = true;

    for (int i = 0; i < 3; ++i) {
        auto* fObj = scene.CreateGameObject("Foliage" + std::to_string(i));
        fObj->GetTransform()->position = { static_cast<float>(i) * 2.0f, 0.0f, 0.0f };
        auto* spr = fObj->AddComponent<Engine::SpriteRenderer>();
        spr->SetWindSwayWeight(0.5f);
    }

    Engine::SceneRenderer renderer;
    renderer.GatherRenderContext(scene);
    const size_t bucketCount1 = renderer.GetFoliageInteractorGrid().bucket_count();

    // Gather again
    renderer.GatherRenderContext(scene);
    const size_t bucketCount2 = renderer.GetFoliageInteractorGrid().bucket_count();
    // Scratch map retains capacity across frames
    EXPECT_GE(bucketCount2, bucketCount1);
}

TEST(TerrainChunkManagerTest, TarFirewallEntityPooling) {
    Engine::Scene scene("FirewallPoolTest");
    auto* terrainGo = scene.CreateGameObject("Terrain");
    auto* mgr = terrainGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 33u, 1, 2);

    Engine::WorldGenConfig cfg;
    cfg.Enabled = true;
    cfg.SpawnSceneEntities = true;
    mgr->ConfigureProceduralWorld(cfg);

    mgr->EnsureEditableChunk({ 0, 0 });
    mgr->PaintMaterialWorld({ 8.0f, 8.0f }, 2u, 3.0f, 1.0f);
    mgr->IgniteAtXZ({ 8.0f, 8.0f }, 1.5f);

    EXPECT_GT(mgr->GetFireGrid().GetActiveFireCount(), 0u);

    // Verify TarFirewall entities are acquired and tracked in EntityPoolManager
    const size_t totalSpawned = scene.GetPoolManager().GetPoolTotalCount("pool_tar_firewall");
    EXPECT_GT(totalSpawned, 0u);
    EXPECT_EQ(scene.GetPoolManager().GetPoolFreeCount("pool_tar_firewall"), 0u);

    // Extinguish: all firewalls returned to pool free list
    mgr->ExtinguishAtXZ({ 8.0f, 8.0f }, 5.0f);
    EXPECT_EQ(mgr->GetFireGrid().GetActiveFireCount(), 0u);
    EXPECT_EQ(scene.GetPoolManager().GetPoolFreeCount("pool_tar_firewall"), totalSpawned);
    EXPECT_EQ(scene.GetPoolManager().GetPoolTotalCount("pool_tar_firewall"), totalSpawned);

    // Re-ignite: firewalls must be recycled from pool without instantiating any new GameObjects
    mgr->IgniteAtXZ({ 8.0f, 8.0f }, 1.5f);
    EXPECT_GT(mgr->GetFireGrid().GetActiveFireCount(), 0u);
    EXPECT_EQ(scene.GetPoolManager().GetPoolTotalCount("pool_tar_firewall"), totalSpawned);
    EXPECT_EQ(scene.GetPoolManager().GetPoolFreeCount("pool_tar_firewall"), 0u);
}

} // namespace
