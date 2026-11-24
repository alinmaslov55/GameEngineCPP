#include <gtest/gtest.h>
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/TerrainHelpers.h"
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

TEST(TerrainArraySplatTest, ResizeR8BilinearAccurateInterpolation) {
    // 2x2 source
    const uint8_t src2x2[4] = { 10, 50, 100, 200 };

    // Same-size copy (2x2 -> 2x2)
    std::vector<uint8_t> outSame;
    Engine::Texture2DArray::ResizeR8Bilinear(src2x2, 2u, 2u, 2u, 2u, outSame);
    ASSERT_EQ(outSame.size(), 4u);
    EXPECT_EQ(outSame[0], 10u);
    EXPECT_EQ(outSame[1], 50u);
    EXPECT_EQ(outSame[2], 100u);
    EXPECT_EQ(outSame[3], 200u);

    // Upsample 2x2 -> 4x4
    std::vector<uint8_t> out4x4;
    Engine::Texture2DArray::ResizeR8Bilinear(src2x2, 2u, 2u, 4u, 4u, out4x4);
    ASSERT_EQ(out4x4.size(), 16u);
    // Values should be bounded within [10, 200]
    for (uint8_t val : out4x4) {
        EXPECT_GE(val, 10u);
        EXPECT_LE(val, 200u);
    }
    // Top-left pixel should be close to 10
    EXPECT_NEAR(static_cast<float>(out4x4[0]), 10.0f, 15.0f);
    // Bottom-right pixel should be close to 200
    EXPECT_NEAR(static_cast<float>(out4x4[15]), 200.0f, 35.0f);

    // Downsample 4x4 -> 1x1
    std::vector<uint8_t> out1x1;
    Engine::Texture2DArray::ResizeR8Bilinear(out4x4.data(), 4u, 4u, 1u, 1u, out1x1);
    ASSERT_EQ(out1x1.size(), 1u);
    EXPECT_GT(out1x1[0], 10u);
    EXPECT_LT(out1x1[0], 200u);

    // Null and empty dimension guards
    std::vector<uint8_t> outEmpty;
    Engine::Texture2DArray::ResizeR8Bilinear(nullptr, 2u, 2u, 4u, 4u, outEmpty);
    EXPECT_TRUE(outEmpty.empty());
    Engine::Texture2DArray::ResizeR8Bilinear(src2x2, 0u, 2u, 4u, 4u, outEmpty);
    EXPECT_TRUE(outEmpty.empty());
}

TEST(TerrainRendererTest, MultiTerrainDistinctLayerSetsDoNotThrashCache) {
    Engine::TerrainRenderer::Init();

    std::array<Engine::TerrainLayer, Engine::TerrainComponent::MaxLayers> layersA{};
    layersA[0].Glossiness = 0.25f;
    layersA[1].Glossiness = 0.50f;

    std::array<Engine::TerrainLayer, Engine::TerrainComponent::MaxLayers> layersB{};
    layersB[0].Glossiness = 0.85f;
    layersB[1].Glossiness = 0.10f;

    // Cache set A
    Engine::TerrainRenderer::SyncLayerTextures(layersA);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 1u);
    const uint64_t hashA = Engine::TerrainRenderer::GetActiveLayerSetHash();
    EXPECT_NE(hashA, 0ull);

    // Cache set B
    Engine::TerrainRenderer::SyncLayerTextures(layersB);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 2u);
    const uint64_t hashB = Engine::TerrainRenderer::GetActiveLayerSetHash();
    EXPECT_NE(hashB, 0ull);
    EXPECT_NE(hashA, hashB);

    // Switch back to A: must hit cache and NOT reallocate or rebuild
    Engine::TerrainRenderer::SyncLayerTextures(layersA);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 2u);
    EXPECT_EQ(Engine::TerrainRenderer::GetActiveLayerSetHash(), hashA);

    // Switch back to B: must hit cache and NOT reallocate or rebuild
    Engine::TerrainRenderer::SyncLayerTextures(layersB);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 2u);
    EXPECT_EQ(Engine::TerrainRenderer::GetActiveLayerSetHash(), hashB);

    // Capacity bound check: add sets C, D, E (cache capped at 4)
    auto layersC = layersA; layersC[2].Glossiness = 0.33f;
    auto layersD = layersA; layersD[2].Glossiness = 0.66f;
    auto layersE = layersA; layersE[2].Glossiness = 0.99f;
    Engine::TerrainRenderer::SyncLayerTextures(layersC);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 3u);
    Engine::TerrainRenderer::SyncLayerTextures(layersD);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 4u);
    Engine::TerrainRenderer::SyncLayerTextures(layersE);
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 4u);

    Engine::TerrainRenderer::Shutdown();
    EXPECT_EQ(Engine::TerrainRenderer::GetCachedLayerSetCount(), 0u);
}

TEST(TerrainHelpersTest, EvaluateBilinearSurfaceSampleZeroDimensionsSafety) {
    std::vector<uint8_t> emptySplat;
    // Calling with width == 0, height == 0 must not underflow or crash
    auto sample = Engine::TerrainHelpers::EvaluateBilinearSurfaceSample(emptySplat, emptySplat, 0u, 0u, 5.0f, 5.0f, false);
    EXPECT_EQ(sample.DominantBiome, Engine::BiomeType::AshBarrens);

    // Calling with width < 2
    auto sample1 = Engine::TerrainHelpers::EvaluateBilinearSurfaceSample(emptySplat, emptySplat, 1u, 64u, 0.5f, 0.5f, false);
    EXPECT_EQ(sample1.DominantBiome, Engine::BiomeType::AshBarrens);
}

TEST(TerrainComponentBoundsTest, DecodeSplatBase64RejectsInvalidDimensions) {
    Engine::TerrainComponent terrain(16.0f, 16.0f, 64u, 64u);
    std::string dummyData = "AAAA"; // minimal string

    // Sub-4 dimension rejection
    EXPECT_FALSE(terrain.DecodeSplatBase64(dummyData, 1u, 1u));
    EXPECT_FALSE(terrain.DecodeSplatBase64(dummyData, 2u, 64u));
    EXPECT_FALSE(terrain.DecodeSplatBase64(dummyData, 64u, 3u));

    // Exceeding 2048 rejection
    EXPECT_FALSE(terrain.DecodeSplatBase64(dummyData, 4096u, 4096u));
    EXPECT_FALSE(terrain.DecodeSplatBase64(dummyData, 2049u, 64u));

    // Same for DecodeSplat1Base64
    EXPECT_FALSE(terrain.DecodeSplat1Base64(dummyData, 1u, 1u));
    EXPECT_FALSE(terrain.DecodeSplat1Base64(dummyData, 4096u, 4096u));
}

} // namespace
