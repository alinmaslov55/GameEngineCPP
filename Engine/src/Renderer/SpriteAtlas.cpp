#include "Engine/Renderer/SpriteAtlas.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Engine {

SpriteAtlas::SpriteAtlas(const std::string& jsonPath) {
    const auto info = ResourceManager::ResolvePathInfo(jsonPath);
    m_Filepath = info.ResolvedPath;

    std::ifstream file(info.ResolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("SpriteAtlas: Failed to open atlas JSON at '{}' (resolved: '{}')", jsonPath, info.ResolvedPath);
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    ParseJSON(buffer.str(), info.ResolvedPath, nullptr);
}

std::shared_ptr<SpriteAtlas> SpriteAtlas::LoadFromJSON(const std::string& jsonPath) {
    return ResourceManager::Load<SpriteAtlas>(jsonPath);
}

std::shared_ptr<SpriteAtlas> SpriteAtlas::LoadFromJSON(const std::string& jsonPath, const std::shared_ptr<Texture>& customTexture) {
    if (!customTexture) {
        return LoadFromJSON(jsonPath);
    }

    const auto info = ResourceManager::ResolvePathInfo(jsonPath);
    std::ifstream file(info.ResolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("SpriteAtlas: Failed to open atlas JSON at '{}' (resolved: '{}')", jsonPath, info.ResolvedPath);
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto atlas = std::make_shared<SpriteAtlas>();
    atlas->m_Filepath = info.ResolvedPath;
    if (atlas->ParseJSON(buffer.str(), info.ResolvedPath, customTexture)) {
        return atlas;
    }
    return nullptr;
}

std::shared_ptr<SpriteAtlas> SpriteAtlas::CreateFromJSONString(const std::string& jsonString,
                                                             const std::shared_ptr<Texture>& customTexture,
                                                             const std::string& basePath) {
    auto atlas = std::make_shared<SpriteAtlas>();
    atlas->m_Filepath = basePath;
    if (atlas->ParseJSON(jsonString, basePath, customTexture)) {
        return atlas;
    }
    return nullptr;
}

bool SpriteAtlas::ParseJSON(const std::string& jsonString,
                            const std::string& basePath,
                            const std::shared_ptr<Texture>& customTexture) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonString);
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SpriteAtlas: JSON parse error for '{}': {}", basePath, e.what());
        return false;
    }

    if (!root.is_object()) {
        ENGINE_CORE_ERROR("SpriteAtlas: Root JSON value is not an object in '{}'", basePath);
        return false;
    }

    try {
    // 1. Resolve atlas texture
    if (customTexture) {
        m_AtlasTexture = customTexture;
    } else if (root.contains("meta") && root["meta"].contains("image") && root["meta"]["image"].is_string()) {
        const std::string imageRel = root["meta"]["image"].get<std::string>();
        std::filesystem::path imagePath;
        if (!basePath.empty()) {
            imagePath = std::filesystem::path(basePath).parent_path() / imageRel;
        } else {
            imagePath = imageRel;
        }
        m_AtlasTexture = ResourceManager::Load<Texture>(imagePath.string());
    }

    // 2. Resolve texture dimensions (W, H)
    float texWidth = 0.0f;
    float texHeight = 0.0f;

    // Prefer meta.size from json manifest, as it defines the coordinate system of the atlas frames
    // even when the backing texture is an async 1x1 placeholder texture (C-20).
    if (root.contains("meta") && root["meta"].contains("size")) {
        const auto& sizeJson = root["meta"]["size"];
        texWidth = sizeJson.value("w", 0.0f);
        texHeight = sizeJson.value("h", 0.0f);
    }

    if ((texWidth <= 0.0f || texHeight <= 0.0f) && m_AtlasTexture && m_AtlasTexture->GetWidth() > 1 && m_AtlasTexture->GetHeight() > 1) {
        texWidth = static_cast<float>(m_AtlasTexture->GetWidth());
        texHeight = static_cast<float>(m_AtlasTexture->GetHeight());
    }

    if (texWidth <= 0.0f || texHeight <= 0.0f) {
        // Calculate max bounds across all frames as fallback
        if (root.contains("frames")) {
            const auto& framesJson = root["frames"];
            auto inspectFrame = [&](const nlohmann::json& frObj) {
                if (frObj.contains("frame")) {
                    const auto& fr = frObj["frame"];
                    const float x = fr.value("x", 0.0f);
                    const float y = fr.value("y", 0.0f);
                    const float w = fr.value("w", 0.0f);
                    const float h = fr.value("h", 0.0f);
                    texWidth = std::max(texWidth, x + w);
                    texHeight = std::max(texHeight, y + h);
                }
            };

            if (framesJson.is_object()) {
                for (const auto& [name, frObj] : framesJson.items()) {
                    inspectFrame(frObj);
                }
            } else if (framesJson.is_array()) {
                for (const auto& frObj : framesJson) {
                    inspectFrame(frObj);
                }
            }
        }
    }

    if (texWidth <= 0.0f) texWidth = 1024.0f;
    if (texHeight <= 0.0f) texHeight = 1024.0f;

    m_Frames.clear();
    m_FrameNames.clear();

    auto parseSingleFrame = [&](const std::string& frameName, const nlohmann::json& frameData) {
        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        if (frameData.contains("frame")) {
            const auto& fr = frameData["frame"];
            fx = fr.value("x", 0.0f);
            fy = fr.value("y", 0.0f);
            fw = fr.value("w", 0.0f);
            fh = fr.value("h", 0.0f);
        }

        const bool trimmed = frameData.value("trimmed", false);
        const bool rotated = frameData.value("rotated", false);

        glm::vec2 originalSize{ fw, fh };
        if (frameData.contains("sourceSize")) {
            const auto& ss = frameData["sourceSize"];
            originalSize.x = ss.value("w", fw);
            originalSize.y = ss.value("h", fh);
        }

        glm::vec2 trimmedSize{ fw, fh };
        glm::vec2 sourceOffset{ 0.0f, 0.0f };
        if (frameData.contains("spriteSourceSize")) {
            const auto& sss = frameData["spriteSourceSize"];
            trimmedSize.x = sss.value("w", fw);
            trimmedSize.y = sss.value("h", fh);
            sourceOffset.x = sss.value("x", 0.0f);
            sourceOffset.y = sss.value("y", 0.0f);
        }

        glm::vec2 pivot{ 0.5f, 0.0f }; // Default: bottom-center ground contact pivot!
        if (frameData.contains("pivot")) {
            const auto& pv = frameData["pivot"];
            pivot.x = pv.value("x", 0.5f);
            pivot.y = pv.value("y", 0.0f);
        }

        const float durationMs = frameData.value("duration", 100.0f);

        // Convert top-left pixel space to OpenGL bottom-left UV space (accounting for vertical flip)
        const float uMin = fx / texWidth;
        const float uMax = (fx + fw) / texWidth;
        const float vMin = (texHeight - (fy + fh)) / texHeight;
        const float vMax = (texHeight - fy) / texHeight;

        auto subTex = std::make_shared<SubTexture2D>(
            m_AtlasTexture,
            glm::vec2(uMin, vMin),
            glm::vec2(uMax, vMax)
        );

        AtlasFrame frame;
        frame.Name = frameName;
        frame.SubTex = subTex;
        frame.OriginalCanvasSizePx = originalSize;
        frame.TrimmedSizePx = trimmedSize;
        frame.PivotNormalized = pivot;
        frame.SpriteSourceOffsetPx = sourceOffset;
        frame.Trimmed = trimmed;
        frame.Rotated = rotated;
        frame.DurationMs = durationMs;

        m_Frames.emplace(frameName, std::move(frame));
        m_FrameNames.push_back(frameName);
    };

    if (root.contains("frames")) {
        const auto& framesJson = root["frames"];
        if (framesJson.is_object()) {
            // Aseprite / TexturePacker JSON Hash
            for (const auto& [frameName, frameData] : framesJson.items()) {
                parseSingleFrame(frameName, frameData);
            }
        } else if (framesJson.is_array()) {
            // Aseprite / TexturePacker JSON Array
            for (const auto& frameData : framesJson) {
                const std::string frameName = frameData.value("filename", "");
                if (!frameName.empty()) {
                    parseSingleFrame(frameName, frameData);
                }
            }
        }
    }

    std::sort(m_FrameNames.begin(), m_FrameNames.end());
    ENGINE_CORE_INFO("SpriteAtlas: Successfully loaded '{}' ({} frames, texture: {}x{})",
                     basePath, m_Frames.size(), static_cast<int>(texWidth), static_cast<int>(texHeight));
    return true;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SpriteAtlas: Invalid JSON schema in '{}': {}", basePath, e.what());
        m_Frames.clear();
        m_FrameNames.clear();
        return false;
    }
}

const AtlasFrame* SpriteAtlas::GetFrame(std::string_view frameName) const noexcept {
    const auto it = m_Frames.find(frameName);
    if (it != m_Frames.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const AtlasFrame*> SpriteAtlas::GetFramesByPrefix(std::string_view prefix) const {
    std::vector<const AtlasFrame*> result;
    result.reserve(m_FrameNames.size());
    for (const auto& name : m_FrameNames) {
        if (name.starts_with(prefix)) {
            const auto it = m_Frames.find(name);
            if (it != m_Frames.end()) {
                result.push_back(&it->second);
            }
        }
    }
    return result;
}

} // namespace Engine
