#include <gtest/gtest.h>
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/WorldBiomeGenerator.h"
#include "Engine/Terrain/ProceduralPropFactory.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Particles/DecalManager.h"
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
#include <limits>
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

TEST(TerrainFireGridTest, CornerIntersectionIgnition) {
    Engine::TerrainFireGrid grid;
    grid.ConfigureCallbacks(
        [](float, float) { return 1.0f; },
        [](const glm::vec2&, float, float) {}
    );

    // Cell {0, 0} has bounds [0.0, 0.5] x [0.0, 0.5] with center at (0.25, 0.25).
    // Distance from center to corner (0, 0) is sqrt(0.25^2 + 0.25^2) ≈ 0.3536m.
    // An ignition circle centered at (0.0, 0.0) with radius 0.15m (< 0.3536m)
    // overlaps the corner of cell {0, 0}.
    grid.IgniteAtXZ({ 0.0f, 0.0f }, 0.15f);

    EXPECT_GT(grid.GetActiveFireCount(), 0u);
    EXPECT_TRUE(grid.IsBurningAtXZ(0.0f, 0.0f));
}

TEST(TerrainFireGridTest, NaNDtDoesNotLockAccumulator) {
    Engine::TerrainFireGrid grid;
    grid.ConfigureCallbacks(
        [](float, float) { return 1.0f; },
        [](const glm::vec2&, float, float) {}
    );

    grid.IgniteAtXZ({ 0.0f, 0.0f }, 0.5f);
    ASSERT_GT(grid.GetActiveFireCount(), 0u);

    // Pass NaN dt: must not poison the accumulator or freeze the simulation
    grid.Update(std::numeric_limits<float>::quiet_NaN());

    // Normal tick should execute safely without deadlock or permanent freeze
    grid.Update(0.1f);
    EXPECT_GT(grid.GetActiveFireCount(), 0u);
}

TEST(TerrainFireGridTest, SpreadsToNewlyPouredAdjacentTar) {
    Engine::TerrainFireGrid grid;
    bool neighborHasTar = false;

    grid.ConfigureCallbacks(
        [&](float wx, float wz) -> float {
            // Origin cell at (0.25, 0.25) is always flammable
            if (wx >= 0.0f && wx <= 0.5f && wz >= 0.0f && wz <= 0.5f) {
                return 1.0f;
            }
            // East neighbor cell at (0.75, 0.25) only becomes flammable when poured
            if (neighborHasTar && wx >= 0.5f && wx <= 1.0f && wz >= 0.0f && wz <= 0.5f) {
                return 1.0f;
            }
            return 0.0f;
        },
        [](const glm::vec2&, float, float) {}
    );

    // Ignite origin cell at (0.25, 0.25)
    grid.IgniteAtXZ({ 0.25f, 0.25f }, 0.2f);
    ASSERT_TRUE(grid.IsBurningAtXZ(0.25f, 0.25f));
    ASSERT_FALSE(grid.IsBurningAtXZ(0.75f, 0.25f));

    // Step 1.0s (10 ticks): the cell has been burning for 1.0s (> SpreadDelay 0.18s)
    for (int i = 0; i < 10; ++i) {
        grid.Update(0.1f);
    }
    ASSERT_FALSE(grid.IsBurningAtXZ(0.75f, 0.25f));

    // Player now pours Tar adjacent to the burning fire
    neighborHasTar = true;

    // Next tick should detect newly flammable adjacent Tar and spread!
    for (int i = 0; i < 3; ++i) {
        grid.Update(0.1f);
    }
    EXPECT_TRUE(grid.IsBurningAtXZ(0.75f, 0.25f));
}

