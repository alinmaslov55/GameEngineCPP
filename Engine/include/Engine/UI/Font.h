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

namespace Engine {
struct Character {
    std::shared_ptr<SubTexture2D> subTexture{nullptr};
    glm::vec2 size{0.0f};       // Width and height of the glyph
    glm::vec2 bearing{0.0f};    // Offset from baseline to left/top of glyph
    float advance{0.0f};        // Horizontal distance to the next character
};

class Font {
public:
    // The single atlas texture containing all characters
    std::shared_ptr<Texture> atlasTexture{nullptr};

    void AddCharacter(char c, const Character& characterData) {
        ENGINE_CORE_DEBUG("Font: Added character '{}' (ASCII {})", c, static_cast<int>(c));
        const auto idx = static_cast<uint8_t>(c);
        m_AsciiTable[idx] = characterData;
        m_HasAsciiChar[idx] = true;
        m_Characters[c] = characterData;
    }

    const Character& GetCharacter(char c) const {
        const auto idx = static_cast<uint8_t>(c);
        if (m_HasAsciiChar[idx]) {
            return m_AsciiTable[idx];
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

private:
    std::unordered_map<char, Character> m_Characters;
    std::array<Character, 256> m_AsciiTable{};
    std::array<bool, 256> m_HasAsciiChar{ false };
};
} // namespace Engine

#endif // UI_FONT_H
