#include <gtest/gtest.h>
#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Renderer/Texture.h"
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif
#ifndef GL_COMPRESSED_RED_RGTC1
#define GL_COMPRESSED_RED_RGTC1 0x8DBB
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace {

std::vector<uint8_t> CreateSyntheticDDS(int width,
                                        int height,
                                        uint32_t mipCount,
                                        uint32_t dxgiFormat,
                                        uint32_t legacyFourCC = 0) {
    const bool useDX10 = (legacyFourCC == 0);
    const size_t headerSize = 4 + 124 + (useDX10 ? 20 : 0);

    uint32_t bytesPerBlock = 16;
    if (useDX10) {
        if (dxgiFormat == 71 || dxgiFormat == 72 || dxgiFormat == 80 || dxgiFormat == 81) {
            bytesPerBlock = 8;
        }
    } else {
        if (legacyFourCC == 0x31545844 /* DXT1 */ || legacyFourCC == 0x55344342 /* BC4U */ || legacyFourCC == 0x31495441 /* ATI1 */) {
            bytesPerBlock = 8;
        }
    }

    size_t totalDataSize = 0;
    int curW = width;
    int curH = height;
    for (uint32_t i = 0; i < mipCount; ++i) {
        const uint32_t blocksX = static_cast<uint32_t>(std::max(1, (curW + 3) / 4));
        const uint32_t blocksY = static_cast<uint32_t>(std::max(1, (curH + 3) / 4));
        totalDataSize += blocksX * blocksY * bytesPerBlock;
        curW = std::max(1, curW / 2);
        curH = std::max(1, curH / 2);
    }

    std::vector<uint8_t> buffer(headerSize + totalDataSize, 0xAB);

    // 1. Magic
    const uint32_t magic = 0x20534444; // "DDS "
    std::memcpy(buffer.data(), &magic, 4);

    // 2. DDS_HEADER (124 bytes)
    const uint32_t dwSize = 124;
    std::memcpy(buffer.data() + 4, &dwSize, 4);

    // dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT
    const uint32_t dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
    std::memcpy(buffer.data() + 8, &dwFlags, 4);

    const uint32_t h = static_cast<uint32_t>(height);
    std::memcpy(buffer.data() + 12, &h, 4);

    const uint32_t w = static_cast<uint32_t>(width);
    std::memcpy(buffer.data() + 16, &w, 4);

    const uint32_t pitch = static_cast<uint32_t>(totalDataSize);
    std::memcpy(buffer.data() + 20, &pitch, 4);

    std::memcpy(buffer.data() + 28, &mipCount, 4);

    // ddspf at offset 4 + 72 = 76
    const uint32_t spfSize = 32;
    std::memcpy(buffer.data() + 76, &spfSize, 4);
    const uint32_t spfFlags = 0x4; // DDPF_FOURCC
    std::memcpy(buffer.data() + 80, &spfFlags, 4);
    const uint32_t fourCC = useDX10 ? 0x30315844 /* "DX10" */ : legacyFourCC;
    std::memcpy(buffer.data() + 84, &fourCC, 4);

    if (useDX10) {
        // DDS_HEADER_DXT10 at offset 128
        std::memcpy(buffer.data() + 128, &dxgiFormat, 4);
        const uint32_t dim = 3; // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        std::memcpy(buffer.data() + 132, &dim, 4);
        const uint32_t arraySize = 1;
        std::memcpy(buffer.data() + 140, &arraySize, 4);
    }

    return buffer;
}

void WriteTestPng(const std::filesystem::path& path, uint32_t width, uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u, 200u);
    stbi_flip_vertically_on_write(0);
    stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(), static_cast<int>(width * 4u));
}

