#ifndef RENDERER_TEXTURE_H
#define RENDERER_TEXTURE_H

#include <string>
#include <cstdint>

namespace Engine {
class Texture {
public:
    explicit Texture(const std::string& path);
    Texture(uint32_t width,
            uint32_t height,
            const void* rgbaData = nullptr,
            bool linearFilter = true,
            bool repeatWrap = false);
    ~Texture();

    void Bind(uint32_t slot = 0) const;
    void Unbind() const;

    void SetFilterLinear(bool linear);
    void SetWrapRepeat(bool repeat);

    void SetData(const void* rgbaData, uint32_t sizeBytes);
    void SetSubData(uint32_t xOffset,
                    uint32_t yOffset,
                    uint32_t width,
                    uint32_t height,
                    const void* rgbaSubData);
    bool ReloadFromDisk();

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    bool IsWrapRepeat() const { return m_RepeatWrap; }
    uint32_t GetRendererID() const { return m_RendererID; }
    const std::string& GetFilepath() const { return m_Filepath; }

private:
    uint32_t m_RendererID = 0;
    std::string m_Filepath;
    // BPP = Channels in source image
    int m_Width = 0;
    int m_Height = 0;
    int m_BPP = 4;
    bool m_LinearFilter = true;
    bool m_HasMipmaps = false;
    bool m_RepeatWrap = false;
};
} // namespace Engine

#endif // RENDERER_TEXTURE_H
