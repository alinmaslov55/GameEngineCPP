#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "Engine/Utils/CutoutProcessor.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SpriteAtlas.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <filesystem>
#include <fstream>
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

TEST(CutoutProcessorTest, ProcessAnimationStripNormalizesVaryingReachAndLocksPivot) {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "cutout_strip_norm_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);

    auto setPx = [&](std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        buf[idx + 0] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
        buf[idx + 3] = a;
    };

    const uint32_t frameW = 32u, frameH = 32u;
    std::vector<std::filesystem::path> frameFiles;

    // Create 4 frames on #00FF00 green screen:
    // Torso in all 4 frames: x in [12..18], y in [10..24] (feet at y = 24)
    // Frame 0 (idle): dagger at x in [17..19], y in [14..18]
    // Frame 1 (windup back): axe raised back-left at x in [6..12], y in [4..12]
    // Frame 2 (thrust wide reach): blade reaching wide right at x in [19..28], y in [12..16]
    // Frame 3 (recovery): blade resting down at x in [16..19], y in [18..24]
    for (int i = 0; i < 4; ++i) {
        std::vector<uint8_t> frameData(static_cast<size_t>(frameW) * frameH * 4u, 0u);
        for (uint32_t y = 0; y < frameH; ++y) {
            for (uint32_t x = 0; x < frameW; ++x) {
                setPx(frameData, frameW, x, y, 0u, 255u, 0u, 255u);
            }
        }
        for (uint32_t y = 10; y <= 24; ++y) {
            for (uint32_t x = 12; x <= 18; ++x) {
                setPx(frameData, frameW, x, y, 120u, 80u, 40u, 255u);
            }
        }
        if (i == 0) {
            for (uint32_t y = 14; y <= 18; ++y) {
                for (uint32_t x = 17; x <= 19; ++x) {
                    setPx(frameData, frameW, x, y, 180u, 200u, 220u, 255u);
                }
            }
        } else if (i == 1) {
            for (uint32_t y = 4; y <= 12; ++y) {
                for (uint32_t x = 6; x <= 12; ++x) {
                    setPx(frameData, frameW, x, y, 180u, 200u, 220u, 255u);
                }
            }
        } else if (i == 2) {
            for (uint32_t y = 12; y <= 16; ++y) {
                for (uint32_t x = 19; x <= 28; ++x) {
                    setPx(frameData, frameW, x, y, 180u, 200u, 220u, 255u);
                }
            }
        } else if (i == 3) {
            for (uint32_t y = 18; y <= 24; ++y) {
                for (uint32_t x = 16; x <= 19; ++x) {
                    setPx(frameData, frameW, x, y, 180u, 200u, 220u, 255u);
                }
            }
        }

        const auto filePath = tempDir / ("hero_raw_" + std::to_string(i) + ".png");
        stbi_flip_vertically_on_write(0);
        ASSERT_NE(stbi_write_png(filePath.string().c_str(), static_cast<int>(frameW), static_cast<int>(frameH), 4, frameData.data(), static_cast<int>(frameW * 4u)), 0);
        frameFiles.push_back(filePath);
    }

    const auto outSheetPng = tempDir / "hero_sheet.png";
    const auto outAtlasJson = tempDir / "hero_sheet.atlas.json";

    Engine::SheetCutoutSettings settings;
    settings.BaseSettings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
    settings.BaseSettings.HorizontalPaddingPx = 2u;
    settings.BaseSettings.TopPaddingPx = 2u;
    settings.BaseSettings.AlphaBleedPasses = 2u;
    settings.LockSharedUnionBounds = true;
    settings.LockBottomFootPivot = true;
    settings.LockHorizontalCenter = true;
    settings.FrameDurationMs = 80.0f;
    settings.FrameNamePrefix = "spr_hero_chop";

    const auto res = Engine::CutoutProcessor::ProcessAnimationStrip(frameFiles, outSheetPng, outAtlasJson, settings);
    ASSERT_TRUE(res.Success);

    EXPECT_EQ(res.ContentWidth, 23u);
    EXPECT_EQ(res.ContentHeight, 21u);
    EXPECT_EQ(res.OutputWidth, 108u);
    EXPECT_EQ(res.OutputHeight, 23u);
    EXPECT_TRUE(std::filesystem::exists(outSheetPng));
    EXPECT_TRUE(std::filesystem::exists(outAtlasJson));

    int loadedW = 0, loadedH = 0, loadedChannels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* sheetPixels = stbi_load(outSheetPng.string().c_str(), &loadedW, &loadedH, &loadedChannels, 4);
    ASSERT_NE(sheetPixels, nullptr);
    EXPECT_EQ(loadedW, 108);
    EXPECT_EQ(loadedH, 23);

    auto getSheetPx = [&](int x, int y) {
        const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(loadedW) + static_cast<size_t>(x)) * 4u;
        return glm::u8vec4(sheetPixels[idx + 0], sheetPixels[idx + 1], sheetPixels[idx + 2], sheetPixels[idx + 3]);
    };

    for (int i = 0; i < 4; ++i) {
        const int cellOriginX = i * 27;
        const int torsoX = cellOriginX + 11;
        const auto torsoPx = getSheetPx(torsoX, 12);
        EXPECT_EQ(torsoPx.a, 255u);
        EXPECT_GT(torsoPx.r, 80u);

        const auto footPx = getSheetPx(torsoX, 22);
        EXPECT_EQ(footPx.a, 255u);
    }

    EXPECT_EQ(getSheetPx(0 * 27 + 24, 14).a, 0u);
    EXPECT_EQ(getSheetPx(2 * 27 + 24, 14).a, 255u);

    stbi_image_free(sheetPixels);

    auto atlas = Engine::SpriteAtlas::LoadFromJSON(outAtlasJson.string());
    ASSERT_NE(atlas, nullptr);
    EXPECT_NE(atlas->GetFrame("spr_hero_chop_00"), nullptr);
    EXPECT_NE(atlas->GetFrame("spr_hero_chop_01"), nullptr);
    EXPECT_NE(atlas->GetFrame("spr_hero_chop_02"), nullptr);
    EXPECT_NE(atlas->GetFrame("spr_hero_chop_03"), nullptr);

    auto frames = atlas->GetFramesByPrefix("spr_hero_chop");
    EXPECT_EQ(frames.size(), 4u);
    EXPECT_FLOAT_EQ(frames[0]->DurationMs, 80.0f);
    EXPECT_FLOAT_EQ(frames[0]->PivotNormalized.x, 0.5f);
    EXPECT_FLOAT_EQ(frames[0]->PivotNormalized.y, 0.0f);

    std::filesystem::remove_all(tempDir, ec);
}

