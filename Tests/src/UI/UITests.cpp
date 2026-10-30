#include <gtest/gtest.h>
#include "Engine/UI/Font.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Scene/GameObject.h"

namespace {

TEST(FontTest, AddAndRetrieveCharactersWithFallback) {
    Engine::Font font;
    EXPECT_TRUE(font.GetCharacters().empty());

    Engine::Character charA;
    charA.size = { 16.0f, 24.0f };
    charA.bearing = { 0.0f, 20.0f };
    charA.advance = 18.0f;

    Engine::Character questionMark;
    questionMark.size = { 14.0f, 22.0f };
    questionMark.bearing = { 1.0f, 19.0f };
    questionMark.advance = 16.0f;

    font.AddCharacter('A', charA);
    font.AddCharacter('?', questionMark);

    EXPECT_EQ(font.GetCharacters().size(), 2u);

    // Existing character lookup
    const auto& retrievedA = font.GetCharacter('A');
    EXPECT_FLOAT_EQ(retrievedA.size.x, 16.0f);
    EXPECT_FLOAT_EQ(retrievedA.advance, 18.0f);

    // Missing character lookup falls back to '?'
    const auto& fallback = font.GetCharacter('Z');
    EXPECT_FLOAT_EQ(fallback.size.x, 14.0f);
    EXPECT_FLOAT_EQ(fallback.advance, 16.0f);
}

TEST(FontTest, MissingCharacterWithoutFallbackReturnsEmpty) {
    Engine::Font fontWithoutFallback;
    const auto& emptyChar = fontWithoutFallback.GetCharacter('X');
    EXPECT_FLOAT_EQ(emptyChar.size.x, 0.0f);
    EXPECT_FLOAT_EQ(emptyChar.advance, 0.0f);
}

TEST(UILabelTest, ComponentCreationAndProperties) {
    Engine::GameObject entity("TextEntity");
    auto font = std::make_shared<Engine::Font>();

    auto* label = entity.AddComponent<Engine::UILabel>("Hello World", font, glm::vec2(100.0f, 200.0f));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello World");
    EXPECT_EQ(label->font, font);
    EXPECT_FLOAT_EQ(label->position.x, 100.0f);
    EXPECT_FLOAT_EQ(label->position.y, 200.0f);
    EXPECT_FLOAT_EQ(label->scale, 1.0f);
    EXPECT_FLOAT_EQ(label->color.a, 1.0f);
}

TEST(UIPanelTest, ComponentCreationAndProperties) {
    Engine::GameObject entity("PanelEntity");

    auto* panel = entity.AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 75.0f),
        glm::vec2(300.0f, 150.0f),
        glm::vec4(0.2f, 0.4f, 0.8f, 0.9f)
    );

    ASSERT_NE(panel, nullptr);
    EXPECT_FLOAT_EQ(panel->position.x, 50.0f);
    EXPECT_FLOAT_EQ(panel->position.y, 75.0f);
    EXPECT_FLOAT_EQ(panel->size.x, 300.0f);
    EXPECT_FLOAT_EQ(panel->size.y, 150.0f);
    EXPECT_FLOAT_EQ(panel->color.r, 0.2f);
    EXPECT_FLOAT_EQ(panel->color.a, 0.9f);
    EXPECT_EQ(panel->texture, nullptr);
}

} // namespace

