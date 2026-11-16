#include "Engine/UI/Font.h"
#include "Engine/Core/ResourceManager.h"
#include <algorithm>
#include <cctype>
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
            return true;
        } catch (...) {}
    }
    return false;
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
            if (FindKeyInt(sv, "lineHeight=", lineHeight)) font->m_LineHeight = static_cast<float>(lineHeight);
            if (FindKeyInt(sv, "base=", base))             font->m_Base = static_cast<float>(base);
            if (FindKeyInt(sv, "scaleW=", scaleW))         font->m_ScaleW = static_cast<float>(scaleW);
            if (FindKeyInt(sv, "scaleH=", scaleH))         font->m_ScaleH = static_cast<float>(scaleH);
        } else if (sv.starts_with("char ")) {
            int id = 0, x = 0, y = 0, width = 0, height = 0, xoffset = 0, yoffset = 0, xadvance = 0;
            FindKeyInt(sv, "id=", id);
            FindKeyInt(sv, "x=", x);
            FindKeyInt(sv, "y=", y);
            FindKeyInt(sv, "width=", width);
            FindKeyInt(sv, "height=", height);
            FindKeyInt(sv, "xoffset=", xoffset);
            FindKeyInt(sv, "yoffset=", yoffset);
            FindKeyInt(sv, "xadvance=", xadvance);

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
            FindKeyInt(sv, "first=", first);
            FindKeyInt(sv, "second=", second);
            FindKeyFloat(sv, "amount=", amount);
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
    uint32_t prevCodepoint = 0;

    // Helper to finish current line
    auto finishLine = [&]() {
        currentLine.width = currentCursorX;
        lines.push_back(std::move(currentLine));
        currentLine = Line{};
        currentCursorX = 0.0f;
        prevCodepoint = 0;
    };

    // Helper to measure token width (with kerning)
    auto measureWord = [&](std::string_view word, uint32_t startPrevCode) -> float {
        float w = 0.0f;
        uint32_t pCode = startPrevCode;
        for (char c : word) {
            uint32_t code = static_cast<uint32_t>(static_cast<uint8_t>(c));
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
        char c = utf8Text[i];
        if (c == '\r') {
            i++;
            continue;
        }

        if (c == '\n') {
            finishLine();
            i++;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            uint32_t code = static_cast<uint32_t>(static_cast<uint8_t>(c));
            if (prevCodepoint != 0) {
                currentCursorX += GetKerning(prevCodepoint, code) * scale;
            }
            const auto& ch = GetCharacter(code);
            currentCursorX += (ch.advance > 0.0f ? ch.advance : 8.0f) * scale;
            prevCodepoint = code;
            i++;
            continue;
        }

        // Extract word
        size_t wordStart = i;
        while (i < utf8Text.size() && !std::isspace(static_cast<unsigned char>(utf8Text[i])) && utf8Text[i] != '\n' && utf8Text[i] != '\r') {
            i++;
        }
        std::string_view word = utf8Text.substr(wordStart, i - wordStart);
        float wordWidth = measureWord(word, prevCodepoint);

        // Word-wrap check: if adding word exceeds maxWrapWidth, and currentLine has characters
        if (maxWrapWidth > 0.0f && !currentLine.glyphs.empty() && (currentCursorX + wordWidth > maxWrapWidth)) {
            finishLine();
        }

        // Append word glyphs to currentLine
        for (char wc : word) {
            uint32_t code = static_cast<uint32_t>(static_cast<uint8_t>(wc));
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
            prevCodepoint = code;
        }
    }

    // Commit any trailing line
    if (!currentLine.glyphs.empty() || lines.empty()) {
        currentLine.width = currentCursorX;
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
