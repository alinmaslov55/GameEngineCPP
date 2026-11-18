#include "Engine/Renderer/DDSLoader.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

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

} // namespace

bool DDSLoader::IsDDS(const std::string& filepath) noexcept {
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

bool DDSLoader::IsDDS(const void* data, size_t sizeBytes) noexcept {
    if (!data || sizeBytes < 4) {
        return false;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(uint32_t));
    return magic == DDS_MAGIC;
}

DDSInfo DDSLoader::ParseDDS(const void* data, size_t sizeBytes) {
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

} // namespace Engine
