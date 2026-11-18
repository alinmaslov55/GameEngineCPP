#include <gtest/gtest.h>
#include "Engine/Particles/DecalManager.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/TerrainChunkManager.h"

#include <cmath>

namespace {

TEST(DecalManagerTest, RingBufferOverflowOverwritesOldestInOrder) {
    Engine::DecalManager manager(512u);
    EXPECT_EQ(manager.GetCapacity(), 512u);
    EXPECT_EQ(manager.GetActiveCount(), 0u);

    // Spawn 1,000 decals into a 512-capacity ring buffer
    for (uint32_t i = 0; i < 1000u; ++i) {
        Engine::GroundDecal decal;
        decal.Position = { static_cast<float>(i), 0.0f, 0.0f };
        decal.SizeXZ = { 1.2f, 1.2f };
        decal.MaxLifetime = 45.0f;
        decal.FadeOutDuration = 4.0f;
        manager.SpawnDecal(decal);
    }

    // Active count must cap strictly at 512
    ASSERT_EQ(manager.GetActiveCount(), 512u);

    // Oldest surviving decal must be #488, newest must be #999, in chronological order
    for (uint32_t k = 0; k < 512u; ++k) {
        const auto& d = manager.GetActiveDecal(k);
        EXPECT_FLOAT_EQ(d.Position.x, static_cast<float>(488u + k));
        // Auto-lifted Y must be >= kBaseLiftY (0.005f) to prevent Z-fighting
        EXPECT_GE(d.Position.y, Engine::DecalManager::kBaseLiftY);
        EXPECT_LT(d.Position.y, Engine::DecalManager::kBaseLiftY + 0.01f);
    }

    // Consecutive overlapping spawns must receive strictly distinct Y micro-offsets
    const float y0 = manager.GetActiveDecal(0).Position.y;
    const float y1 = manager.GetActiveDecal(1).Position.y;
    EXPECT_NE(y0, y1);
}

TEST(DecalManagerTest, AlphaFadeOutAndCleanExpirationCompaction) {
    Engine::DecalManager manager(512u);

    // Spawn 10 decals with 45s lifetime (4s fade), and insert a 10s decal in the middle
    for (int i = 0; i < 5; ++i) {
        Engine::GroundDecal d;
        d.Position = { static_cast<float>(i), 0.0f, 0.0f };
        d.Color = { 0.8f, 0.1f, 0.1f, 0.8f };
        d.MaxLifetime = 45.0f;
        d.FadeOutDuration = 4.0f;
        manager.SpawnDecal(d);
    }
    {
        Engine::GroundDecal shortLived;
        shortLived.Position = { 999.0f, 0.0f, 0.0f };
        shortLived.Color = { 0.2f, 0.2f, 0.2f, 1.0f };
        shortLived.MaxLifetime = 10.0f;
        shortLived.FadeOutDuration = 2.0f;
        manager.SpawnDecal(shortLived);
    }
    for (int i = 5; i < 10; ++i) {
        Engine::GroundDecal d;
        d.Position = { static_cast<float>(i), 0.0f, 0.0f };
        d.Color = { 0.8f, 0.1f, 0.1f, 0.8f };
        d.MaxLifetime = 45.0f;
        d.FadeOutDuration = 4.0f;
        manager.SpawnDecal(d);
    }
    ASSERT_EQ(manager.GetActiveCount(), 11u);

    // At t = 0: full alpha
    EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(0)), 0.8f, 1e-5f);

    // Advance 10.0s -> middle short-lived decal expires and compacts cleanly preserving chronological order
    manager.Update(10.0f);
    ASSERT_EQ(manager.GetActiveCount(), 10u);
    for (uint32_t i = 0; i < 10u; ++i) {
        EXPECT_FLOAT_EQ(manager.GetActiveDecal(i).Position.x, static_cast<float>(i));
        EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(i)), 0.8f, 1e-5f);
    }

    // Advance 31.0s (total Age = 41.0s, remaining = 4.0s -> fade threshold begins)
    manager.Update(31.0f);
    ASSERT_EQ(manager.GetActiveCount(), 10u);
    EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(0)), 0.8f, 1e-5f);

    // Step 1.0s at a time across the final 4.0s fade window and verify smooth linear fade
    manager.Update(1.0f); // Age = 42.0s, remaining = 3.0s -> 75% of 0.8 = 0.6
    ASSERT_EQ(manager.GetActiveCount(), 10u);
    EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(0)), 0.6f, 1e-5f);

    manager.Update(1.0f); // Age = 43.0s, remaining = 2.0s -> 50% of 0.8 = 0.4
    ASSERT_EQ(manager.GetActiveCount(), 10u);
    EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(0)), 0.4f, 1e-5f);

    manager.Update(1.0f); // Age = 44.0s, remaining = 1.0s -> 25% of 0.8 = 0.2
    ASSERT_EQ(manager.GetActiveCount(), 10u);
    EXPECT_NEAR(Engine::DecalManager::ComputeEffectiveAlpha(manager.GetActiveDecal(0)), 0.2f, 1e-5f);

    // Final 1.0s (Age = 45.0s) -> all remaining decals expire and compact to 0
    manager.Update(1.0f);
    EXPECT_EQ(manager.GetActiveCount(), 0u);
}

