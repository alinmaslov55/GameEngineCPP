#include <gtest/gtest.h>
#include "Engine/Renderer/SpriteAtlas.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Core/ResourceManager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

TEST(TextureAnisotropyTest, QueryMaxAnisotropy) {
    const float maxAniso = Engine::Texture::GetMaxSupportedAnisotropy();
    EXPECT_GE(maxAniso, 1.0f);
}

TEST(TextureAnisotropyTest, MipmappedTextureAppliesAnisotropy) {
    std::vector<uint32_t> pixels(64 * 64, 0xFFFFFFFF);

    // Mipmapped texture
    auto texMip = std::make_shared<Engine::Texture>(64u, 64u, pixels.data(), true, false, true);
    EXPECT_TRUE(texMip->HasMipmaps());
    const float maxSupported = Engine::Texture::GetMaxSupportedAnisotropy();
    EXPECT_FLOAT_EQ(texMip->GetAnisotropy(), std::min(8.0f, maxSupported));

    // Non-mipmapped texture
    auto texNoMip = std::make_shared<Engine::Texture>(64u, 64u, pixels.data(), true, false, false);
    EXPECT_FALSE(texNoMip->HasMipmaps());
    EXPECT_FLOAT_EQ(texNoMip->GetAnisotropy(), 1.0f);

    // Changing linear filter updates anisotropy on mipmapped texture
    texMip->SetFilterLinear(false);
    EXPECT_FLOAT_EQ(texMip->GetAnisotropy(), 1.0f);
    texMip->SetFilterLinear(true);
    EXPECT_FLOAT_EQ(texMip->GetAnisotropy(), std::min(8.0f, maxSupported));

    // Custom anisotropy clamp
    texMip->SetAnisotropy(4.0f);
    EXPECT_FLOAT_EQ(texMip->GetAnisotropy(), std::min(4.0f, maxSupported));
}

TEST(SpriteAtlasTest, ParseTexturePackerJSONHash) {
    const std::string jsonHash = R"({
        "frames": {
            "spr_hero_run_d_00": {
                "frame": { "x": 0, "y": 0, "w": 128, "h": 256 },
                "rotated": false,
                "trimmed": true,
                "spriteSourceSize": { "x": 16, "y": 12, "w": 128, "h": 256 },
                "sourceSize": { "w": 160, "h": 280 },
                "pivot": { "x": 0.5, "y": 0.0 },
                "duration": 120
            },
            "spr_hero_run_d_01": {
                "frame": { "x": 128, "y": 0, "w": 128, "h": 256 },
                "rotated": false,
                "trimmed": false,
                "spriteSourceSize": { "x": 0, "y": 0, "w": 128, "h": 256 },
                "sourceSize": { "w": 128, "h": 256 },
                "pivot": { "x": 0.5, "y": 0.0 },
                "duration": 120
            },
            "spr_hero_idle_00": {
                "frame": { "x": 256, "y": 0, "w": 128, "h": 256 },
                "rotated": false,
                "trimmed": false,
                "duration": 200
            }
        },
        "meta": {
            "image": "hero_sheet.png",
            "size": { "w": 1024, "h": 1024 },
            "scale": "1"
        }
    })";

    auto atlas = Engine::SpriteAtlas::CreateFromJSONString(jsonHash);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->GetFrameCount(), 3u);

    // Verify spr_hero_run_d_00
    const auto* frame0 = atlas->GetFrame("spr_hero_run_d_00");
    ASSERT_NE(frame0, nullptr);
    EXPECT_EQ(frame0->Name, "spr_hero_run_d_00");
    EXPECT_TRUE(frame0->Trimmed);
    EXPECT_FALSE(frame0->Rotated);
    EXPECT_FLOAT_EQ(frame0->DurationMs, 120.0f);
    EXPECT_EQ(frame0->OriginalCanvasSizePx, glm::vec2(160.0f, 280.0f));
    EXPECT_EQ(frame0->TrimmedSizePx, glm::vec2(128.0f, 256.0f));
    EXPECT_EQ(frame0->SpriteSourceOffsetPx, glm::vec2(16.0f, 12.0f));
    EXPECT_EQ(frame0->PivotNormalized, glm::vec2(0.5f, 0.0f));

    // Verify OpenGL vertical flip in UV bounds:
    // x = 0, y = 0, w = 128, h = 256 on 1024x1024 atlas:
    // uMin = 0 / 1024 = 0.0
    // uMax = 128 / 1024 = 0.125
    // vMin = (1024 - (0 + 256)) / 1024 = 768 / 1024 = 0.75
    // vMax = (1024 - 0) / 1024 = 1.0
    ASSERT_NE(frame0->SubTex, nullptr);
    EXPECT_FLOAT_EQ(frame0->SubTex->GetMin().x, 0.0f);
    EXPECT_FLOAT_EQ(frame0->SubTex->GetMin().y, 0.75f);
    EXPECT_FLOAT_EQ(frame0->SubTex->GetMax().x, 0.125f);
    EXPECT_FLOAT_EQ(frame0->SubTex->GetMax().y, 1.0f);

    // Verify spr_hero_idle_00 defaults
    const auto* idleFrame = atlas->GetFrame("spr_hero_idle_00");
    ASSERT_NE(idleFrame, nullptr);
    EXPECT_FLOAT_EQ(idleFrame->DurationMs, 200.0f);
    EXPECT_EQ(idleFrame->PivotNormalized, glm::vec2(0.5f, 0.0f));
}

