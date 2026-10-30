#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "Engine/Utils/CutoutProcessor.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Texture.h"
#include <stb_image_write.h>
#include <filesystem>
#include <vector>

namespace {

TEST(CutoutProcessorTest, ChromaGreenAndMagentaKeyingBottomFlushAndAlphaEdgeBleed) {
    // 1. Synthesize a 24x24 #00FF00 chroma-green image containing a crimson banner silhouette [x:8..15, y:4..17]
    //    with a 6px floating green margin at the bottom (y:18..23) AND an interior 2x2 #00FF00 hole at [x:11..12, y:9..10]
    uint32_t w = 24u, h = 24u;
    std::vector<uint8_t> greenImg(static_cast<size_t>(w) * h * 4u, 0u);
    auto setPx = [&](std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        buf[idx + 0] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
        buf[idx + 3] = a;
    };

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            setPx(greenImg, w, x, y, 0u, 255u, 0u, 255u);
        }
    }
    for (uint32_t y = 4; y <= 17; ++y) {
        for (uint32_t x = 8; x <= 15; ++x) {
            setPx(greenImg, w, x, y, 165u, 28u, 36u, 255u); // Deep cult crimson
        }
    }
    // Carve an interior #00FF00 hole inside the crimson banner
    for (uint32_t y = 9; y <= 10; ++y) {
        for (uint32_t x = 11; x <= 12; ++x) {
            setPx(greenImg, w, x, y, 0u, 255u, 0u, 255u);
        }
    }

    Engine::CutoutProcessSettings settings;
    settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
    settings.AutoTrimAndBottomFlush = true;
    settings.HorizontalPaddingPx = 4u;
    settings.TopPaddingPx = 4u;
    settings.AlphaBleedPasses = 3u;

    const auto resGreen = Engine::CutoutProcessor::ProcessRGBAInPlace(greenImg, w, h, settings);
    ASSERT_TRUE(resGreen.Success);
    EXPECT_EQ(resGreen.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaGreen);
    EXPECT_EQ(resGreen.ContentWidth, 8u);   // x: 8..15
    EXPECT_EQ(resGreen.ContentHeight, 14u); // y: 4..17
    EXPECT_EQ(w, 8u + 2u * 4u);             // 16px wide
    EXPECT_EQ(h, 14u + 4u);                 // 18px tall (bottom-flush -> zero bottom padding!)

    auto getPixel = [&](const std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        return glm::u8vec4(buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]);
    };

    // Top-left corner must be fully transparent
    EXPECT_EQ(getPixel(greenImg, w, 0u, 0u).a, 0u);
    // Bottom row (y = h - 1 = 17) inside content [x = 6] MUST be 100% opaque crimson (bottom-flush to UV.y = 0!)
    const glm::u8vec4 bottomCrimson = getPixel(greenImg, w, 6u, h - 1u);
    EXPECT_EQ(bottomCrimson.a, 255u);
    EXPECT_GT(bottomCrimson.r, 150u);
    // Interior hole (originally [11, 9] -> shifted to [4 + (11-8)=7, 4 + (9-4)=9]) MUST be keyed transparent (A == 0)
    const glm::u8vec4 holePx = getPixel(greenImg, w, 7u, 9u);
    EXPECT_EQ(holePx.a, 0u);
    // Alpha-edge RGB bleed: transparent neighbor immediately left of crimson silhouette [x = 3, y = h - 1]
    // keeps A == 0 while inheriting dilated crimson RGB (R > 140, G < 50) instead of #00FF00 green!
    const glm::u8vec4 bledNeighbor = getPixel(greenImg, w, 3u, h - 1u);
    EXPECT_EQ(bledNeighbor.a, 0u);
    EXPECT_GT(bledNeighbor.r, 140u);
    EXPECT_LT(bledNeighbor.g, 50u);

    // 2. Test Chroma-Magenta (#FF00FF) keying + Disk File Round-Trip + ResourceManager::Reload<Texture>
    uint32_t mw = 20u, mh = 20u;
    std::vector<uint8_t> magImg(static_cast<size_t>(mw) * mh * 4u, 0u);
    for (uint32_t y = 0; y < mh; ++y) {
        for (uint32_t x = 0; x < mw; ++x) {
            setPx(magImg, mw, x, y, 255u, 0u, 255u, 255u); // #FF00FF magenta key
        }
    }
    // Paint emerald green grass blades [x:7..12, y:5..14] — must NOT be erased on magenta background!
    for (uint32_t y = 5; y <= 14; ++y) {
        for (uint32_t x = 7; x <= 12; ++x) {
            setPx(magImg, mw, x, y, 36u, 178u, 58u, 255u);
        }
    }

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_cutout_processor_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path rawMagPng = tempDir / "raw_grass_magenta.png";
    const std::filesystem::path outPng = tempDir / "grass_cutout.png";

    stbi_flip_vertically_on_write(0);
    ASSERT_NE(stbi_write_png(rawMagPng.string().c_str(), static_cast<int>(mw), static_cast<int>(mh), 4, magImg.data(), static_cast<int>(mw * 4u)), 0);

    // Load initial uncropped 20x20 texture into ResourceManager cache
    auto cachedTex = Engine::ResourceManager::Load<Engine::Texture>(rawMagPng.string());
    ASSERT_NE(cachedTex, nullptr);
    EXPECT_EQ(cachedTex->GetWidth(), 20);
    EXPECT_EQ(cachedTex->GetHeight(), 20);

    // Process via CutoutProcessor::ProcessFile directly over rawMagPng & also to outPng
    const auto resFile = Engine::CutoutProcessor::ProcessFile(rawMagPng, outPng, settings);
    ASSERT_TRUE(resFile.Success);
    EXPECT_EQ(resFile.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaMagenta);
    EXPECT_EQ(resFile.OutputWidth, 6u + 2u * 4u); // 14px
    EXPECT_EQ(resFile.OutputHeight, 10u + 4u);    // 14px (bottom-flush)

    ASSERT_TRUE(Engine::CutoutProcessor::ProcessFile(outPng, rawMagPng, settings).Success);
    auto reloadedTex = Engine::ResourceManager::Reload<Engine::Texture>(rawMagPng.string());
    EXPECT_EQ(reloadedTex, cachedTex); // In-place reload preserves existing shared_ptr identity!
    EXPECT_EQ(reloadedTex->GetWidth(), static_cast<int>(resFile.OutputWidth));
    EXPECT_EQ(reloadedTex->GetHeight(), static_cast<int>(resFile.OutputHeight));

    const auto resMag = Engine::CutoutProcessor::ProcessRGBAInPlace(magImg, mw, mh, settings);
    ASSERT_TRUE(resMag.Success);
    EXPECT_EQ(resMag.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaMagenta);
    EXPECT_EQ(getPixel(magImg, mw, 6u, mh - 1u).a, 255u);
    EXPECT_GT(getPixel(magImg, mw, 6u, mh - 1u).g, 160u); // Emerald green preserved intact!

    std::filesystem::remove_all(tempDir, ec);
}

} // namespace