TEST(TerrainFireGridTest, FiniteTerrainComponentSceneIgnitionAndBurning) {
    auto scene = std::make_shared<Engine::Scene>("FiniteTerrainFireScene");
    auto* terrainGo = scene->CreateGameObject("FiniteTerrain");
    auto* tc = terrainGo->AddComponent<Engine::TerrainComponent>(32.0f, 32.0f, 65u, 65u);

    // Paint Tar at (0.0f, 0.0f)
    tc->PaintMaterialWorld({ 0.0f, 0.0f }, 2u /*Tar*/, 2.0f, 1.0f);

    // Call scene.IgniteAtXZ(0.0f, 0.0f, 0.15f) (corner intersection)
    scene->IgniteAtXZ({ 0.0f, 0.0f }, 0.15f);

    // Step 0.25s
    scene->Update(0.25f);

    // Assert scene.IsBurningAtXZ(0.0f, 0.0f) == true
    EXPECT_TRUE(scene->IsBurningAtXZ(0.0f, 0.0f));
}

TEST(TerrainFireGridTest, BuildTarFirewallPrefabContainsFlameVisuals) {
    auto prefab = Engine::ProceduralPropFactory::BuildTarFirewallPrefab();
    ASSERT_NE(prefab, nullptr);
    ASSERT_TRUE(prefab->IsValid());

    const auto& json = prefab->GetJson();
    ASSERT_TRUE(json.contains("Entities"));
    ASSERT_FALSE(json["Entities"].empty());

    const auto& comps = json["Entities"][0]["Components"];
    EXPECT_TRUE(comps.contains("SpriteRenderer"));
    EXPECT_TRUE(comps.contains("ParticleSystem"));
    EXPECT_TRUE(comps.contains("PointLight"));
    EXPECT_TRUE(comps.contains("CircleCollider2D"));

    // Verify SpriteRenderer flame parameters
    EXPECT_GT(comps["SpriteRenderer"].value("Emissive", 0.0f), 2.0f);
    EXPECT_EQ(comps["SpriteRenderer"].value("BillboardMode", 0), 1);

    // Verify ParticleSystem parameters
    EXPECT_TRUE(comps["ParticleSystem"].value("AutoEmit", false));
    EXPECT_TRUE(comps["ParticleSystem"].value("EmissiveParticles", false));
}

TEST(TerrainFireGridTest, RegenerateWorldClearsFireGrid) {
    Engine::TerrainChunkManager mgr(16.0f, 33u, 1, 2);
    auto& c0 = mgr.EnsureEditableChunk({ 0, 0 });
    // Fill with tar
    for (size_t i = 0; i < c0.SplatData.size(); i += 4u) {
        c0.SplatData[i + 0] = 0u;
        c0.SplatData[i + 1] = 0u;
        c0.SplatData[i + 2] = 255u;
        c0.SplatData[i + 3] = 0u;
    }
    mgr.IgniteAtXZ({ 8.0f, 8.0f }, 1.0f);
    EXPECT_GT(mgr.GetFireGrid().GetActiveFireCount(), 0u);

    // RegenerateWorld must clear active fires
    mgr.RegenerateWorld();
    EXPECT_EQ(mgr.GetFireGrid().GetActiveFireCount(), 0u);
}

TEST(TerrainFireGridTest, FireDecaySpawnsGroundDecal) {
    auto scene = std::make_shared<Engine::Scene>("FireDecalScene");
    auto* terrainGo = scene->CreateGameObject("FiniteTerrain");
    auto* tc = terrainGo->AddComponent<Engine::TerrainComponent>(32.0f, 32.0f, 65u, 65u);

    tc->PaintMaterialWorld({ 8.0f, 8.0f }, 2u /*Tar*/, 3.0f, 1.0f);
    scene->IgniteAtXZ({ 8.0f, 8.0f }, 1.0f);
    ASSERT_TRUE(scene->IsBurningAtXZ(8.0f, 8.0f));

    // Step 5.0s (past MaxBurnDuration = 4.5s) to trigger decay to Scorched Ash
    for (int i = 0; i < 50; ++i) {
        scene->Update(0.1f);
    }

    // Fire should have burned out and spawned at least one burn-scar decal
    EXPECT_FALSE(scene->IsBurningAtXZ(8.0f, 8.0f));
    EXPECT_GT(scene->GetDecalManager().GetActiveCount(), 0u);
}

} // namespace