TEST(DecalManagerTest, ClearInChunkEvictsOnlyTargetChunkAndPreservesOrder) {
    Engine::DecalManager manager(32u);
    constexpr float kChunkSize = 32.0f;

    auto spawnAt = [&](float x, float z) {
        Engine::GroundDecal d;
        d.Position = { x, 0.0f, z };
        manager.SpawnDecal(d);
    };

    // Chunk (-1, -1): [-32, 0) x [-32, 0)
    spawnAt(-10.0f, -10.0f);
    // Chunk (0, 0): [0, 32) x [0, 32) including exact lower boundary (0, 0)
    spawnAt(0.0f, 0.0f);
    spawnAt(15.5f, 20.0f);
    // Chunk (1, 0): [32, 64) x [0, 32) including exact lower boundary (32, 0)
    spawnAt(32.0f, 0.0f);
    spawnAt(50.0f, 12.0f);

    ASSERT_EQ(manager.GetActiveCount(), 5u);

    // Clear chunk (0, 0)
    manager.ClearInChunk(0, 0, kChunkSize);
    ASSERT_EQ(manager.GetActiveCount(), 3u);

    // Surviving decals must preserve exact chronological order: (-10,-10), (32,0), (50,12)
    EXPECT_FLOAT_EQ(manager.GetActiveDecal(0).Position.x, -10.0f);
    EXPECT_FLOAT_EQ(manager.GetActiveDecal(1).Position.x,  32.0f);
    EXPECT_FLOAT_EQ(manager.GetActiveDecal(2).Position.x,  50.0f);
}

TEST(DecalManagerTest, FrustumCullingInHeadlessRenderPass) {
    Engine::DecalManager manager(32u);

    Engine::PerspectiveCamera camera(32.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    camera.SetFocalPoint({ 0.0f, 0.0f, 0.0f });
    camera.SetDistance(15.0f);
    camera.SetPitchDegrees(40.0f);
    camera.SetYawDegrees(0.0f);

    // 3 visible decals near origin
    for (int i = -1; i <= 1; ++i) {
        Engine::GroundDecal visible;
        visible.Position = { static_cast<float>(i) * 2.0f, 0.0f, 0.0f };
        visible.SizeXZ = { 1.5f, 1.5f };
        visible.RotationYRadians = 0.75f * static_cast<float>(i);
        manager.SpawnDecal(visible);
    }

    // 2 off-screen decals far outside frustum
    Engine::GroundDecal offscreen1;
    offscreen1.Position = { 500.0f, 0.0f, 0.0f };
    manager.SpawnDecal(offscreen1);

    Engine::GroundDecal offscreen2;
    offscreen2.Position = { 0.0f, 0.0f, 500.0f };
    manager.SpawnDecal(offscreen2);

    ASSERT_EQ(manager.GetActiveCount(), 5u);

    // Render in headless mode (Renderer2D::SubmitVertices gracefully no-ops without GL context)
    manager.Render(camera.GetFrustum());
    EXPECT_EQ(manager.GetLastRenderedCount(), 3u);
    EXPECT_EQ(manager.GetLastCulledCount(), 2u);
}

TEST(DecalManagerTest, SceneLifecycleAndTerrainChunkEvictionIntegration) {
    Engine::Scene scene("DecalIntegrationScene");

    // 1. Spawn decal in chunk (0, 0) and verify Scene::Update ages and expires it
    Engine::GroundDecal d1;
    d1.Position = { 8.0f, 0.0f, 8.0f };
    d1.MaxLifetime = 5.0f;
    d1.FadeOutDuration = 1.0f;
    scene.GetDecalManager().SpawnDecal(d1);
    EXPECT_EQ(scene.GetDecalManager().GetActiveCount(), 1u);

    scene.Update(2.5f);
    ASSERT_EQ(scene.GetDecalManager().GetActiveCount(), 1u);
    EXPECT_NEAR(scene.GetDecalManager().GetActiveDecal(0).Age, 2.5f, 1e-5f);

    scene.Update(3.0f);
    EXPECT_EQ(scene.GetDecalManager().GetActiveCount(), 0u);

    // 2. Verify TerrainChunkManager chunk eviction automatically clears decals in evicted chunks
    auto* terrainGo = scene.CreateGameObject("ChunkedWorld");
    auto* chunkMgr = terrainGo->AddComponent<Engine::TerrainChunkManager>();
    chunkMgr->ChunkSizeMeters = 32.0f;
    chunkMgr->ActiveRadiusChunks = 0;
    chunkMgr->CacheRetentionRadius = 0;

    // Create chunk (0, 0) via viewer at (16, 0, 16) and flush async generation
    chunkMgr->UpdateStreaming({ 16.0f, 0.0f, 16.0f });
    chunkMgr->FlushPendingGenerations();
    ASSERT_NE(chunkMgr->GetChunk({ 0, 0 }), nullptr);

    // Spawn decal in chunk (0, 0) and another in chunk (5, 0)
    Engine::GroundDecal nearOrigin;
    nearOrigin.Position = { 10.0f, 0.0f, 10.0f };
    scene.GetDecalManager().SpawnDecal(nearOrigin);

    Engine::GroundDecal farChunk;
    farChunk.Position = { 170.0f, 0.0f, 10.0f }; // chunk (5, 0)
    scene.GetDecalManager().SpawnDecal(farChunk);
    ASSERT_EQ(scene.GetDecalManager().GetActiveCount(), 2u);

    // Move viewer to chunk (5, 0) -> chunk (0, 0) is evicted and its decal must be cleared
    chunkMgr->UpdateStreaming({ 170.0f, 0.0f, 10.0f });
    chunkMgr->FlushPendingGenerations();
    EXPECT_EQ(chunkMgr->GetChunk({ 0, 0 }), nullptr);
    ASSERT_EQ(scene.GetDecalManager().GetActiveCount(), 1u);
    EXPECT_FLOAT_EQ(scene.GetDecalManager().GetActiveDecal(0).Position.x, 170.0f);

    // 3. Verify OnRuntimeStop clears remaining runtime decals
    scene.OnRuntimeStart();
    scene.OnRuntimeStop();
    EXPECT_EQ(scene.GetDecalManager().GetActiveCount(), 0u);
}

} // namespace
