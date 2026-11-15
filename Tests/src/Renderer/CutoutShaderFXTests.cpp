#include <gtest/gtest.h>
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/StandeeCommand.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneSerializer.h"

#include <cstddef>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {

TEST(QuadVertexLayoutTest, StrideAndOffsetsMatch80ByteLayout) {
    EXPECT_EQ(sizeof(Engine::QuadVertex), 80u);
    EXPECT_EQ(sizeof(Engine::QuadVertex) % 16, 0u);

    EXPECT_EQ(offsetof(Engine::QuadVertex, Position), 0u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, Color), 12u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, TexCoord), 28u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, TexIndex), 36u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, AlphaCutoff), 40u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, Emissive), 44u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, ShadowOffset), 48u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, WindParams), 52u);
    EXPECT_EQ(offsetof(Engine::QuadVertex, CutoutFX), 64u);
}

TEST(SpriteRendererCombatFXTest, DefaultValues) {
    auto go = std::make_unique<Engine::GameObject>("TestEntity");
    auto* sprite = go->AddComponent<Engine::SpriteRenderer>();

    EXPECT_FLOAT_EQ(sprite->GetDissolveAmount(), 0.0f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveEdgeWidth(), 0.08f);
    EXPECT_FLOAT_EQ(sprite->GetFlashAmount(), 0.0f);
    EXPECT_FLOAT_EQ(sprite->GetFlashColorMode(), 0.0f);
    EXPECT_FALSE(sprite->GetRenderSilhouette());
    EXPECT_EQ(sprite->GetSilhouetteColor(), glm::vec4(0.25f, 0.35f, 0.85f, 0.28f));

    glm::vec4 fx = sprite->GetCutoutFX();
    EXPECT_FLOAT_EQ(fx.x, 0.0f);
    EXPECT_FLOAT_EQ(fx.y, 0.0f);
    EXPECT_FLOAT_EQ(fx.z, 0.0f);
    EXPECT_FLOAT_EQ(fx.w, 0.08f);
}

TEST(SpriteRendererCombatFXTest, PropertyClampingAndFlashMode) {
    auto go = std::make_unique<Engine::GameObject>("TestEntity");
    auto* sprite = go->AddComponent<Engine::SpriteRenderer>();

    // Dissolve clamping [0, 1]
    sprite->SetDissolveAmount(-0.5f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveAmount(), 0.0f);
    sprite->SetDissolveAmount(1.5f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveAmount(), 1.0f);
    sprite->SetDissolveAmount(0.45f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveAmount(), 0.45f);

    // Edge width clamping [0.001, 0.5]
    sprite->SetDissolveEdgeWidth(-0.05f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveEdgeWidth(), 0.001f);
    sprite->SetDissolveEdgeWidth(0.8f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveEdgeWidth(), 0.5f);
    sprite->SetDissolveEdgeWidth(0.12f);
    EXPECT_FLOAT_EQ(sprite->GetDissolveEdgeWidth(), 0.12f);

    // Flash amount clamping [0, 1]
    sprite->SetFlashAmount(-0.2f);
    EXPECT_FLOAT_EQ(sprite->GetFlashAmount(), 0.0f);
    sprite->SetFlashAmount(1.3f);
    EXPECT_FLOAT_EQ(sprite->GetFlashAmount(), 1.0f);
    sprite->SetFlashAmount(0.85f);
    EXPECT_FLOAT_EQ(sprite->GetFlashAmount(), 0.85f);

    // Preset flash modes
    sprite->SetFlashMode(Engine::SpriteRenderer::FlashMode::PureWhite);
    EXPECT_FLOAT_EQ(sprite->GetFlashColorMode(), 0.0f);
    sprite->SetFlashMode(Engine::SpriteRenderer::FlashMode::CrimsonImpact);
    EXPECT_FLOAT_EQ(sprite->GetFlashColorMode(), 1.0f);
    sprite->SetFlashMode(Engine::SpriteRenderer::FlashMode::SpectralBlue);
    EXPECT_FLOAT_EQ(sprite->GetFlashColorMode(), 2.0f);
    sprite->SetFlashMode(Engine::SpriteRenderer::FlashMode::DivineGold);
    EXPECT_FLOAT_EQ(sprite->GetFlashColorMode(), 3.0f);

    // Custom 24-bit packed RGB color
    sprite->SetFlashColor(glm::vec3(1.0f, 0.5f, 0.25f));
    EXPECT_GE(sprite->GetFlashColorMode(), 4.0f);
    glm::vec3 unpackedColor = sprite->GetFlashColor();
    EXPECT_NEAR(unpackedColor.r, 1.0f, 0.01f);
    EXPECT_NEAR(unpackedColor.g, 0.5f, 0.01f);
    EXPECT_NEAR(unpackedColor.b, 0.25f, 0.01f);

    // Silhouette toggle & color
    sprite->SetRenderSilhouette(true);
    EXPECT_TRUE(sprite->GetRenderSilhouette());
    sprite->SetSilhouetteColor(glm::vec4(0.85f, 0.15f, 0.15f, 0.35f));
    EXPECT_EQ(sprite->GetSilhouetteColor(), glm::vec4(0.85f, 0.15f, 0.15f, 0.35f));

    // CutoutFX packed vector verification
    glm::vec4 fx = sprite->GetCutoutFX();
    EXPECT_FLOAT_EQ(fx.x, 0.45f);
    EXPECT_FLOAT_EQ(fx.y, 0.85f);
    EXPECT_FLOAT_EQ(fx.z, sprite->GetFlashColorMode());
    EXPECT_FLOAT_EQ(fx.w, 0.12f);
}