TEST(CutoutProcessorTest, ProcessAnimationStripSingleSheetGridSlicing) {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "cutout_grid_slice_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);

    auto setPx = [&](std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        buf[idx + 0] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
        buf[idx + 3] = a;
    };

    const uint32_t gridW = 40u, gridH = 40u;
    std::vector<uint8_t> gridImg(static_cast<size_t>(gridW) * gridH * 4u, 0u);
    for (uint32_t y = 0; y < gridH; ++y) {
        for (uint32_t x = 0; x < gridW; ++x) {
            setPx(gridImg, gridW, x, y, 255u, 0u, 255u, 255u);
        }
    }

    for (uint32_t y = 5; y <= 14; ++y) {
        for (uint32_t x = 5; x <= 14; ++x) {
            setPx(gridImg, gridW, x, y, 220u, 30u, 30u, 255u);
        }
    }
    for (uint32_t y = 5; y <= 14; ++y) {
        for (uint32_t x = 25; x <= 34; ++x) {
            setPx(gridImg, gridW, x, y, 30u, 220u, 30u, 255u);
        }
    }
    for (uint32_t y = 25; y <= 34; ++y) {
        for (uint32_t x = 5; x <= 14; ++x) {
            setPx(gridImg, gridW, x, y, 30u, 30u, 220u, 255u);
        }
    }
    for (uint32_t y = 25; y <= 34; ++y) {
        for (uint32_t x = 25; x <= 34; ++x) {
            setPx(gridImg, gridW, x, y, 220u, 180u, 30u, 255u);
        }
    }

    const auto inGridPng = tempDir / "grid_raw.png";
    const auto outSheetPng = tempDir / "grid_sheet.png";
    const auto outAtlasJson = tempDir / "grid_sheet.atlas.json";

    stbi_flip_vertically_on_write(0);
    ASSERT_NE(stbi_write_png(inGridPng.string().c_str(), static_cast<int>(gridW), static_cast<int>(gridH), 4, gridImg.data(), static_cast<int>(gridW * 4u)), 0);

    Engine::SheetCutoutSettings settings;
    settings.BaseSettings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
    settings.Columns = 2u;
    settings.Rows = 2u;
    settings.BaseSettings.HorizontalPaddingPx = 1u;
    settings.BaseSettings.TopPaddingPx = 1u;
    settings.FrameNamePrefix = "prop_gem";

    const auto res = Engine::CutoutProcessor::ProcessAnimationStrip({ inGridPng }, outSheetPng, outAtlasJson, settings);
    ASSERT_TRUE(res.Success);
    EXPECT_EQ(res.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaMagenta);

    auto atlas = Engine::SpriteAtlas::LoadFromJSON(outAtlasJson.string());
    ASSERT_NE(atlas, nullptr);
    auto frames = atlas->GetFramesByPrefix("prop_gem");
    EXPECT_EQ(frames.size(), 4u);
    EXPECT_NE(atlas->GetFrame("prop_gem_00"), nullptr);
    EXPECT_NE(atlas->GetFrame("prop_gem_01"), nullptr);
    EXPECT_NE(atlas->GetFrame("prop_gem_02"), nullptr);
    EXPECT_NE(atlas->GetFrame("prop_gem_03"), nullptr);

    std::filesystem::remove_all(tempDir, ec);
}

