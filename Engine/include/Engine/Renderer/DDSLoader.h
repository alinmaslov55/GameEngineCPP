#ifndef RENDERER_DDSLOADER_H
#define RENDERER_DDSLOADER_H

#include <cstddef>
#include <cstdint>
#include <string>
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

class DDSLoader {
public:
    [[nodiscard]] static bool IsDDS(const std::string& filepath) noexcept;
    [[nodiscard]] static bool IsDDS(const void* data, size_t sizeBytes) noexcept;
    [[nodiscard]] static DDSInfo ParseDDS(const void* data, size_t sizeBytes);
};

} // namespace Engine

#endif // RENDERER_DDSLOADER_H
