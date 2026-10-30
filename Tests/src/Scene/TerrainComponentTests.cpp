#include <gtest/gtest.h>
#include "Engine/Scene/TerrainComponent.h"
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

} // namespace

