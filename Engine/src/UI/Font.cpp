#include "Engine/UI/Font.h"
#include "Engine/Core/ResourceManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Engine {

namespace {

inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

inline bool FindKeyValue(std::string_view line, std::string_view key, std::string& outValue) {
    size_t pos = line.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.length();
    if (pos >= line.length()) return false;

    if (line[pos] == '"') {
        pos++;
        size_t end = line.find('"', pos);
        if (end == std::string_view::npos) return false;
        outValue = std::string(line.substr(pos, end - pos));
    } else {
        size_t end = pos;
        while (end < line.length() && !std::isspace(static_cast<unsigned char>(line[end]))) {
            end++;
        }
        outValue = std::string(line.substr(pos, end - pos));
    }
    return true;
}

inline bool FindKeyInt(std::string_view line, std::string_view key, int& outValue) {
    std::string valStr;
    if (FindKeyValue(line, key, valStr)) {
        try {
            outValue = std::stoi(valStr);
            return true;
        } catch (...) {}
    }
    return false;
}

inline bool FindKeyFloat(std::string_view line, std::string_view key, float& outValue) {
    std::string valStr;
    if (FindKeyValue(line, key, valStr)) {
        try {
            outValue = std::stof(valStr);
            return std::isfinite(outValue);
        } catch (...) {}
    }
    return false;
}

inline uint32_t DecodeNextUTF8(std::string_view text, size_t& index) noexcept {
    if (index >= text.size()) {
        return 0;
    }
    const uint8_t b0 = static_cast<uint8_t>(text[index]);
    if (b0 < 0x80u) {
        ++index;
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (b0 >= 0xC2u && index + 1 < text.size()) {
            const uint8_t b1 = static_cast<uint8_t>(text[index + 1]);
            if ((b1 & 0xC0u) == 0x80u) {
                index += 2;
                return (static_cast<uint32_t>(b0 & 0x1Fu) << 6) |
                       static_cast<uint32_t>(b1 & 0x3Fu);
            }
        }
    } else if ((b0 & 0xF0u) == 0xE0u) {
        if (index + 2 < text.size()) {
            const uint8_t b1 = static_cast<uint8_t>(text[index + 1]);
            const uint8_t b2 = static_cast<uint8_t>(text[index + 2]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                const uint32_t cp = (static_cast<uint32_t>(b0 & 0x0Fu) << 12) |
                                    (static_cast<uint32_t>(b1 & 0x3Fu) << 6) |
                                    static_cast<uint32_t>(b2 & 0x3Fu);
                if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                    index += 3;
                    return cp;
                }
            }
        }
    } else if ((b0 & 0xF8u) == 0xF0u) {
        if (b0 <= 0xF4u && index + 3 < text.size()) {
            const uint8_t b1 = static_cast<uint8_t>(text[index + 1]);
            const uint8_t b2 = static_cast<uint8_t>(text[index + 2]);
            const uint8_t b3 = static_cast<uint8_t>(text[index + 3]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                const uint32_t cp = (static_cast<uint32_t>(b0 & 0x07u) << 18) |
                                    (static_cast<uint32_t>(b1 & 0x3Fu) << 12) |
                                    (static_cast<uint32_t>(b2 & 0x3Fu) << 6) |
                                    static_cast<uint32_t>(b3 & 0x3Fu);
                if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                    index += 4;
                    return cp;
                }
            }
        }
    }
    ++index;
    return 0xFFFDu;
}

inline bool IsAsciiSpaceOrTab(char c) noexcept {
    return c == ' ' || c == '\t';
}

} // namespace

std::shared_ptr<Font> Font::LoadBMFont(const std::string& fntFilepath) {
    std::ifstream stream(fntFilepath);
    if (!stream.is_open()) {
        ENGINE_CORE_ERROR("Font::LoadBMFont failed to open file: '{}'", fntFilepath);
        return nullptr;
    }

    std::stringstream buffer;
    buffer << stream.rdbuf();
    const std::string content = buffer.str();

    // Check for page image filename
    std::string pageFile;
    std::istringstream lineStream(content);
    std::string line;
    while (std::getline(lineStream, line)) {
        std::string_view sv = Trim(line);
        if (sv.starts_with("page")) {
            FindKeyValue(sv, "file=", pageFile);
            break;
        }
    }

    std::shared_ptr<Texture> atlas = nullptr;
    if (!pageFile.empty()) {
        std::filesystem::path fntPath(fntFilepath);
        std::filesystem::path texPath = fntPath.parent_path() / pageFile;
        atlas = ResourceManager::Load<Texture>(texPath.string());
    }

    auto font = LoadBMFontFromMemory(content, atlas);
    if (font) {
        font->SetFilepath(fntFilepath);
    }
    return font;
}

