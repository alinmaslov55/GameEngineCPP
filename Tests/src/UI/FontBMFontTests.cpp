#include <gtest/gtest.h>
#include "Engine/UI/Font.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/UI/UIButtonComponent.h"
#include "Engine/UI/UIRadialWheelComponent.h"
#include "Engine/UI/MinimapFogSystem.h"
#include "Engine/UI/UIMinimapComponent.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include <algorithm>
#include <vector>

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

TEST(FontTest, AsciiLookupAndSilentFallback) {
    Engine::Font font;
    Engine::Character charM;
    charM.size = { 12.0f, 18.0f };
    charM.advance = 14.0f;
    font.AddCharacter('M', charM);

    // O(1) table lookup
    EXPECT_FLOAT_EQ(font.GetCharacter('M').advance, 14.0f);

    // Space / newline / missing glyph lookup does not throw or crash and returns empty
    const auto& spaceChar = font.GetCharacter(' ');
    EXPECT_FLOAT_EQ(spaceChar.size.x, 0.0f);
    EXPECT_FLOAT_EQ(spaceChar.advance, 0.0f);

    // Now add '?'
    Engine::Character questionMark;
    questionMark.size = { 10.0f, 16.0f };
    questionMark.advance = 12.0f;
    font.AddCharacter('?', questionMark);

    // Missing glyph falls back to '?'
    EXPECT_FLOAT_EQ(font.GetCharacter(' ').advance, 12.0f);
    EXPECT_FLOAT_EQ(font.GetCharacter('\n').advance, 12.0f);
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

TEST(FontBMFontTest, ParseSampleAngelCodeFntHeadless) {
    const std::string sampleFnt =
        "info face=\"GothicDiorama\" size=32 bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacings=0,0\n"
        "common lineHeight=40 base=30 scaleW=512 scaleH=512 pages=1 packed=0\n"
        "page id=0 file=\"font_gothic_diorama.png\"\n"
        "chars count=2\n"
        "char id=65 x=10 y=20 width=30 height=40 xoffset=2 yoffset=5 xadvance=32 page=0 chnl=15\n"
        "char id=86 x=50 y=20 width=30 height=40 xoffset=1 yoffset=5 xadvance=32 page=0 chnl=15\n"
        "kernings count=1\n"
        "kerning first=65 second=86 amount=-4\n";

    auto font = Engine::Font::LoadBMFontFromMemory(sampleFnt, nullptr);
    ASSERT_NE(font, nullptr);

    EXPECT_FLOAT_EQ(font->GetLineHeight(), 40.0f);
    EXPECT_FLOAT_EQ(font->GetBase(), 30.0f);

    // Kerning verification
    EXPECT_FLOAT_EQ(font->GetKerning('A', 'V'), -4.0f);
    EXPECT_FLOAT_EQ(font->GetKerning('V', 'A'), 0.0f);

    // Glyph 'A' metrics
    const auto& glyphA = font->GetCharacter('A');
    EXPECT_FLOAT_EQ(glyphA.size.x, 30.0f);
    EXPECT_FLOAT_EQ(glyphA.size.y, 40.0f);
    EXPECT_FLOAT_EQ(glyphA.bearing.x, 2.0f);
    EXPECT_FLOAT_EQ(glyphA.bearing.y, 30.0f - 5.0f); // base - yoffset = 25
    EXPECT_FLOAT_EQ(glyphA.advance, 32.0f);

    ASSERT_NE(glyphA.subTexture, nullptr);
    // UV slicing: u = [10/512, 40/512], v = [1.0 - 60/512, 1.0 - 20/512]
    EXPECT_NEAR(glyphA.subTexture->GetMin().x, 10.0f / 512.0f, 1e-5f);
    EXPECT_NEAR(glyphA.subTexture->GetMax().x, 40.0f / 512.0f, 1e-5f);
    EXPECT_NEAR(glyphA.subTexture->GetMin().y, 1.0f - (60.0f / 512.0f), 1e-5f);
    EXPECT_NEAR(glyphA.subTexture->GetMax().y, 1.0f - (20.0f / 512.0f), 1e-5f);
}

TEST(FontBMFontTest, MultiLineWordWrappingAndWidthBounds) {
    const std::string sampleFnt =
        "common lineHeight=30 base=24 scaleW=512 scaleH=512\n"
        "chars count=3\n"
        "char id=65 x=0 y=0 width=20 height=24 xoffset=0 yoffset=0 xadvance=20\n" // 'A'
        "char id=86 x=0 y=0 width=20 height=24 xoffset=0 yoffset=0 xadvance=20\n" // 'V'
        "char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=0 xadvance=10\n";   // space ' '

    auto font = Engine::Font::LoadBMFontFromMemory(sampleFnt, nullptr);
    ASSERT_NE(font, nullptr);

    // Each "AV" is 40px wide, space is 10px wide.
    // "AV AV AV":
    // Word 1: "AV" (40px)
    // Space: (10px) -> cursor at 50px
    // Word 2: "AV" (40px) -> fits within 100px (cursor reaches 90px)
    // Space: (10px) -> cursor at 100px
    // Word 3: "AV" (40px) -> 100 + 40 = 140px > 100px wrap width -> wraps to next line!
    const auto layout = font->ComputeLayout("AV AV AV", glm::vec2(0.0f), 1.0f, 100.0f, Engine::TextAlignH::Left);

    EXPECT_EQ(layout.lineCount, 2u);
    EXPECT_LE(layout.bounds.x, 100.0f);
    EXPECT_FLOAT_EQ(layout.bounds.y, 60.0f); // 2 lines * 30 lineHeight
    EXPECT_EQ(layout.glyphs.size(), 6u);     // 3 'A's and 3 'V's
}

TEST(FontBMFontTest, CenterAndRightAlignmentOffsets) {
    const std::string sampleFnt =
        "common lineHeight=30 base=24 scaleW=512 scaleH=512\n"
        "chars count=2\n"
        "char id=65 x=0 y=0 width=30 height=24 xoffset=0 yoffset=0 xadvance=30\n"
        "char id=86 x=0 y=0 width=30 height=24 xoffset=0 yoffset=0 xadvance=30\n";

    auto font = Engine::Font::LoadBMFontFromMemory(sampleFnt, nullptr);
    ASSERT_NE(font, nullptr);

    // "AV" is 60px wide. Max container width = 160px.
    // Left alignment: start at 0.0f
    const auto leftLayout = font->ComputeLayout("AV", glm::vec2(0.0f), 1.0f, 160.0f, Engine::TextAlignH::Left);
    ASSERT_EQ(leftLayout.glyphs.size(), 2u);
    EXPECT_FLOAT_EQ(leftLayout.glyphs[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(leftLayout.glyphs[1].position.x, 30.0f);

    // Center alignment: offset = (160 - 60) * 0.5 = 50px
    const auto centerLayout = font->ComputeLayout("AV", glm::vec2(0.0f), 1.0f, 160.0f, Engine::TextAlignH::Center);
    ASSERT_EQ(centerLayout.glyphs.size(), 2u);
    EXPECT_FLOAT_EQ(centerLayout.glyphs[0].position.x, 50.0f);
    EXPECT_FLOAT_EQ(centerLayout.glyphs[1].position.x, 80.0f);

    // Right alignment: offset = (160 - 60) = 100px
    const auto rightLayout = font->ComputeLayout("AV", glm::vec2(0.0f), 1.0f, 160.0f, Engine::TextAlignH::Right);
    ASSERT_EQ(rightLayout.glyphs.size(), 2u);
    EXPECT_FLOAT_EQ(rightLayout.glyphs[0].position.x, 100.0f);
    EXPECT_FLOAT_EQ(rightLayout.glyphs[1].position.x, 130.0f);
}

TEST(UILabelTest, StyledTextSceneSerializerRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>();
    auto* go = sourceScene->CreateGameObject("PromptLabel");
    const uint64_t goUUID = go->GetUUID();

    auto* label = go->AddComponent<Engine::UILabel>();
    label->text = "[E] Feed 10 Wood (0/10)";
    label->position = { 100.0f, 200.0f };
    label->scale = 1.25f;
    label->color = { 1.0f, 0.9f, 0.8f, 1.0f };
    label->alignment = Engine::TextAlignH::Center;
    label->maxWrapWidth = 250.0f;
    label->dropShadow = true;
    label->shadowOffset = { 3.0f, -3.0f };
    label->shadowColor = { 0.1f, 0.05f, 0.1f, 0.9f };

    const std::string serializedJson = Engine::SceneSerializer(sourceScene).SerializeToString();

    auto destScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(destScene).DeserializeFromString(serializedJson));

    auto* dstObj = destScene->FindGameObjectByUUID(goUUID);
    ASSERT_NE(dstObj, nullptr);

    auto* dstLabel = dstObj->GetComponent<Engine::UILabel>();
    ASSERT_NE(dstLabel, nullptr);
    EXPECT_EQ(dstLabel->text, "[E] Feed 10 Wood (0/10)");
    EXPECT_FLOAT_EQ(dstLabel->scale, 1.25f);
    EXPECT_FLOAT_EQ(dstLabel->color.r, 1.0f);
    EXPECT_FLOAT_EQ(dstLabel->color.g, 0.9f);
    EXPECT_EQ(dstLabel->alignment, Engine::TextAlignH::Center);
    EXPECT_FLOAT_EQ(dstLabel->maxWrapWidth, 250.0f);
    EXPECT_TRUE(dstLabel->dropShadow);
    EXPECT_FLOAT_EQ(dstLabel->shadowOffset.x, 3.0f);
    EXPECT_FLOAT_EQ(dstLabel->shadowOffset.y, -3.0f);
    EXPECT_FLOAT_EQ(dstLabel->shadowColor.r, 0.1f);
    EXPECT_FLOAT_EQ(dstLabel->shadowColor.a, 0.9f);
}

TEST(FontBMFontTest, Utf8MultiByteEmDashDecoding) {
    Engine::Font font;
    font.SetLineHeight(30.0f);
    font.SetBase(24.0f);

    Engine::Character ch{};
    ch.size = { 12.0f, 20.0f };
    ch.advance = 14.0f;
    for (char c : std::string_view("AshBone")) {
        font.AddCharacter(c, ch);
    }

    Engine::Character emDash{};
    emDash.size = { 24.0f, 4.0f };
    emDash.advance = 26.0f;
    font.AddCharacter(0x2014u, emDash); // U+2014 (—)

    // "Ash — Bone": 3 ('Ash') + 1 (emDash) + 4 ('Bone') = 8 visible glyphs (NOT 10 with 3 mojibake bytes)
    const auto layout = font.ComputeLayout("Ash \xE2\x80\x94 Bone", glm::vec2(0.0f), 1.0f);
    ASSERT_EQ(layout.glyphs.size(), 8u);
    EXPECT_EQ(layout.glyphs[3].codepoint, 0x2014u);
    EXPECT_FLOAT_EQ(layout.glyphs[3].size.x, 24.0f);
}

TEST(FontBMFontTest, MalformedFntLinesRejectedCleanly) {
    const std::string malformedFnt =
        "common lineHeight=-10 base=-5 scaleW=512 scaleH=512\n"
        "char x=0 y=0 width=10 height=10 xadvance=10\n"                    // missing id=
        "char id=-1 x=0 y=0 width=10 height=10 xadvance=10\n"             // negative id
        "char id=66 x=-5 y=0 width=10 height=10 xadvance=10\n"            // negative x
        "char id=67 x=500 y=0 width=32 height=10 xadvance=10\n"           // out-of-atlas bounds
        "char id=65 x=10 y=10 width=20 height=20 xoffset=0 yoffset=0 xadvance=22\n"; // valid 'A'

    auto font = Engine::Font::LoadBMFontFromMemory(malformedFnt, nullptr);
    ASSERT_NE(font, nullptr);
    EXPECT_FLOAT_EQ(font->GetLineHeight(), 32.0f); // Default preserved when lineHeight <= 0
    EXPECT_FLOAT_EQ(font->GetCharacter('A').advance, 22.0f);
    EXPECT_FLOAT_EQ(font->GetCharacter('B').advance, 0.0f);
    EXPECT_FLOAT_EQ(font->GetCharacter('C').advance, 0.0f);
}

TEST(FontBMFontTest, WrappedLinesTrimTrailingSpaceForCenterAndRightAlignment) {
    const std::string sampleFnt =
        "common lineHeight=30 base=24 scaleW=512 scaleH=512\n"
        "chars count=2\n"
        "char id=65 x=0 y=0 width=20 height=24 xoffset=0 yoffset=0 xadvance=20\n" // 'A'
        "char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=0 xadvance=10\n";  // ' '

    auto font = Engine::Font::LoadBMFontFromMemory(sampleFnt, nullptr);
    ASSERT_NE(font, nullptr);

    // "AA AA" with maxWrapWidth = 60px:
    // Line 0 = "AA" (40px, trailing space 10px must be trimmed!)
    // Line 1 = "AA" (40px)
    const auto rightLayout = font->ComputeLayout("AA AA", glm::vec2(0.0f), 1.0f, 60.0f, Engine::TextAlignH::Right);
    ASSERT_EQ(rightLayout.lineCount, 2u);
    ASSERT_EQ(rightLayout.glyphs.size(), 4u);
    // Right aligned in 60px container -> 60 - 40 = 20px offset for BOTH lines
    EXPECT_FLOAT_EQ(rightLayout.glyphs[0].position.x, 20.0f);
    EXPECT_FLOAT_EQ(rightLayout.glyphs[2].position.x, 20.0f);

    const auto centerLayout = font->ComputeLayout("AA AA", glm::vec2(0.0f), 1.0f, 60.0f, Engine::TextAlignH::Center);
    ASSERT_EQ(centerLayout.lineCount, 2u);
    EXPECT_FLOAT_EQ(centerLayout.glyphs[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(centerLayout.glyphs[2].position.x, 10.0f);
}

} // namespace
