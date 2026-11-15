#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/GLUtils.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_COMPRESSED_RED_RGTC1
#define GL_COMPRESSED_RED_RGTC1 0x8DBB
#endif
#ifndef GL_COMPRESSED_SIGNED_RED_RGTC1
#define GL_COMPRESSED_SIGNED_RED_RGTC1 0x8DBC
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_SIGNED_RG_RGTC2
#define GL_COMPRESSED_SIGNED_RG_RGTC2 0x8DBE
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif

namespace Engine {

namespace {
bool HasActiveGLContext() {
    return glad_glGenTextures != nullptr && glad_glBindTexture != nullptr;
}

#pragma pack(push, 1)
struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
    uint32_t dwReserved2;
};

struct DDS_HEADER_DXT10 {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "
constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10"
constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"
constexpr uint32_t FOURCC_ATI2 = 0x32495441; // "ATI2"
constexpr uint32_t FOURCC_BC5U = 0x55354342; // "BC5U"
constexpr uint32_t FOURCC_BC4U = 0x55344342; // "BC4U"
constexpr uint32_t FOURCC_ATI1 = 0x31495441; // "ATI1"

void ConfigureAndUploadTexture2D(uint32_t& rendererID,
                                 int width,
                                 int height,
                                 const void* rgbaData,
                                 bool linearFilter,
                                 bool hasMipmaps,
                                 bool repeatWrap,
                                 float& outAnisotropy) {
    if (rendererID == 0) {
        glGenTextures(1, &rendererID);
    }

    glBindTexture(GL_TEXTURE_2D, rendererID);

    const GLint minFilter = linearFilter
        ? (hasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR)
        : GL_NEAREST;
    const GLint magFilter = linearFilter ? GL_LINEAR : GL_NEAREST;
    const GLint wrap = repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    {
        ScopedUnpackAlignment unpackAlign(1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    }
    if (hasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        const float maxSupported = Texture::GetMaxSupportedAnisotropy();
        const float targetAniso = std::min(8.0f, maxSupported);
        if (targetAniso > 1.0f && glad_glTexParameterf != nullptr) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, targetAniso);
            outAnisotropy = targetAniso;
        } else {
            outAnisotropy = 1.0f;
        }
    } else {
        outAnisotropy = 1.0f;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ConfigureAndUploadCompressedTexture2D(uint32_t& rendererID,
                                          int width,
                                          int height,
                                          uint32_t glFormat,
                                          const std::vector<CompressedMipLevel>& mips,
                                          bool repeatWrap,
                                          float& outAnisotropy) {
    (void)width;
    (void)height;
    if (rendererID == 0) {
        glGenTextures(1, &rendererID);
    }

    glBindTexture(GL_TEXTURE_2D, rendererID);

    const bool hasMipmaps = (mips.size() > 1);
    const GLint minFilter = hasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    const GLint magFilter = GL_LINEAR;
    const GLint wrap = repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    for (size_t i = 0; i < mips.size(); ++i) {
        if (glad_glCompressedTexImage2D != nullptr) {
            glCompressedTexImage2D(GL_TEXTURE_2D,
                                   static_cast<GLint>(i),
                                   static_cast<GLenum>(glFormat),
                                   mips[i].Width,
                                   mips[i].Height,
                                   0,
                                   static_cast<GLsizei>(mips[i].SizeBytes),
                                   mips[i].Data);
        }
    }

    if (hasMipmaps) {
        const float maxSupported = Texture::GetMaxSupportedAnisotropy();
        const float targetAniso = std::min(8.0f, maxSupported);
        if (targetAniso > 1.0f && glad_glTexParameterf != nullptr) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, targetAniso);
            outAnisotropy = targetAniso;
        } else {
            outAnisotropy = 1.0f;
        }
    } else {
        outAnisotropy = 1.0f;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
} // namespace

bool Texture::IsDDS(const std::string& filepath) noexcept {
    if (filepath.size() < 4) {
        return false;
    }
    std::string_view sv(filepath);
    if (sv.size() >= 4) {
        std::string_view ext = sv.substr(sv.size() - 4);
        if (ext == ".dds" || ext == ".DDS") {
            return true;
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(filepath, ec)) {
        std::ifstream file(filepath, std::ios::binary);
        if (file) {
            uint32_t magic = 0;
            if (file.read(reinterpret_cast<char*>(&magic), sizeof(magic))) {
                return magic == DDS_MAGIC;
            }
        }
    }
    return false;
}

bool Texture::IsDDS(const void* data, size_t sizeBytes) noexcept {
    if (!data || sizeBytes < 4) {
        return false;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(uint32_t));
    return magic == DDS_MAGIC;
}

DDSInfo Texture::ParseDDS(const void* data, size_t sizeBytes) {
    DDSInfo result{};
    if (!data || sizeBytes < sizeof(uint32_t) + sizeof(DDS_HEADER)) {
        return result;
    }

    const auto* buffer = static_cast<const uint8_t*>(data);
    uint32_t magic = 0;
    std::memcpy(&magic, buffer, sizeof(uint32_t));
    if (magic != DDS_MAGIC) {
        return result;
    }

    DDS_HEADER header{};
    std::memcpy(&header, buffer + sizeof(uint32_t), sizeof(DDS_HEADER));
    if (header.dwSize != 124 || header.dwWidth == 0 || header.dwHeight == 0) {
        return result;
    }

    result.Width = static_cast<int>(header.dwWidth);
    result.Height = static_cast<int>(header.dwHeight);
    result.MipCount = header.dwMipMapCount > 0 ? header.dwMipMapCount : 1;

    size_t offset = sizeof(uint32_t) + sizeof(DDS_HEADER);

    if ((header.ddspf.dwFlags & 0x4 /* DDPF_FOURCC */) != 0) {
        if (header.ddspf.dwFourCC == FOURCC_DX10) {
            if (sizeBytes < offset + sizeof(DDS_HEADER_DXT10)) {
                return result;
            }
            DDS_HEADER_DXT10 dxt10{};
            std::memcpy(&dxt10, buffer + offset, sizeof(DDS_HEADER_DXT10));
            offset += sizeof(DDS_HEADER_DXT10);

            switch (dxt10.dxgiFormat) {
                case 71: // DXGI_FORMAT_BC1_UNORM
                    result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
                    result.BytesPerBlock = 8;
                    break;
                case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
                    result.GLFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
                    result.BytesPerBlock = 8;
                    break;
                case 74: // DXGI_FORMAT_BC2_UNORM
                case 75: // DXGI_FORMAT_BC2_UNORM_SRGB
                    result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
                    result.BytesPerBlock = 16;
                    break;
                case 77: // DXGI_FORMAT_BC3_UNORM
                    result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
                    result.BytesPerBlock = 16;
                    break;
                case 78: // DXGI_FORMAT_BC3_UNORM_SRGB
                    result.GLFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
                    result.BytesPerBlock = 16;
                    break;
                case 80: // DXGI_FORMAT_BC4_UNORM
                    result.GLFormat = GL_COMPRESSED_RED_RGTC1;
                    result.BytesPerBlock = 8;
                    break;
                case 81: // DXGI_FORMAT_BC4_SNORM
                    result.GLFormat = GL_COMPRESSED_SIGNED_RED_RGTC1;
                    result.BytesPerBlock = 8;
                    break;
                case 83: // DXGI_FORMAT_BC5_UNORM
                    result.GLFormat = GL_COMPRESSED_RG_RGTC2;
                    result.BytesPerBlock = 16;
                    break;
                case 84: // DXGI_FORMAT_BC5_SNORM
                    result.GLFormat = GL_COMPRESSED_SIGNED_RG_RGTC2;
                    result.BytesPerBlock = 16;
                    break;
                case 98: // DXGI_FORMAT_BC7_UNORM
                    result.GLFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
                    result.BytesPerBlock = 16;
                    break;
                case 99: // DXGI_FORMAT_BC7_UNORM_SRGB
                    result.GLFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
                    result.BytesPerBlock = 16;
                    break;
                default:
                    ENGINE_CORE_ERROR("Unsupported DXGI format: {}", dxt10.dxgiFormat);
                    return result;
            }
        } else if (header.ddspf.dwFourCC == FOURCC_DXT1) {
            result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            result.BytesPerBlock = 8;
        } else if (header.ddspf.dwFourCC == FOURCC_DXT3) {
            result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
            result.BytesPerBlock = 16;
        } else if (header.ddspf.dwFourCC == FOURCC_DXT5) {
            result.GLFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            result.BytesPerBlock = 16;
        } else if (header.ddspf.dwFourCC == FOURCC_ATI2 || header.ddspf.dwFourCC == FOURCC_BC5U) {
            result.GLFormat = GL_COMPRESSED_RG_RGTC2;
            result.BytesPerBlock = 16;
        } else if (header.ddspf.dwFourCC == FOURCC_ATI1 || header.ddspf.dwFourCC == FOURCC_BC4U) {
            result.GLFormat = GL_COMPRESSED_RED_RGTC1;
            result.BytesPerBlock = 8;
        } else {
            ENGINE_CORE_ERROR("Unsupported DDS FourCC: 0x{:08X}", header.ddspf.dwFourCC);
            return result;
        }
    } else {
        ENGINE_CORE_ERROR("DDS file does not specify DDPF_FOURCC compression");
        return result;
    }

    result.Mips.reserve(result.MipCount);
    int curW = result.Width;
    int curH = result.Height;

    for (uint32_t i = 0; i < result.MipCount; ++i) {
        uint32_t blocksX = static_cast<uint32_t>(std::max(1, (curW + 3) / 4));
        uint32_t blocksY = static_cast<uint32_t>(std::max(1, (curH + 3) / 4));
        uint32_t levelSize = blocksX * blocksY * result.BytesPerBlock;

        if (offset + levelSize > sizeBytes) {
            ENGINE_CORE_ERROR("DDS buffer truncated at mip level {} (needed {} bytes, file remaining {})",
                              i, levelSize, sizeBytes - offset);
            return result;
        }

        result.Mips.push_back({ curW, curH, levelSize, buffer + offset });
        offset += levelSize;
        curW = std::max(1, curW / 2);
        curH = std::max(1, curH / 2);
    }

    result.Valid = true;
    return result;
}

float Texture::GetMaxSupportedAnisotropy() noexcept {
    static float s_MaxAnisotropy = []() {
        if (!HasActiveGLContext()) {
            return 1.0f;
        }
        float maxAniso = 1.0f;
        if (glad_glGetFloatv != nullptr) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        }
        return std::max(1.0f, maxAniso);
    }();
    return s_MaxAnisotropy;
}

Texture::Texture(const std::string& path)
    : m_RendererID(0),
      m_Filepath(path),
      m_Width(0),
      m_Height(0),
      m_BPP(0),
      m_LinearFilter(true),
      m_HasMipmaps(false),
      m_RepeatWrap(false),
      m_IsLoaded(false),
      m_Anisotropy(1.0f),
      m_IsCompressed(false),
      m_MipCount(1),
      m_CompressedGLFormat(0) {

    ENGINE_CORE_INFO("Loading texture from: '{}'", path);

    if (IsDDS(path)) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            ENGINE_CORE_ERROR("Failed to open DDS file: '{}'", path);
            return;
        }
        std::streamsize fileSize = file.tellg();
        if (fileSize < 128) {
            ENGINE_CORE_ERROR("DDS file too small: '{}' ({} bytes)", path, fileSize);
            return;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            ENGINE_CORE_ERROR("Failed to read DDS file: '{}'", path);
            return;
        }

