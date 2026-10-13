#ifndef RENDERER_TEXTURE_H
#define RENDERER_TEXTURE_H

#include <string>
#include <cstdint>

namespace Engine {
class Texture {
public:
    Texture(const std::string& path);
    ~Texture();

    void Bind(uint32_t slot = 0) const;
    void Unbind() const;

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    uint32_t GetRendererID() const { return m_RendererID; }

private:
    uint32_t m_RendererID;
    // BPP = Bits Per Pixel (Channels)
    int m_Width, m_Height, m_BPP;
};
} // namespace Engine

#endif // RENDERER_TEXTURE_H