TEST(SpriteAtlasTest, ParseTexturePackerJSONArray) {
    const std::string jsonArray = R"({
        "frames": [
            {
                "filename": "spr_slash_fx_00",
                "frame": { "x": 0, "y": 512, "w": 256, "h": 256 },
                "rotated": false,
                "trimmed": true,
                "spriteSourceSize": { "x": 20, "y": 30, "w": 256, "h": 256 },
                "sourceSize": { "w": 300, "h": 300 },
                "pivot": { "x": 0.5, "y": 0.1 },
                "duration": 80
            },
            {
                "filename": "spr_slash_fx_01",
                "frame": { "x": 256, "y": 512, "w": 256, "h": 256 },
                "duration": 80
            }
        ],
        "meta": {
            "image": "vfx_sheet.png",
            "size": { "w": 2048, "h": 2048 }
        }
    })";

    auto atlas = Engine::SpriteAtlas::CreateFromJSONString(jsonArray);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->GetFrameCount(), 2u);

    const auto* f0 = atlas->GetFrame("spr_slash_fx_00");
    ASSERT_NE(f0, nullptr);
    EXPECT_EQ(f0->Name, "spr_slash_fx_00");
    EXPECT_FLOAT_EQ(f0->DurationMs, 80.0f);
    EXPECT_EQ(f0->PivotNormalized, glm::vec2(0.5f, 0.1f));

    // UVs on 2048x2048:
    // x = 0, y = 512, w = 256, h = 256:
    // uMin = 0 / 2048 = 0.0, uMax = 256 / 2048 = 0.125
    // vMin = (2048 - (512 + 256)) / 2048 = 1280 / 2048 = 0.625
    // vMax = (2048 - 512) / 2048 = 1536 / 2048 = 0.75
    ASSERT_NE(f0->SubTex, nullptr);
    EXPECT_FLOAT_EQ(f0->SubTex->GetMin().x, 0.0f);
    EXPECT_FLOAT_EQ(f0->SubTex->GetMin().y, 0.625f);
    EXPECT_FLOAT_EQ(f0->SubTex->GetMax().x, 0.125f);
    EXPECT_FLOAT_EQ(f0->SubTex->GetMax().y, 0.75f);
}

TEST(SpriteAtlasTest, PrefixQueryReturnsSortedFrames) {
    // Deliberately unsorted frame order in JSON
    const std::string jsonHash = R"({
        "frames": {
            "spr_hero_run_d_03": { "frame": { "x": 384, "y": 0, "w": 128, "h": 128 } },
            "spr_hero_run_d_01": { "frame": { "x": 128, "y": 0, "w": 128, "h": 128 } },
            "spr_hero_idle_00":  { "frame": { "x": 512, "y": 0, "w": 128, "h": 128 } },
            "spr_hero_run_d_00": { "frame": { "x": 0,   "y": 0, "w": 128, "h": 128 } },
            "spr_hero_run_d_02": { "frame": { "x": 256, "y": 0, "w": 128, "h": 128 } },
            "spr_hero_attack_00":{ "frame": { "x": 640, "y": 0, "w": 128, "h": 128 } }
        },
        "meta": {
            "size": { "w": 1024, "h": 1024 }
        }
    })";

    auto atlas = Engine::SpriteAtlas::CreateFromJSONString(jsonHash);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->GetFrameCount(), 6u);

    // Prefix query for spr_hero_run_d must return 4 frames sorted lexicographically: 00, 01, 02, 03
    auto runFrames = atlas->GetFramesByPrefix("spr_hero_run_d");
    ASSERT_EQ(runFrames.size(), 4u);
    EXPECT_EQ(runFrames[0]->Name, "spr_hero_run_d_00");
    EXPECT_EQ(runFrames[1]->Name, "spr_hero_run_d_01");
    EXPECT_EQ(runFrames[2]->Name, "spr_hero_run_d_02");
    EXPECT_EQ(runFrames[3]->Name, "spr_hero_run_d_03");

    // Single match query
    auto attackFrames = atlas->GetFramesByPrefix("spr_hero_attack");
    ASSERT_EQ(attackFrames.size(), 1u);
    EXPECT_EQ(attackFrames[0]->Name, "spr_hero_attack_00");

    // Nonexistent prefix query
    auto missingFrames = atlas->GetFramesByPrefix("spr_nonexistent");
    EXPECT_TRUE(missingFrames.empty());
}

TEST(SpriteAtlasTest, ResourceManagerCachingRoundTrip) {
    const std::string jsonContent = R"({
        "frames": {
            "spr_test_00": { "frame": { "x": 0, "y": 0, "w": 64, "h": 64 } },
            "spr_test_01": { "frame": { "x": 64, "y": 0, "w": 64, "h": 64 } }
        },
        "meta": {
            "size": { "w": 256, "h": 256 }
        }
    })";

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_atlas_fixture.atlas.json";
    {
        std::ofstream out(tempPath);
        ASSERT_TRUE(out.is_open());
        out << jsonContent;
    }

    auto atlas1 = Engine::ResourceManager::Load<Engine::SpriteAtlas>(tempPath.string());
    ASSERT_NE(atlas1, nullptr);
    EXPECT_EQ(atlas1->GetFrameCount(), 2u);

    auto atlas2 = Engine::ResourceManager::Load<Engine::SpriteAtlas>(tempPath.string());
    ASSERT_NE(atlas2, nullptr);
    // Verified asset cache hit returns the exact same shared_ptr
    EXPECT_EQ(atlas1.get(), atlas2.get());

    std::filesystem::remove(tempPath);
}

TEST(SpriteAtlasTest, GracefulFailureOnInvalidJSON) {
    auto invalidAtlas = Engine::SpriteAtlas::CreateFromJSONString("not valid json {");
    EXPECT_EQ(invalidAtlas, nullptr);

    auto nonObjectAtlas = Engine::SpriteAtlas::CreateFromJSONString("[1, 2, 3]");
    EXPECT_EQ(nonObjectAtlas, nullptr);
}

} // namespace
