#include <gtest/gtest.h>
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/TerrainChunkManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Core/JobSystem.h"
#include <filesystem>

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

} // namespace