        DDSInfo dds = ParseDDS(buffer.data(), buffer.size());
        if (!dds.Valid) {
            ENGINE_CORE_ERROR("Failed to parse DDS file: '{}'", path);
            return;
        }

        PopulateFromCompressedData(dds.Width, dds.Height, dds.GLFormat, dds.Mips);
        ENGINE_CORE_INFO("Texture (DDS) loaded successfully: '{}' (ID: {}, {}x{}, mips: {}, format: 0x{:X})",
                         path, m_RendererID, m_Width, m_Height, m_MipCount, m_CompressedGLFormat);
        return;
    }

    stbi_set_flip_vertically_on_load_thread(1);

    unsigned char* localBuffer = stbi_load(path.c_str(), &m_Width, &m_Height, &m_BPP, 4);

    if (!localBuffer) {
        ENGINE_CORE_ERROR("Failed to load texture: '{}'", path);
        return;
    }

    m_LinearFilter = (m_Width >= 64 && m_Height >= 64);
    m_HasMipmaps = m_LinearFilter;
    m_RepeatWrap = false;
    m_IsLoaded = true;

    if (!HasActiveGLContext()) {
        stbi_image_free(localBuffer);
        ENGINE_CORE_DEBUG("Texture '{}': Loaded metadata ({}x{}) in headless mode (no GL context)", path, m_Width, m_Height);
        return;
    }

