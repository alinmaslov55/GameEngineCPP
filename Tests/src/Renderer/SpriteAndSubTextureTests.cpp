#include <gtest/gtest.h>
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/SubTexture2D.h"
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

} // namespace