TEST(CompressedTextureTest, ParseSyntheticBC7DDS) {
    // 16x16 with 3 mips (16x16, 8x8, 4x4)
    auto buffer = CreateSyntheticDDS(16, 16, 3, 98 /* BC7_UNORM */);
    EXPECT_GT(buffer.size(), 148u);

    Engine::DDSInfo dds = Engine::Texture::ParseDDS(buffer.data(), buffer.size());
    EXPECT_TRUE(dds.Valid);
    EXPECT_EQ(dds.Width, 16);
    EXPECT_EQ(dds.Height, 16);
    EXPECT_EQ(dds.MipCount, 3u);
    EXPECT_EQ(dds.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RGBA_BPTC_UNORM));
    EXPECT_EQ(dds.BytesPerBlock, 16u);

    ASSERT_EQ(dds.Mips.size(), 3u);
    // Level 0: 16x16 -> (4x4) blocks * 16 bytes = 256 bytes
    EXPECT_EQ(dds.Mips[0].Width, 16);
    EXPECT_EQ(dds.Mips[0].Height, 16);
    EXPECT_EQ(dds.Mips[0].SizeBytes, 256u);
    // Level 1: 8x8 -> (2x2) blocks * 16 bytes = 64 bytes
    EXPECT_EQ(dds.Mips[1].Width, 8);
    EXPECT_EQ(dds.Mips[1].Height, 8);
    EXPECT_EQ(dds.Mips[1].SizeBytes, 64u);
    // Level 2: 4x4 -> (1x1) block * 16 bytes = 16 bytes
    EXPECT_EQ(dds.Mips[2].Width, 4);
    EXPECT_EQ(dds.Mips[2].Height, 4);
    EXPECT_EQ(dds.Mips[2].SizeBytes, 16u);
}

TEST(CompressedTextureTest, HeadlessTextureLoadingBC7) {
    const auto tempDir = std::filesystem::temp_directory_path() / "engine_test_bc7";
    std::filesystem::create_directories(tempDir);
    const auto ddsPath = tempDir / "hero_standee.dds";

    auto buffer = CreateSyntheticDDS(16, 16, 3, 98 /* BC7_UNORM */);
    {
        std::ofstream out(ddsPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    EXPECT_TRUE(Engine::Texture::IsDDS(ddsPath.string()));

    Engine::Texture texture(ddsPath.string());
    EXPECT_TRUE(texture.IsLoaded());
    EXPECT_TRUE(texture.IsCompressed());
    EXPECT_EQ(texture.GetWidth(), 16);
    EXPECT_EQ(texture.GetHeight(), 16);
    EXPECT_EQ(texture.GetMipCount(), 3u);
    EXPECT_TRUE(texture.HasMipmaps());
    EXPECT_EQ(texture.GetCompressedFormat(), static_cast<uint32_t>(GL_COMPRESSED_RGBA_BPTC_UNORM));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST(CompressedTextureTest, ParseBC5NormalMap) {
    // BC5 tangent normal map (dxgiFormat = 83)
    auto buffer = CreateSyntheticDDS(32, 32, 1, 83 /* BC5_UNORM */);
    Engine::DDSInfo dds = Engine::Texture::ParseDDS(buffer.data(), buffer.size());
    EXPECT_TRUE(dds.Valid);
    EXPECT_EQ(dds.Width, 32);
    EXPECT_EQ(dds.Height, 32);
    EXPECT_EQ(dds.MipCount, 1u);
    EXPECT_EQ(dds.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RG_RGTC2));
    EXPECT_EQ(dds.BytesPerBlock, 16u);
    ASSERT_EQ(dds.Mips.size(), 1u);
    // 32x32 -> (8x8) blocks * 16 bytes = 1024 bytes
    EXPECT_EQ(dds.Mips[0].SizeBytes, 1024u);
}

TEST(CompressedTextureTest, ParseBC4SingleChannel) {
    // BC4 height/roughness map (dxgiFormat = 80, 8 bytes per block)
    auto buffer = CreateSyntheticDDS(32, 32, 1, 80 /* BC4_UNORM */);
    Engine::DDSInfo dds = Engine::Texture::ParseDDS(buffer.data(), buffer.size());
    EXPECT_TRUE(dds.Valid);
    EXPECT_EQ(dds.Width, 32);
    EXPECT_EQ(dds.Height, 32);
    EXPECT_EQ(dds.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RED_RGTC1));
    EXPECT_EQ(dds.BytesPerBlock, 8u);
    ASSERT_EQ(dds.Mips.size(), 1u);
    // 32x32 -> (8x8) blocks * 8 bytes = 512 bytes
    EXPECT_EQ(dds.Mips[0].SizeBytes, 512u);
}

TEST(CompressedTextureTest, ParseLegacyFourCC) {
    // DXT1 (0x31545844)
    auto bufDXT1 = CreateSyntheticDDS(16, 16, 1, 0, 0x31545844);
    auto ddsDXT1 = Engine::Texture::ParseDDS(bufDXT1.data(), bufDXT1.size());
    EXPECT_TRUE(ddsDXT1.Valid);
    EXPECT_EQ(ddsDXT1.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT));
    EXPECT_EQ(ddsDXT1.BytesPerBlock, 8u);

    // DXT5 (0x35545844)
    auto bufDXT5 = CreateSyntheticDDS(16, 16, 1, 0, 0x35545844);
    auto ddsDXT5 = Engine::Texture::ParseDDS(bufDXT5.data(), bufDXT5.size());
    EXPECT_TRUE(ddsDXT5.Valid);
    EXPECT_EQ(ddsDXT5.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT));
    EXPECT_EQ(ddsDXT5.BytesPerBlock, 16u);

    // ATI2 (0x32495441)
    auto bufATI2 = CreateSyntheticDDS(16, 16, 1, 0, 0x32495441);
    auto ddsATI2 = Engine::Texture::ParseDDS(bufATI2.data(), bufATI2.size());
    EXPECT_TRUE(ddsATI2.Valid);
    EXPECT_EQ(ddsATI2.GLFormat, static_cast<uint32_t>(GL_COMPRESSED_RG_RGTC2));
    EXPECT_EQ(ddsATI2.BytesPerBlock, 16u);
}

