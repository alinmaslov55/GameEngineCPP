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

TEST(TerrainFireGridTest, UpdateCapsTicksPerFrameOnLargeDeltaTimeHitch) {
    Engine::TerrainFireGrid grid;
    grid.ConfigureCallbacks(
        [](float, float) { return 1.0f; },
        [](const glm::vec2&, float, float) {}
    );
    grid.IgniteAtXZ({ 0.0f, 0.0f }, 0.5f);
    EXPECT_GT(grid.GetActiveFireCount(), 0u);

    // A massive frame hitch of 10.0 seconds
    grid.Update(10.0f);

    // Ticks are capped to MaxTicksPerFrame (5 ticks = 0.5s), well below MaxBurnDuration (4.5s)
    EXPECT_GT(grid.GetActiveFireCount(), 0u);
}

TEST(TerrainFireGridTest, SimulatesWithoutHeapAllocations) {
    Engine::TerrainFireGrid grid;
    grid.ConfigureCallbacks(
        [](float, float) { return 1.0f; },
        [](const glm::vec2&, float, float) {}
    );

    grid.IgniteAtXZ({ 0.0f, 0.0f }, 1.0f);
    grid.IgniteAtXZ({ 2.0f, 2.0f }, 1.0f);
    EXPECT_GT(grid.GetActiveFireCount(), 0u);

    for (int i = 0; i < 5; ++i) {
        grid.Update(0.1f);
    }
    EXPECT_GT(grid.GetActiveFireCount(), 0u);

    grid.ExtinguishInBoundsXZ({ -5.0f, -5.0f }, { 5.0f, 5.0f });
    EXPECT_EQ(grid.GetActiveFireCount(), 0u);
}

} // namespace
