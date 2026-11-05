#include <gtest/gtest.h>
#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Utils/CutoutProcessor.h"
#include <stb_image_write.h>
#include <filesystem>
#include <vector>
#include <atomic>
#include <thread>

namespace {

void WriteTestPng(const std::filesystem::path& path, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    stbi_flip_vertically_on_write(0);
    stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(), static_cast<int>(width * 4u));
}

TEST(AsyncTextureLoadingTest, ReturnsPlaceholderImmediatelyAndCompletesInBackground) {
    Engine::JobSystem::Shutdown();
    Engine::JobSystem::Init(4);
    EXPECT_TRUE(Engine::JobSystem::IsInitialized());

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_async_tex_test_1";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path testPng = tempDir / "async_test_32x32.png";
    WriteTestPng(testPng, 32, 32, 200, 100, 50, 255);

    // 1. Immediately returns a valid 1x1 placeholder texture before background decode finishes
    auto tex = Engine::ResourceManager::LoadTextureAsync(testPng.string());
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->GetFilepath(), Engine::ResourceManager::ResolvePath(testPng.string()));

    // 2. Wait for background worker threads to decode and flush to OpenGL / metadata
    Engine::ResourceManager::WaitForPendingUploads();
    EXPECT_EQ(Engine::ResourceManager::GetPendingUploadCount(), 0u);

    // 3. Texture should now be fully populated with real dimensions
    EXPECT_TRUE(tex->IsLoaded());
    EXPECT_EQ(tex->GetWidth(), 32);
    EXPECT_EQ(tex->GetHeight(), 32);

    Engine::ResourceManager::ClearType<Engine::Texture>();
    std::filesystem::remove_all(tempDir, ec);
    Engine::JobSystem::Shutdown();
}

TEST(AsyncTextureLoadingTest, ConcurrentDuplicateRequestsReturnSamePlaceholder) {
    Engine::JobSystem::Shutdown();
    Engine::JobSystem::Init(4);

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_async_tex_test_2";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path testPng = tempDir / "duplicate_test.png";
    WriteTestPng(testPng, 64, 64, 40, 140, 240, 255);

    // Duplicate concurrent async loads must return the exact same shared_ptr placeholder
    auto tex1 = Engine::ResourceManager::LoadTextureAsync(testPng.string());
    auto tex2 = Engine::ResourceManager::LoadTextureAsync(testPng.string());
    EXPECT_EQ(tex1, tex2);

    Engine::ResourceManager::WaitForPendingUploads();
    EXPECT_TRUE(tex1->IsLoaded());
    EXPECT_TRUE(tex2->IsLoaded());
    EXPECT_EQ(tex1->GetWidth(), 64);
    EXPECT_EQ(tex2->GetWidth(), 64);

    Engine::ResourceManager::ClearType<Engine::Texture>();
    std::filesystem::remove_all(tempDir, ec);
    Engine::JobSystem::Shutdown();
}

TEST(AsyncTextureLoadingTest, IncrementalUploadBatchingBoundsFrameTime) {
    Engine::JobSystem::Shutdown();
    Engine::JobSystem::Init(4);

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_async_tex_test_3";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);

    const std::filesystem::path pngA = tempDir / "texA.png";
    const std::filesystem::path pngB = tempDir / "texB.png";
    const std::filesystem::path pngC = tempDir / "texC.png";
    WriteTestPng(pngA, 16, 16, 10, 20, 30);
    WriteTestPng(pngB, 16, 16, 40, 50, 60);
    WriteTestPng(pngC, 16, 16, 70, 80, 90);

    auto texA = Engine::ResourceManager::LoadTextureAsync(pngA.string());
    auto texB = Engine::ResourceManager::LoadTextureAsync(pngB.string());
    auto texC = Engine::ResourceManager::LoadTextureAsync(pngC.string());

    // Wait until background decoding jobs have placed their buffers in the pending upload queue
    while (Engine::ResourceManager::GetPendingUploadCount() > 0) {
        if (!Engine::JobSystem::ExecuteOneJob()) {
            std::this_thread::yield();
        }
    }

    // Process exactly 1 upload per frame tick to bound frame time
    size_t processedFirst = Engine::ResourceManager::ProcessPendingUploads(1);
    EXPECT_EQ(processedFirst, 1u);

    // Process remaining uploads (maxUploads = 0 flushes all available)
    size_t processedRemaining = Engine::ResourceManager::ProcessPendingUploads(0);
    EXPECT_EQ(processedRemaining, 2u);

    EXPECT_TRUE(texA->IsLoaded());
    EXPECT_TRUE(texB->IsLoaded());
    EXPECT_TRUE(texC->IsLoaded());

    Engine::ResourceManager::ClearType<Engine::Texture>();
    std::filesystem::remove_all(tempDir, ec);
    Engine::JobSystem::Shutdown();
}

TEST(CutoutProcessorAsyncTest, ProcessFileAsyncExecutesInBackgroundAndInvokesCallback) {
    Engine::JobSystem::Shutdown();
    Engine::JobSystem::Init(4);

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_cutout_async_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);

    const std::filesystem::path inPng = tempDir / "raw_banner_green.png";
    const std::filesystem::path outPng = tempDir / "banner_cutout.png";

    // 24x24 image with chroma green background and crimson rectangle
    const uint32_t w = 24u, h = 24u;
    std::vector<uint8_t> greenImg(static_cast<size_t>(w) * h * 4u, 0u);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            greenImg[idx + 0] = 0u;
            greenImg[idx + 1] = 255u; // Chroma green
            greenImg[idx + 2] = 0u;
            greenImg[idx + 3] = 255u;
        }
    }
    for (uint32_t y = 4; y <= 17; ++y) {
        for (uint32_t x = 8; x <= 15; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            greenImg[idx + 0] = 180u;
            greenImg[idx + 1] = 20u;
            greenImg[idx + 2] = 30u;
            greenImg[idx + 3] = 255u;
        }
    }

    stbi_flip_vertically_on_write(0);
    ASSERT_NE(stbi_write_png(inPng.string().c_str(), static_cast<int>(w), static_cast<int>(h), 4, greenImg.data(), static_cast<int>(w * 4u)), 0);

    Engine::CutoutProcessSettings settings;
    settings.BackgroundMode = Engine::CutoutKeyMode::ChromaGreen;
    settings.AutoTrimAndBottomFlush = true;

    std::atomic<bool> callbackFired{ false };
    Engine::CutoutProcessResult capturedResult;

    Engine::JobHandle handle = Engine::CutoutProcessor::ProcessFileAsync(
        inPng, outPng, settings, [&](const Engine::CutoutProcessResult& res) {
            capturedResult = res;
            callbackFired.store(true, std::memory_order_release);
        });

    EXPECT_TRUE(handle.IsValid());
    handle.Wait();

    EXPECT_TRUE(callbackFired.load(std::memory_order_acquire));
    EXPECT_TRUE(capturedResult.Success);
    EXPECT_TRUE(std::filesystem::exists(outPng, ec));
    EXPECT_GT(capturedResult.OutputWidth, 0u);
    EXPECT_GT(capturedResult.OutputHeight, 0u);

    std::filesystem::remove_all(tempDir, ec);
    Engine::JobSystem::Shutdown();
}

} // namespace