TEST(CutoutProcessorTest, ProcessAnimationStripAsyncExecutesInBackground) {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "cutout_async_strip_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);

    const uint32_t frameW = 16u, frameH = 16u;
    std::vector<uint8_t> frameData(static_cast<size_t>(frameW) * frameH * 4u, 0u);
    for (uint32_t y = 0; y < frameH; ++y) {
        for (uint32_t x = 0; x < frameW; ++x) {
            const size_t idx = (static_cast<size_t>(y) * frameW + x) * 4u;
            frameData[idx + 0] = 0u;
            frameData[idx + 1] = 255u;
            frameData[idx + 2] = 0u;
            frameData[idx + 3] = 255u;
        }
    }
    for (uint32_t y = 4; y <= 11; ++y) {
        for (uint32_t x = 4; x <= 11; ++x) {
            const size_t idx = (static_cast<size_t>(y) * frameW + x) * 4u;
            frameData[idx + 0] = 255u;
            frameData[idx + 1] = 255u;
            frameData[idx + 2] = 255u;
            frameData[idx + 3] = 255u;
        }
    }

    const auto inFrame = tempDir / "async_frame_0.png";
    const auto outSheet = tempDir / "async_sheet.png";
    const auto outJson = tempDir / "async_sheet.atlas.json";

    stbi_flip_vertically_on_write(0);
    ASSERT_NE(stbi_write_png(inFrame.string().c_str(), static_cast<int>(frameW), static_cast<int>(frameH), 4, frameData.data(), static_cast<int>(frameW * 4u)), 0);

    Engine::JobSystem::Init();

    std::atomic<bool> completed{ false };
    Engine::CutoutProcessResult asyncResult;

    auto job = Engine::CutoutProcessor::ProcessAnimationStripAsync(
        { inFrame },
        outSheet,
        outJson,
        {},
        [&](const Engine::CutoutProcessResult& res) {
            asyncResult = res;
            completed.store(true);
        });

    Engine::JobSystem::Wait(job);

    EXPECT_TRUE(completed.load());
    EXPECT_TRUE(asyncResult.Success);
    EXPECT_TRUE(std::filesystem::exists(outSheet));
    EXPECT_TRUE(std::filesystem::exists(outJson));

    std::filesystem::remove_all(tempDir, ec);
}

} // namespace