    // Keep small pixel-art atlases (< 64px, e.g. font_atlas.png) crisp with GL_NEAREST,
    // and use GL_LINEAR for high-resolution illustrated 2.5D diorama sprites & ground tiles.
    ConfigureAndUploadTexture2D(m_RendererID, m_Width, m_Height, localBuffer, m_LinearFilter, m_HasMipmaps, m_RepeatWrap, m_Anisotropy);

    stbi_image_free(localBuffer);

    ENGINE_CORE_INFO("Texture loaded successfully: '{}' (ID: {}, {}x{}, BPP: {}, Aniso: {:.1f})",
                     path, m_RendererID, m_Width, m_Height, m_BPP, m_Anisotropy);
}

Texture::Texture(uint32_t width,
                 uint32_t height,
                 const void* rgbaData,
                 bool linearFilter,
                 bool repeatWrap,
                 bool hasMipmaps)
    : m_RendererID(0),
      m_Filepath(""),
      m_Width(static_cast<int>(width)),
      m_Height(static_cast<int>(height)),
      m_BPP(4),
      m_LinearFilter(linearFilter),
      m_HasMipmaps(hasMipmaps),
      m_RepeatWrap(repeatWrap),
      m_IsLoaded(false),
      m_Anisotropy(1.0f),
      m_IsCompressed(false),
      m_MipCount(1),
      m_CompressedGLFormat(0) {
    if (width == 0 || height == 0 || !HasActiveGLContext()) {
        return;
    }

    ConfigureAndUploadTexture2D(m_RendererID, m_Width, m_Height, rgbaData, linearFilter, hasMipmaps, repeatWrap, m_Anisotropy);
    m_IsLoaded = true;
}

