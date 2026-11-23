#include <gtest/gtest.h>
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Renderer/SpriteAtlas.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/GameObject.h"

namespace {

TEST(SpriteRendererTest, DefaultPropertiesAndBillboardModes) {
    Engine::GameObject entity("TreeStandee");
    auto* sprite = entity.AddComponent<Engine::SpriteRenderer>();

    ASSERT_NE(sprite, nullptr);
    EXPECT_EQ(sprite->GetBillboardMode(), Engine::SpriteRenderer::BillboardMode::ViewPlane);
    EXPECT_FLOAT_EQ(sprite->GetColor().r, 1.0f);
    EXPECT_FLOAT_EQ(sprite->GetColor().a, 1.0f);
    EXPECT_FLOAT_EQ(sprite->GetAlphaCutoff(), 0.15f);
    EXPECT_TRUE(sprite->GetCastShadows());
    EXPECT_TRUE(sprite->GetReceiveLighting());
    EXPECT_FLOAT_EQ(sprite->GetEmissive(), 0.0f);
    EXPECT_FLOAT_EQ(sprite->GetWindSwayWeight(), 0.0f);

    sprite->SetWindSwayWeight(1.0f);
    EXPECT_FLOAT_EQ(sprite->GetWindSwayWeight(), 1.0f);

    // Change billboard mode
    sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::CylindricalY);
    EXPECT_EQ(sprite->GetBillboardMode(), Engine::SpriteRenderer::BillboardMode::CylindricalY);

    sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
    EXPECT_EQ(sprite->GetBillboardMode(), Engine::SpriteRenderer::BillboardMode::FlatGround);

    // Modify properties
    sprite->SetAlphaCutoff(0.35f);
    EXPECT_FLOAT_EQ(sprite->GetAlphaCutoff(), 0.35f);

    sprite->SetEmissive(2.5f);
    EXPECT_FLOAT_EQ(sprite->GetEmissive(), 2.5f);

    sprite->SetCastShadows(false);
    EXPECT_FALSE(sprite->GetCastShadows());
}

TEST(SubTexture2DTest, ManualBoundsAndTextureAccess) {
    glm::vec2 uvMin(0.25f, 0.25f);
    glm::vec2 uvMax(0.50f, 0.50f);

    Engine::SubTexture2D subTex(nullptr, uvMin, uvMax);
    EXPECT_EQ(subTex.GetTexture(), nullptr);
    EXPECT_FLOAT_EQ(subTex.GetMin().x, 0.25f);
    EXPECT_FLOAT_EQ(subTex.GetMin().y, 0.25f);
    EXPECT_FLOAT_EQ(subTex.GetMax().x, 0.50f);
    EXPECT_FLOAT_EQ(subTex.GetMax().y, 0.50f);
}

TEST(SubTexture2DTest, DivisionByZeroProtection) {
    // Null texture
    auto nullSubTex = Engine::SubTexture2D::CreateFromCoords(nullptr, {0, 0}, {32, 32});
    EXPECT_EQ(nullSubTex, nullptr);

    // 0x0 texture
    auto zeroTex = std::make_shared<Engine::Texture>(0u, 0u, nullptr);
    auto zeroSubTex = Engine::SubTexture2D::CreateFromCoords(zeroTex, {0, 0}, {32, 32});
    EXPECT_EQ(zeroSubTex, nullptr);
}

TEST(SpriteAtlasTest, AsyncPlaceholderUVNormalizedParity) {
    // Given an async 1x1 placeholder texture returned during JobSystem loading
    auto placeholderTex = Engine::Texture::CreatePlaceholder("hero_sheet.png");
    ASSERT_NE(placeholderTex, nullptr);
    EXPECT_EQ(placeholderTex->GetWidth(), 1);
    EXPECT_EQ(placeholderTex->GetHeight(), 1);

    // When parsing atlas json where meta.size defines the 512x512 sprite sheet
    const std::string atlasJson = R"({
        "frames": {
            "hero_walk_0": {
                "frame": { "x": 64, "y": 128, "w": 32, "h": 64 },
                "rotated": false,
                "trimmed": false,
                "spriteSourceSize": { "x": 0, "y": 0, "w": 32, "h": 64 },
                "sourceSize": { "w": 32, "h": 64 }
            }
        },
        "meta": {
            "image": "hero_sheet.png",
            "size": { "w": 512, "h": 512 },
            "scale": "1"
        }
    })";

    auto atlas = Engine::SpriteAtlas::CreateFromJSONString(atlasJson, placeholderTex);
    ASSERT_NE(atlas, nullptr);

    const auto* frame = atlas->GetFrame("hero_walk_0");
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(frame->SubTex, nullptr);

    // Frame UVs must be normalized against meta.size (512x512), NOT 1x1 placeholder
    // uMin = 64 / 512 = 0.125
    // uMax = (64 + 32) / 512 = 0.1875
    // vMin = (512 - (128 + 64)) / 512 = 320 / 512 = 0.625
    // vMax = (512 - 128) / 512 = 384 / 512 = 0.75
    EXPECT_NEAR(frame->SubTex->GetMin().x, 0.125f, 1e-4f);
    EXPECT_NEAR(frame->SubTex->GetMax().x, 0.1875f, 1e-4f);
    EXPECT_NEAR(frame->SubTex->GetMin().y, 0.625f, 1e-4f);
    EXPECT_NEAR(frame->SubTex->GetMax().y, 0.75f, 1e-4f);

    // UVs must stay strictly in [0, 1] range
    EXPECT_LE(frame->SubTex->GetMax().x, 1.0f);
    EXPECT_LE(frame->SubTex->GetMax().y, 1.0f);
    EXPECT_GE(frame->SubTex->GetMin().x, 0.0f);
    EXPECT_GE(frame->SubTex->GetMin().y, 0.0f);
}

} // namespace

