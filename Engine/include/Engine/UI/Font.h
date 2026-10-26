#ifndef UI_FONT_H
#define UI_FONT_H

#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"
#include <glm/glm.hpp>
#include <unordered_map>
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
        m_Characters[c] = characterData;
    }

    const Character& GetCharacter(char c) const {
        auto it = m_Characters.find(c);
        if (it != m_Characters.end()) {
            return it->second;
        }

        // Fallback to '?' if character is missing
        auto fallback = m_Characters.find('?');
        if (fallback != m_Characters.end()) {
            ENGINE_CORE_WARN("Font: Character '{}' (ASCII {}) not found, falling back to '?'", c, static_cast<int>(c));
            return fallback->second;
        }

        ENGINE_CORE_WARN("Font: Character '{}' (ASCII {}) and fallback '?' not found", c, static_cast<int>(c));
        static const Character s_EmptyCharacter{};
        return s_EmptyCharacter;
    }

    const std::unordered_map<char, Character>& GetCharacters() const { return m_Characters; }

private:
    std::unordered_map<char, Character> m_Characters;
};
} // namespace Engine

#endif // UI_FONT_H