TEST(CompressedTextureTest, CorruptedAndTruncatedDDSRejection) {
    // 1. Truncated buffer (< 128 bytes)
    std::vector<uint8_t> shortBuf(64, 0);
    EXPECT_FALSE(Engine::Texture::ParseDDS(shortBuf.data(), shortBuf.size()).Valid);

    // 2. Corrupt magic
    auto validBuf = CreateSyntheticDDS(16, 16, 1, 98);
    validBuf[0] = 'X';
    EXPECT_FALSE(Engine::Texture::ParseDDS(validBuf.data(), validBuf.size()).Valid);

    // 3. Header size mismatch
    validBuf = CreateSyntheticDDS(16, 16, 1, 98);
    uint32_t badSize = 100;
    std::memcpy(validBuf.data() + 4, &badSize, 4);
    EXPECT_FALSE(Engine::Texture::ParseDDS(validBuf.data(), validBuf.size()).Valid);

    // 4. Truncated mip payload
    validBuf = CreateSyntheticDDS(16, 16, 1, 98);
    validBuf.resize(148 + 10); // Less than the needed 256 bytes
    EXPECT_FALSE(Engine::Texture::ParseDDS(validBuf.data(), validBuf.size()).Valid);

    // 5. Instantiating a Texture with a corrupted file fails gracefully without crash
    const auto tempDir = std::filesystem::temp_directory_path() / "engine_test_corrupt_dds";
    std::filesystem::create_directories(tempDir);
    const auto badDdsPath = tempDir / "corrupted.dds";
    {
        std::ofstream out(badDdsPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(validBuf.data()), validBuf.size());
    }

    Engine::Texture badTex(badDdsPath.string());
    EXPECT_FALSE(badTex.IsLoaded());

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST(CompressedTextureTest, ResourceManagerSiblingResolution) {
    const auto tempDir = std::filesystem::temp_directory_path() / "engine_test_sibling_res";
    std::filesystem::create_directories(tempDir);

    const auto pngPath = tempDir / "atlas_character.png";
    const auto ddsPath = tempDir / "atlas_character.dds";

    WriteTestPng(pngPath, 16, 16);
    auto ddsBuf = CreateSyntheticDDS(16, 16, 1, 98 /* BC7_UNORM */);
    {
        std::ofstream out(ddsPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(ddsBuf.data()), ddsBuf.size());
    }

    // Ensure dds has mtime >= png
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto nowTime = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(pngPath, nowTime - std::chrono::seconds(10));
    std::filesystem::last_write_time(ddsPath, nowTime);

    Engine::ResourceManager::ClearPathCache();
    Engine::ResourceManager::ClearType<Engine::Texture>();

    // Requesting png should resolve and load the sibling DDS!
    auto loadedTex = Engine::ResourceManager::LoadTexture(pngPath.string());
    ASSERT_NE(loadedTex, nullptr);
    EXPECT_TRUE(loadedTex->IsLoaded());
    EXPECT_TRUE(loadedTex->IsCompressed());
    EXPECT_EQ(loadedTex->GetCompressedFormat(), static_cast<uint32_t>(GL_COMPRESSED_RGBA_BPTC_UNORM));

    // Now update PNG so png mtime > dds (artist hot iteration)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::filesystem::last_write_time(pngPath, std::filesystem::file_time_type::clock::now());

    Engine::ResourceManager::ClearPathCache();
    Engine::ResourceManager::ClearType<Engine::Texture>();

    auto iteratedTex = Engine::ResourceManager::LoadTexture(pngPath.string());
    ASSERT_NE(iteratedTex, nullptr);
    EXPECT_TRUE(iteratedTex->IsLoaded());
    // Should now be uncompressed PNG!
    EXPECT_FALSE(iteratedTex->IsCompressed());

    // Now delete PNG, leaving only DDS sibling
    std::error_code ec;
    std::filesystem::remove(pngPath, ec);

    Engine::ResourceManager::ClearPathCache();
    Engine::ResourceManager::ClearType<Engine::Texture>();

    auto fallbackDdsTex = Engine::ResourceManager::LoadTexture(pngPath.string());
    ASSERT_NE(fallbackDdsTex, nullptr);
    EXPECT_TRUE(fallbackDdsTex->IsLoaded());
    EXPECT_TRUE(fallbackDdsTex->IsCompressed());

    std::filesystem::remove_all(tempDir, ec);
}

TEST(CompressedTextureTest, AsyncDDSLoading) {
    if (!Engine::JobSystem::IsInitialized() || Engine::JobSystem::GetWorkerCount() == 0) {
        Engine::JobSystem::Shutdown();
        Engine::JobSystem::Init(4);
    }

    const auto tempDir = std::filesystem::temp_directory_path() / "engine_test_async_dds";
    std::filesystem::create_directories(tempDir);
    const auto ddsPath = tempDir / "async_terrain_bc7.dds";

    auto ddsBuf = CreateSyntheticDDS(16, 16, 2, 98 /* BC7_UNORM */);
    {
        std::ofstream out(ddsPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(ddsBuf.data()), ddsBuf.size());
    }

    Engine::ResourceManager::ClearPathCache();
    Engine::ResourceManager::ClearType<Engine::Texture>();

    auto placeholder = Engine::ResourceManager::LoadTextureAsync(ddsPath.string());
    ASSERT_NE(placeholder, nullptr);

    Engine::ResourceManager::WaitForPendingUploads();

    EXPECT_TRUE(placeholder->IsLoaded());
    EXPECT_TRUE(placeholder->IsCompressed());
    EXPECT_EQ(placeholder->GetWidth(), 16);
    EXPECT_EQ(placeholder->GetHeight(), 16);
    EXPECT_EQ(placeholder->GetMipCount(), 2u);
    EXPECT_EQ(placeholder->GetCompressedFormat(), static_cast<uint32_t>(GL_COMPRESSED_RGBA_BPTC_UNORM));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

} // namespace
