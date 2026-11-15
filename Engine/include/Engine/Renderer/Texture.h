#ifndef RENDERER_TEXTURE_H
#define RENDERER_TEXTURE_H

#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine {

struct CompressedMipLevel {
    int Width = 0;
    int Height = 0;
    uint32_t SizeBytes = 0;
    const uint8_t* Data = nullptr;
};

struct DDSInfo {
    bool Valid = false;
    int Width = 0;
    int Height = 0;
    uint32_t MipCount = 1;
    uint32_t GLFormat = 0;
    uint32_t BytesPerBlock = 16;
    std::vector<CompressedMipLevel> Mips;
};

class Texture {
public:
    using CompressedMipLevel = Engine::CompressedMipLevel;
    using DDSInfo = Engine::DDSInfo;

    explicit Texture(const std::string& path);
    Texture(uint32_t width,
            uint32_t height,
            const void* rgbaData = nullptr,
            bool linearFilter = true,
            bool repeatWrap = false,
            bool hasMipmaps = false);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void Bind(uint32_t slot = 0) const;
    void Unbind() const;

    void SetFilterLinear(bool linear);
    void SetWrapRepeat(bool repeat);
    void SetAnisotropy(float aniso);

    void SetData(const void* rgbaData, uint32_t sizeBytes);
    void SetSubData(uint32_t xOffset,
                    uint32_t yOffset,
                    uint32_t width,
                    uint32_t height,
                    const void* rgbaSubData);
    bool ReloadFromDisk();

    // Async loading support: queries whether image data has been decoded and uploaded to OpenGL
    [[nodiscard]] bool IsLoaded() const noexcept { return m_IsLoaded; }

    // Anisotropic filtering queries
    [[nodiscard]] bool HasMipmaps() const noexcept { return m_HasMipmaps; }
    [[nodiscard]] float GetAnisotropy() const noexcept { return m_Anisotropy; }
    [[nodiscard]] static float GetMaxSupportedAnisotropy() noexcept;

    // Compressed Texture queries
    [[nodiscard]] bool IsCompressed() const noexcept { return m_IsCompressed; }
    [[nodiscard]] uint32_t GetMipCount() const noexcept { return m_MipCount; }
    [[nodiscard]] uint32_t GetCompressedFormat() const noexcept { return m_CompressedGLFormat; }

    [[nodiscard]] static bool IsDDS(const std::string& filepath) noexcept;
    [[nodiscard]] static bool IsDDS(const void* data, size_t sizeBytes) noexcept;
    [[nodiscard]] static DDSInfo ParseDDS(const void* data, size_t sizeBytes);

    // Replaces the texture content in-place with decoded pixel data and updates OpenGL state
    void PopulateFromData(int width, int height, int bpp, const void* rgbaData);

    // Replaces texture content with pre-compressed mip chain data (BC7/BC5/BC4/BC1/BC3)
    void PopulateFromCompressedData(int width, int height, uint32_t glFormat, const std::vector<CompressedMipLevel>& mips);

    // Creates an immediate 1x1 neutral white placeholder texture for async loading
    static std::shared_ptr<Texture> CreatePlaceholder(const std::string& filepath);

    [[nodiscard]] int GetWidth() const noexcept { return m_Width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_Height; }
    [[nodiscard]] bool IsWrapRepeat() const noexcept { return m_RepeatWrap; }
    [[nodiscard]] uint32_t GetRendererID() const noexcept { return m_RendererID; }
    [[nodiscard]] const std::string& GetFilepath() const noexcept { return m_Filepath; }

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
    bool m_IsLoaded = false;
    float m_Anisotropy = 1.0f;
    bool m_IsCompressed = false;
    uint32_t m_MipCount = 1;
    uint32_t m_CompressedGLFormat = 0;
};
} // namespace Engine

#endif // RENDERER_TEXTURE_H