Texture::~Texture() {
    if (m_RendererID != 0) {
        ENGINE_CORE_DEBUG("Texture destroyed with ID: {}", m_RendererID);
        if (glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &m_RendererID);
        }
        m_RendererID = 0;
    }
}

Texture::Texture(Texture&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_Filepath(std::move(other.m_Filepath)),
      m_Width(std::exchange(other.m_Width, 0)),
      m_Height(std::exchange(other.m_Height, 0)),
      m_BPP(std::exchange(other.m_BPP, 4)),
      m_LinearFilter(other.m_LinearFilter),
      m_HasMipmaps(other.m_HasMipmaps),
      m_RepeatWrap(other.m_RepeatWrap),
      m_IsLoaded(std::exchange(other.m_IsLoaded, false)),
      m_Anisotropy(other.m_Anisotropy),
      m_IsCompressed(std::exchange(other.m_IsCompressed, false)),
      m_MipCount(std::exchange(other.m_MipCount, 1)),
      m_CompressedGLFormat(std::exchange(other.m_CompressedGLFormat, 0)) {
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Filepath = std::move(other.m_Filepath);
        m_Width = std::exchange(other.m_Width, 0);
        m_Height = std::exchange(other.m_Height, 0);
        m_BPP = std::exchange(other.m_BPP, 4);
        m_LinearFilter = other.m_LinearFilter;
        m_HasMipmaps = other.m_HasMipmaps;
        m_RepeatWrap = other.m_RepeatWrap;
        m_IsLoaded = std::exchange(other.m_IsLoaded, false);
        m_Anisotropy = other.m_Anisotropy;
        m_IsCompressed = std::exchange(other.m_IsCompressed, false);
        m_MipCount = std::exchange(other.m_MipCount, 1);
        m_CompressedGLFormat = std::exchange(other.m_CompressedGLFormat, 0);
    }
    return *this;
}