std::shared_ptr<Font> Font::LoadBMFontFromMemory(std::string_view fntContent,
                                                 const std::shared_ptr<Texture>& atlasTexture) {
    auto font = std::make_shared<Font>();
    font->atlasTexture = atlasTexture;

    std::istringstream stream{ std::string(fntContent) };
    std::string line;

    while (std::getline(stream, line)) {
        std::string_view sv = Trim(line);
        if (sv.empty()) continue;

        if (sv.starts_with("common")) {
            int lineHeight = 32, base = 26, scaleW = 512, scaleH = 512;
            if (FindKeyInt(sv, "lineHeight=", lineHeight) && lineHeight > 0) font->m_LineHeight = static_cast<float>(lineHeight);
            if (FindKeyInt(sv, "base=", base) && base >= 0)                  font->m_Base = static_cast<float>(base);
            if (FindKeyInt(sv, "scaleW=", scaleW) && scaleW > 0)             font->m_ScaleW = static_cast<float>(scaleW);
            if (FindKeyInt(sv, "scaleH=", scaleH) && scaleH > 0)             font->m_ScaleH = static_cast<float>(scaleH);
        } else if (sv.starts_with("char ")) {
            int id = 0, x = 0, y = 0, width = 0, height = 0, xoffset = 0, yoffset = 0, xadvance = 0;
            if (!FindKeyInt(sv, "id=", id) || id < 0) continue;
            if (!FindKeyInt(sv, "x=", x) || x < 0) continue;
            if (!FindKeyInt(sv, "y=", y) || y < 0) continue;
            if (!FindKeyInt(sv, "width=", width) || width < 0) continue;
            if (!FindKeyInt(sv, "height=", height) || height < 0) continue;
            FindKeyInt(sv, "xoffset=", xoffset);
            FindKeyInt(sv, "yoffset=", yoffset);
            if (!FindKeyInt(sv, "xadvance=", xadvance) || xadvance < 0) continue;

            if (font->m_ScaleW > 0.0f && font->m_ScaleH > 0.0f) {
                const uint64_t xEnd = static_cast<uint64_t>(x) + static_cast<uint64_t>(width);
                const uint64_t yEnd = static_cast<uint64_t>(y) + static_cast<uint64_t>(height);
                if (static_cast<float>(xEnd) > font->m_ScaleW ||
                    static_cast<float>(yEnd) > font->m_ScaleH) {
                    continue;
                }
            }

            Character ch;
            ch.size = { static_cast<float>(width), static_cast<float>(height) };
            ch.bearing = { static_cast<float>(xoffset), font->m_Base - static_cast<float>(yoffset) };
            ch.advance = static_cast<float>(xadvance);

            if (font->m_ScaleW > 0.0f && font->m_ScaleH > 0.0f) {
                const float uMin = static_cast<float>(x) / font->m_ScaleW;
                const float uMax = static_cast<float>(x + width) / font->m_ScaleW;
                const float vMin = 1.0f - static_cast<float>(y + height) / font->m_ScaleH;
                const float vMax = 1.0f - static_cast<float>(y) / font->m_ScaleH;
                ch.subTexture = std::make_shared<SubTexture2D>(font->atlasTexture,
                                                              glm::vec2(uMin, vMin),
                                                              glm::vec2(uMax, vMax));
            }

            font->AddCharacter(static_cast<uint32_t>(id), ch);
        } else if (sv.starts_with("kerning ")) {
            int first = 0, second = 0;
            float amount = 0.0f;
            if (!FindKeyInt(sv, "first=", first) || first < 0) continue;
            if (!FindKeyInt(sv, "second=", second) || second < 0) continue;
            if (!FindKeyFloat(sv, "amount=", amount)) continue;
            font->AddKerning(static_cast<uint32_t>(first), static_cast<uint32_t>(second), amount);
        }
    }

    return font;
}