TEST(SceneSerializerCombatFXTest, RoundTripSerialization) {
    auto sourceScene = std::make_shared<Engine::Scene>("SourceScene");
    auto* go = sourceScene->CreateGameObject("CombatEntity");
    auto* sprite = go->AddComponent<Engine::SpriteRenderer>();

    sprite->SetDissolveAmount(0.65f);
    sprite->SetDissolveEdgeWidth(0.15f);
    sprite->SetFlashAmount(0.90f);
    sprite->SetFlashMode(Engine::SpriteRenderer::FlashMode::CrimsonImpact);
    sprite->SetRenderSilhouette(true);
    sprite->SetSilhouetteColor(glm::vec4(0.1f, 0.7f, 0.4f, 0.32f));

    Engine::SceneSerializer serializer(sourceScene);
    std::string jsonStr = serializer.SerializeToString();
    EXPECT_FALSE(jsonStr.empty());

    auto destScene = std::make_shared<Engine::Scene>("DestScene");
    Engine::SceneSerializer deserializer(destScene);
    bool success = deserializer.DeserializeFromString(jsonStr);
    EXPECT_TRUE(success);

    ASSERT_EQ(destScene->GetGameObjects().size(), 1u);
    auto* restoredGo = destScene->GetGameObjects()[0].get();
    ASSERT_NE(restoredGo, nullptr);
    auto* restoredSprite = restoredGo->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(restoredSprite, nullptr);

    EXPECT_NEAR(restoredSprite->GetDissolveAmount(), 0.65f, 1e-4f);
    EXPECT_NEAR(restoredSprite->GetDissolveEdgeWidth(), 0.15f, 1e-4f);
    EXPECT_NEAR(restoredSprite->GetFlashAmount(), 0.90f, 1e-4f);
    EXPECT_FLOAT_EQ(restoredSprite->GetFlashColorMode(), 1.0f);
    EXPECT_TRUE(restoredSprite->GetRenderSilhouette());
    const glm::vec4 silCol = restoredSprite->GetSilhouetteColor();
    EXPECT_NEAR(silCol.r, 0.1f, 1e-4f);
    EXPECT_NEAR(silCol.g, 0.7f, 1e-4f);
    EXPECT_NEAR(silCol.b, 0.4f, 1e-4f);
    EXPECT_NEAR(silCol.a, 0.32f, 1e-4f);
}

TEST(StandeeCommandCombatFXTest, CommandDefaultInitialization) {
    Engine::StandeeRenderCommand cmd;
    EXPECT_FLOAT_EQ(cmd.cutoutFX.x, 0.0f);
    EXPECT_FLOAT_EQ(cmd.cutoutFX.y, 0.0f);
    EXPECT_FLOAT_EQ(cmd.cutoutFX.z, 0.0f);
    EXPECT_FLOAT_EQ(cmd.cutoutFX.w, 0.08f);
    EXPECT_FALSE(cmd.renderSilhouette);
    EXPECT_EQ(cmd.silhouetteColor, glm::vec4(0.25f, 0.35f, 0.85f, 0.28f));
}

} // namespace