void Texture::SetFilterLinear(bool linear) {
    m_LinearFilter = linear;
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    const GLint minFilter = linear
        ? (m_HasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR)
        : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    if (m_HasMipmaps) {
        const float maxSupported = GetMaxSupportedAnisotropy();
        const float targetAniso = linear ? std::min(8.0f, maxSupported) : 1.0f;
        if (targetAniso > 1.0f && glad_glTexParameterf != nullptr) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, targetAniso);
        }
        m_Anisotropy = targetAniso;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetAnisotropy(float aniso) {
    if (!m_HasMipmaps) {
        m_Anisotropy = 1.0f;
        return;
    }
    const float maxSupported = GetMaxSupportedAnisotropy();
    m_Anisotropy = std::clamp(aniso, 1.0f, maxSupported);
    if (m_RendererID != 0 && HasActiveGLContext() && glad_glTexParameterf != nullptr) {
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, m_Anisotropy);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Texture::SetWrapRepeat(bool repeat) {
    m_RepeatWrap = repeat;
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    const GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetData(const void* rgbaData, uint32_t sizeBytes) {
    const uint32_t expectedBytes = static_cast<uint32_t>(m_Width) * static_cast<uint32_t>(m_Height) * 4u;
    if (!rgbaData || sizeBytes < expectedBytes) {
        ENGINE_CORE_ERROR("Texture::SetData size mismatch (got {} bytes, expected {} bytes)", sizeBytes, expectedBytes);
        return;
    }
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    {
        ScopedUnpackAlignment unpackAlign(1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    }
    if (m_HasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetSubData(uint32_t xOffset,
                         uint32_t yOffset,
                         uint32_t width,
                         uint32_t height,
                         const void* rgbaSubData) {
    if (!rgbaSubData || width == 0 || height == 0) {
        return;
    }
    if (static_cast<int>(xOffset + width) > m_Width || static_cast<int>(yOffset + height) > m_Height) {
        ENGINE_CORE_ERROR("Texture::SetSubData region ({}, {}, {}x{}) exceeds texture bounds ({}x{})",
                          xOffset, yOffset, width, height, m_Width, m_Height);
        return;
    }
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    {
        ScopedUnpackAlignment unpackAlign(1);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        static_cast<GLint>(xOffset),
                        static_cast<GLint>(yOffset),
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height),
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        rgbaSubData);
    }
    if (m_HasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind(uint32_t slot) const {
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture::Unbind() const {
    if (!HasActiveGLContext()) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Texture::ReloadFromDisk() {
    if (m_Filepath.empty()) {
        return false;
    }

    if (IsDDS(m_Filepath)) {
        std::ifstream file(m_Filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            ENGINE_CORE_ERROR("Texture::ReloadFromDisk failed to open DDS '{}'", m_Filepath);
            return false;
        }
        std::streamsize fileSize = file.tellg();
        if (fileSize < 128) {
            ENGINE_CORE_ERROR("Texture::ReloadFromDisk DDS file too small: '{}'", m_Filepath);
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            return false;
        }
        DDSInfo dds = ParseDDS(buffer.data(), buffer.size());
        if (!dds.Valid) {
            return false;
        }
        PopulateFromCompressedData(dds.Width, dds.Height, dds.GLFormat, dds.Mips);
        return true;
    }

    stbi_set_flip_vertically_on_load_thread(1);
    int newW = 0, newH = 0, newBPP = 0;
    unsigned char* localBuffer = stbi_load(m_Filepath.c_str(), &newW, &newH, &newBPP, 4);
    if (!localBuffer) {
        ENGINE_CORE_ERROR("Texture::ReloadFromDisk failed to load '{}'", m_Filepath);
        return false;
    }

    m_Width = newW;
    m_Height = newH;
    m_BPP = newBPP;
    m_LinearFilter = (m_Width >= 64 && m_Height >= 64);
    m_HasMipmaps = m_LinearFilter;
    m_IsLoaded = true;

    if (!HasActiveGLContext()) {
        stbi_image_free(localBuffer);
        return true;
    }

    ConfigureAndUploadTexture2D(m_RendererID, m_Width, m_Height, localBuffer, m_LinearFilter, m_HasMipmaps, m_RepeatWrap, m_Anisotropy);

    stbi_image_free(localBuffer);
    ENGINE_CORE_INFO("Texture reloaded in-place: '{}' (ID: {}, {}x{}, Aniso: {:.1f})", m_Filepath, m_RendererID, m_Width, m_Height, m_Anisotropy);
    return true;
}

void Texture::PopulateFromCompressedData(int width,
                                        int height,
                                        uint32_t glFormat,
                                        const std::vector<CompressedMipLevel>& mips) {
    if (mips.empty() || width <= 0 || height <= 0) {
        return;
    }

    m_Width = width;
    m_Height = height;
    m_BPP = 4;
    m_MipCount = static_cast<uint32_t>(mips.size());
    m_HasMipmaps = (m_MipCount > 1);
    m_LinearFilter = true;
    m_IsCompressed = true;
    m_CompressedGLFormat = glFormat;
    m_IsLoaded = true;

    if (!HasActiveGLContext()) {
        ENGINE_CORE_DEBUG("Texture '{}': Populated compressed metadata ({}x{}, mips: {}) in headless mode",
                          m_Filepath, m_Width, m_Height, m_MipCount);
        return;
    }

    ConfigureAndUploadCompressedTexture2D(m_RendererID, m_Width, m_Height, glFormat, mips, m_RepeatWrap, m_Anisotropy);
    ENGINE_CORE_INFO("Texture compressed upload complete: '{}' (ID: {}, {}x{}, mips: {}, format: 0x{:X})",
                     m_Filepath, m_RendererID, m_Width, m_Height, m_MipCount, glFormat);
}

void Texture::PopulateFromData(int width, int height, int bpp, const void* rgbaData) {
    if (!rgbaData || width <= 0 || height <= 0) {
        return;
    }

    m_Width = width;
    m_Height = height;
    m_BPP = bpp;
    m_LinearFilter = (m_Width >= 64 && m_Height >= 64);
    m_HasMipmaps = m_LinearFilter;

    if (!HasActiveGLContext()) {
        m_IsLoaded = true;
        ENGINE_CORE_DEBUG("Texture '{}': Populated metadata ({}x{}) in headless mode", m_Filepath, m_Width, m_Height);
        return;
    }

    ConfigureAndUploadTexture2D(m_RendererID, m_Width, m_Height, rgbaData, m_LinearFilter, m_HasMipmaps, m_RepeatWrap, m_Anisotropy);

    m_IsLoaded = true;
    ENGINE_CORE_INFO("Texture async upload complete: '{}' (ID: {}, {}x{}, BPP: {})",
                     m_Filepath, m_RendererID, m_Width, m_Height, m_BPP);
}

std::shared_ptr<Texture> Texture::CreatePlaceholder(const std::string& filepath) {
    static const uint32_t whitePixel = 0xFFFFFFFF;
    auto tex = std::make_shared<Texture>(1u, 1u, &whitePixel, true, false);
    tex->m_Filepath = filepath;
    tex->m_IsLoaded = false;
    return tex;
}
} // namespace Engine