TextLayoutResult Font::ComputeLayout(std::string_view utf8Text,
                                     const glm::vec2& startPos,
                                     float scale,
                                     float maxWrapWidth,
                                     TextAlignH alignment) const {
    TextLayoutResult result;
    if (utf8Text.empty()) {
        return result;
    }

    struct GlyphItem {
        uint32_t codepoint = 0;
        Character ch;
        float localX = 0.0f; // offset from start of line
    };

    struct Line {
        std::vector<GlyphItem> glyphs;
        float width = 0.0f;
    };

    std::vector<Line> lines;
    Line currentLine;
    float currentCursorX = 0.0f;
    float lastVisibleEndX = 0.0f;
    uint32_t prevCodepoint = 0;

    // Helper to finish current line (trimming trailing whitespace from line width)
    auto finishLine = [&]() {
        currentLine.width = currentLine.glyphs.empty() ? 0.0f : lastVisibleEndX;
        lines.push_back(std::move(currentLine));
        currentLine = Line{};
        currentCursorX = 0.0f;
        lastVisibleEndX = 0.0f;
        prevCodepoint = 0;
    };

    // Helper to measure token width (with UTF-8 decoding and kerning)
    auto measureWord = [&](std::string_view word, uint32_t startPrevCode) -> float {
        float w = 0.0f;
        uint32_t pCode = startPrevCode;
        size_t wi = 0;
        while (wi < word.size()) {
            const uint32_t code = DecodeNextUTF8(word, wi);
            if (pCode != 0) {
                w += GetKerning(pCode, code) * scale;
            }
            w += GetCharacter(code).advance * scale;
            pCode = code;
        }
        return w;
    };

    size_t i = 0;
    while (i < utf8Text.size()) {
        const char c = utf8Text[i];
        if (c == '\r') {
            i++;
            continue;
        }

        if (c == '\n') {
            finishLine();
            i++;
            continue;
        }

        if (IsAsciiSpaceOrTab(c)) {
            const uint32_t code = static_cast<uint32_t>(static_cast<uint8_t>(c));
            if (prevCodepoint != 0) {
                currentCursorX += GetKerning(prevCodepoint, code) * scale;
            }
            const auto& ch = GetCharacter(code);
            currentCursorX += (ch.advance > 0.0f ? ch.advance : 8.0f) * scale;
            prevCodepoint = code;
            i++;
            continue;
        }

        // Extract word (up to next ASCII space/tab/newline)
        const size_t wordStart = i;
        while (i < utf8Text.size() && !IsAsciiSpaceOrTab(utf8Text[i]) && utf8Text[i] != '\n' && utf8Text[i] != '\r') {
            i++;
        }
        const std::string_view word = utf8Text.substr(wordStart, i - wordStart);
        const float wordWidth = measureWord(word, prevCodepoint);

        // Word-wrap check: if adding word exceeds maxWrapWidth, and currentLine has characters
        if (maxWrapWidth > 0.0f && !currentLine.glyphs.empty() && (currentCursorX + wordWidth > maxWrapWidth)) {
            finishLine();
        }

        // Append decoded UTF-8 word glyphs to currentLine
        size_t wi = 0;
        while (wi < word.size()) {
            const uint32_t code = DecodeNextUTF8(word, wi);
            if (prevCodepoint != 0) {
                currentCursorX += GetKerning(prevCodepoint, code) * scale;
            }

            const auto& ch = GetCharacter(code);
            GlyphItem item;
            item.codepoint = code;
            item.ch = ch;
            item.localX = currentCursorX;
            currentLine.glyphs.push_back(item);

            currentCursorX += ch.advance * scale;
            lastVisibleEndX = currentCursorX;
            prevCodepoint = code;
        }
    }

    // Commit any trailing line (trimming trailing whitespace)
    if (!currentLine.glyphs.empty() || lines.empty()) {
        currentLine.width = currentLine.glyphs.empty() ? 0.0f : lastVisibleEndX;
        lines.push_back(std::move(currentLine));
    }

    result.lineCount = static_cast<uint32_t>(lines.size());

    // Calculate maximum line width
    float maxLineWidth = 0.0f;
    for (const auto& lineItem : lines) {
        if (lineItem.width > maxLineWidth) {
            maxLineWidth = lineItem.width;
        }
    }

    const float containerWidth = (maxWrapWidth > 0.0f) ? maxWrapWidth : maxLineWidth;
    const float scaledLineHeight = m_LineHeight * scale;
    result.bounds = { maxLineWidth, static_cast<float>(lines.size()) * scaledLineHeight };

    // Layout each glyph with alignment offset and multi-line baseline
    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const auto& lineItem = lines[lineIdx];

        float xOffset = 0.0f;
        if (alignment == TextAlignH::Center) {
            xOffset = std::max(0.0f, 0.5f * (containerWidth - lineItem.width));
        } else if (alignment == TextAlignH::Right) {
            xOffset = std::max(0.0f, containerWidth - lineItem.width);
        }

        const float baselineY = startPos.y - static_cast<float>(lineIdx) * scaledLineHeight;

        for (const auto& g : lineItem.glyphs) {
            GlyphLayout layout;
            layout.codepoint = g.codepoint;
            layout.size = g.ch.size * scale;
            layout.subTexture = g.ch.subTexture;

            const float glyphX = startPos.x + xOffset + g.localX + (g.ch.bearing.x * scale);
            const float glyphY = baselineY - ((g.ch.size.y - g.ch.bearing.y) * scale);
            layout.position = { glyphX, glyphY };

            result.glyphs.push_back(layout);
        }
    }

    return result;
}

glm::vec2 Font::MeasureText(std::string_view utf8Text, float scale, float maxWrapWidth) const {
    auto layout = ComputeLayout(utf8Text, glm::vec2(0.0f), scale, maxWrapWidth, TextAlignH::Left);
    return layout.bounds;
}

} // namespace Engine
