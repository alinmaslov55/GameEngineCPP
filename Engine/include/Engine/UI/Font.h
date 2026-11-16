#ifndef UI_FONT_H
#define UI_FONT_H

#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

enum class TextAlignH : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2
};

struct Character {
    std::shared_ptr<SubTexture2D> subTexture{nullptr};
    glm::vec2 size{0.0f};       // Width and height of the glyph
    glm::vec2 bearing{0.0f};    // Offset from baseline to left/top of glyph
    float advance{0.0f};        // Horizontal distance to the next character
};

struct GlyphLayout {
    uint32_t codepoint = 0;
    glm::vec2 position{ 0.0f }; // Screen coordinates (bottom-left of glyph quad)
    glm::vec2 size{ 0.0f };
    std::shared_ptr<SubTexture2D> subTexture{ nullptr };
};

struct TextLayoutResult {
    std::vector<GlyphLayout> glyphs;
    glm::vec2 bounds{ 0.0f }; // (totalWidth, totalHeight)
    uint32_t lineCount = 0;
};

class Font {
public:
    // The single atlas texture containing all characters
    std::shared_ptr<Texture> atlasTexture{nullptr};

    void AddCharacter(char c, const Character& characterData) {
        AddCharacter(static_cast<uint32_t>(static_cast<uint8_t>(c)), characterData);
    }

    void AddCharacter(uint32_t codepoint, const Character& characterData) {
        if (codepoint < 256) {
            m_AsciiTable[codepoint] = characterData;
            m_HasAsciiChar[codepoint] = true;
            m_Characters[static_cast<char>(codepoint)] = characterData;
        } else {
            m_ExtendedCharacters[codepoint] = characterData;
        }
    }

    const Character& GetCharacter(char c) const {
        return GetCharacter(static_cast<uint32_t>(static_cast<uint8_t>(c)));
    }

    const Character& GetCharacter(uint32_t codepoint) const {
        if (codepoint < 256) {
            if (m_HasAsciiChar[codepoint]) {
                return m_AsciiTable[codepoint];
            }
        } else {
            auto it = m_ExtendedCharacters.find(codepoint);
            if (it != m_ExtendedCharacters.end()) {
                return it->second;
            }
        }

        // Silent fallback to '?' if present, avoiding disk flushes from log warnings
        const auto qIdx = static_cast<uint8_t>('?');
        if (m_HasAsciiChar[qIdx]) {
            return m_AsciiTable[qIdx];
        }

        static const Character s_EmptyCharacter{};
        return s_EmptyCharacter;
    }

    const std::unordered_map<char, Character>& GetCharacters() const { return m_Characters; }

    void AddKerning(uint32_t firstCodepoint, uint32_t secondCodepoint, float amount) {
        const uint64_t key = (static_cast<uint64_t>(firstCodepoint) << 32) | static_cast<uint64_t>(secondCodepoint);
        m_KerningTable[key] = amount;
    }

    [[nodiscard]] float GetKerning(uint32_t firstCodepoint, uint32_t secondCodepoint) const noexcept {
        const uint64_t key = (static_cast<uint64_t>(firstCodepoint) << 32) | static_cast<uint64_t>(secondCodepoint);
        auto it = m_KerningTable.find(key);
        return (it != m_KerningTable.end()) ? it->second : 0.0f;
    }

    [[nodiscard]] const std::string& GetFilepath() const noexcept { return m_Filepath; }
    void SetFilepath(std::string_view path) { m_Filepath = path; }

    [[nodiscard]] float GetLineHeight() const noexcept { return m_LineHeight; }
    void SetLineHeight(float lineHeight) noexcept { m_LineHeight = lineHeight; }

    [[nodiscard]] float GetBase() const noexcept { return m_Base; }
    void SetBase(float base) noexcept { m_Base = base; }

    [[nodiscard]] glm::vec2 MeasureText(std::string_view utf8Text, float scale, float maxWrapWidth = 0.0f) const;

    [[nodiscard]] TextLayoutResult ComputeLayout(std::string_view utf8Text,
                                                 const glm::vec2& startPos,
                                                 float scale,
                                                 float maxWrapWidth = 0.0f,
                                                 TextAlignH alignment = TextAlignH::Left) const;

    // Parses AngelCode BMFont text (.fnt) format (`info`, `common`, `page`, `char id=...`, `kerning first=...`)
    static std::shared_ptr<Font> LoadBMFont(const std::string& fntFilepath);
    static std::shared_ptr<Font> LoadBMFontFromMemory(std::string_view fntContent,
                                                      const std::shared_ptr<Texture>& atlasTexture = nullptr);

private:
    std::string m_Filepath;
    float m_LineHeight = 32.0f;
    float m_Base = 26.0f;
    float m_ScaleW = 512.0f;
    float m_ScaleH = 512.0f;

    std::unordered_map<char, Character> m_Characters;
    std::unordered_map<uint32_t, Character> m_ExtendedCharacters;
    std::array<Character, 256> m_AsciiTable{};
    std::array<bool, 256> m_HasAsciiChar{ false };
    std::unordered_map<uint64_t, float> m_KerningTable;
};

} // namespace Engine

#endif // UI_FONT_H
