#ifndef RENDERER_SPRITEATLAS_H
#define RENDERER_SPRITEATLAS_H

#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include <functional>
#include <glm/vec2.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

struct AtlasFrame {
    std::string Name;
    std::shared_ptr<SubTexture2D> SubTex;
    glm::vec2 OriginalCanvasSizePx{ 1024.0f, 1024.0f };
    glm::vec2 TrimmedSizePx{ 1024.0f, 1024.0f };
    glm::vec2 PivotNormalized{ 0.5f, 0.0f }; // Bottom-center ground contact pivot
    glm::vec2 SpriteSourceOffsetPx{ 0.0f, 0.0f };
    bool Trimmed = false;
    bool Rotated = false;
    float DurationMs = 100.0f;
};

class SpriteAtlas {
public:
    struct TransparentStringHash {
        using is_transparent = void;
        [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
    using FrameMap = std::unordered_map<std::string, AtlasFrame, TransparentStringHash, std::equal_to<>>;

    SpriteAtlas() = default;
    explicit SpriteAtlas(const std::string& jsonPath);
    ~SpriteAtlas() = default;

    static std::shared_ptr<SpriteAtlas> LoadFromJSON(const std::string& jsonPath);
    static std::shared_ptr<SpriteAtlas> LoadFromJSON(const std::string& jsonPath, const std::shared_ptr<Texture>& customTexture);
    static std::shared_ptr<SpriteAtlas> CreateFromJSONString(const std::string& jsonString,
                                                             const std::shared_ptr<Texture>& customTexture = nullptr,
                                                             const std::string& basePath = "");

    [[nodiscard]] const AtlasFrame* GetFrame(std::string_view frameName) const noexcept;
    [[nodiscard]] std::vector<const AtlasFrame*> GetFramesByPrefix(std::string_view prefix) const;
    [[nodiscard]] const std::shared_ptr<Texture>& GetTexture() const noexcept { return m_AtlasTexture; }
    [[nodiscard]] const std::string& GetFilepath() const noexcept { return m_Filepath; }
    [[nodiscard]] size_t GetFrameCount() const noexcept { return m_Frames.size(); }
    [[nodiscard]] const std::vector<std::string>& GetFrameNames() const noexcept { return m_FrameNames; }
    [[nodiscard]] const FrameMap& GetAllFrames() const noexcept { return m_Frames; }

private:
    bool ParseJSON(const std::string& jsonString, const std::string& basePath, const std::shared_ptr<Texture>& customTexture);

    std::string m_Filepath;
    std::shared_ptr<Texture> m_AtlasTexture;
    FrameMap m_Frames;
    std::vector<std::string> m_FrameNames;
};

} // namespace Engine

#endif // RENDERER_SPRITEATLAS_H
