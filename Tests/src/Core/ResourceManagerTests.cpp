#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Texture.h"
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

struct DummyAsset {
    std::string loadedPath;
    static inline int constructCount = 0;

    explicit DummyAsset(const std::string& path) : loadedPath(path) {
        ++constructCount;
    }
};

TEST(ResourceManagerTest, PathResolutionAndNormalization) {
    const std::filesystem::path prevRoot = Engine::ResourceManager::GetAssetRoot();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "res_mgr_path_test";
    std::filesystem::create_directories(tempRoot / "textures");
    Engine::ResourceManager::SetAssetRoot(tempRoot);

    const std::string relative = "textures/grass.png";
    const std::string resolved = Engine::ResourceManager::ResolvePath(relative);
    EXPECT_FALSE(resolved.empty());
    EXPECT_EQ(Engine::ResourceManager::ToRelativePath(resolved), relative);

    // If given an already relative path that is not absolute, ToRelativePath handles it cleanly
    EXPECT_EQ(Engine::ResourceManager::ToRelativePath(relative), relative);

    if (!prevRoot.empty()) {
        Engine::ResourceManager::SetAssetRoot(prevRoot);
    }
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(ResourceManagerTest, CachesAssetsAcrossRelativeAndResolvedPaths) {
    Engine::ResourceManager::ClearType<DummyAsset>();
    DummyAsset::constructCount = 0;

    const std::filesystem::path prevRoot = Engine::ResourceManager::GetAssetRoot();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "engine_test_assets";
    std::filesystem::create_directories(tempRoot / "textures");
    Engine::ResourceManager::SetAssetRoot(tempRoot);

    const std::string relativeKey = "textures/hero.png";
    const std::string resolvedFull = Engine::ResourceManager::ResolvePath(relativeKey);
    EXPECT_EQ(Engine::ResourceManager::ToRelativePath(resolvedFull), relativeKey);

    // First load constructs the asset once
    auto first = Engine::ResourceManager::Load<DummyAsset>(relativeKey);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(DummyAsset::constructCount, 1);

    // Second load via full path resolves to the same normalized relative cache key
    auto second = Engine::ResourceManager::Load<DummyAsset>(resolvedFull);
    EXPECT_EQ(first, second);
    EXPECT_EQ(DummyAsset::constructCount, 1);

    // Get retrieves the cached instance without constructing
    EXPECT_EQ(Engine::ResourceManager::Get<DummyAsset>(relativeKey), first);

    // ClearType evicts all cached instances of DummyAsset
    Engine::ResourceManager::ClearType<DummyAsset>();
    EXPECT_EQ(Engine::ResourceManager::Get<DummyAsset>(relativeKey), nullptr);

    // Restore previous asset root & clean up temp folder
    if (!prevRoot.empty()) {
        Engine::ResourceManager::SetAssetRoot(prevRoot);
    }
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(ResourceManagerTest, ConcurrentLoadGetAndPathResolutionAreThreadSafe) {
    Engine::ResourceManager::ClearType<DummyAsset>();
    DummyAsset::constructCount = 0;

    const std::filesystem::path prevRoot = Engine::ResourceManager::GetAssetRoot();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "res_mgr_concurrent_test";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot / "textures", ec);
    Engine::ResourceManager::SetAssetRoot(tempRoot);

    constexpr int kNumThreads = 6;
    constexpr int kIterations = 40;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kIterations; ++i) {
                const std::string key = "textures/concurrent_" + std::to_string(i % 6) + ".png";
                auto loaded = Engine::ResourceManager::Load<DummyAsset>(key);
                EXPECT_NE(loaded, nullptr);
                auto fetched = Engine::ResourceManager::Get<DummyAsset>(key);
                EXPECT_NE(fetched, nullptr);
                if ((t % 2) == 0 && (i % 10) == 0) {
                    (void)Engine::ResourceManager::ResolvePathInfo(key);
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    // Also stress concurrent SetAssetRoot / GetAssetRoot / ResolvePath / Reload / ClearType
    std::vector<std::thread> rootThreads;
    for (int t = 0; t < 4; ++t) {
        rootThreads.emplace_back([t, &tempRoot]() {
            for (int i = 0; i < 25; ++i) {
                if (t == 0) {
                    Engine::ResourceManager::SetAssetRoot(tempRoot);
                } else {
                    auto root = Engine::ResourceManager::GetAssetRoot();
                    EXPECT_FALSE(root.empty());
                    (void)Engine::ResourceManager::ResolvePath("textures/item.png");
                    (void)Engine::ResourceManager::Reload<DummyAsset>("textures/concurrent_0.png");
                }
            }
        });
    }
    for (auto& th : rootThreads) {
        th.join();
    }

    Engine::ResourceManager::ClearType<DummyAsset>();
    if (!prevRoot.empty()) {
        Engine::ResourceManager::SetAssetRoot(prevRoot);
    }
    std::filesystem::remove_all(tempRoot, ec);
}

TEST(ResourceManagerTest, ResolvePathHitsCache) {
    Engine::ResourceManager::ClearPathCache();
    const std::string testPath = "textures/cached_check.png";
    const auto info1 = Engine::ResourceManager::ResolvePathInfo(testPath);
    EXPECT_FALSE(info1.ResolvedPath.empty());
    EXPECT_FALSE(info1.CacheKey.empty());

    // ResolvePath and ToRelativePath use the cached result
    EXPECT_EQ(Engine::ResourceManager::ResolvePath(testPath), info1.ResolvedPath);
    EXPECT_EQ(Engine::ResourceManager::ToRelativePath(testPath), info1.CacheKey);
    Engine::ResourceManager::ClearPathCache();
}

TEST(ResourceManagerTest, BoundedUploadsWithDeque) {
    // ProcessPendingUploads with 0 or empty queue returns 0 safely
    EXPECT_EQ(Engine::ResourceManager::ProcessPendingUploads(0), 0u);
    EXPECT_EQ(Engine::ResourceManager::ProcessPendingUploads(4), 0u);
}

TEST(ResourceManagerTest, StaleAsyncLoadFailureDoesNotEvictReplacedCacheEntry) {
    const bool wasInitialized = Engine::JobSystem::IsInitialized();
    if (!wasInitialized) {
        Engine::JobSystem::Init(2);
    }
    Engine::ResourceManager::ClearType<Engine::Texture>();

    const std::string missingPath = "textures/crit02_nonexistent_async_test.png";
    {
        // Kick off an async load for a missing file, then immediately drop caller ref and clear cache
        auto stalePlaceholder = Engine::ResourceManager::LoadTextureAsync(missingPath);
        ASSERT_NE(stalePlaceholder, nullptr);
        Engine::ResourceManager::ClearType<Engine::Texture>();
        stalePlaceholder.reset();

        // Insert a replacement Texture under the exact same path before/during stale failure completion
        auto replacement = Engine::ResourceManager::LoadTexture(missingPath);
        ASSERT_NE(replacement, nullptr);

        Engine::ResourceManager::WaitForPendingUploads();

        // Reloading from cache must still return the replacement entry (not evicted by stale worker)
        auto cachedAfter = Engine::ResourceManager::LoadTexture(missingPath);
        EXPECT_EQ(cachedAfter.get(), replacement.get());
    }

    Engine::ResourceManager::ClearType<Engine::Texture>();
    if (!wasInitialized) {
        Engine::JobSystem::Shutdown();
    }
}

TEST(ResourceManagerTest, ResolvePathHandlesTrailingSlashAndRejectsTraversalEscape) {
    const auto prevRoot = Engine::ResourceManager::GetAssetRoot();
    const auto tempRoot = std::filesystem::temp_directory_path() / "rm_trailing_slash_test";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot / "assets", ec);

    const std::string rootWithSlash = (tempRoot.generic_string() + "/");
    Engine::ResourceManager::SetAssetRoot(rootWithSlash);

    const std::string resolvedInside = Engine::ResourceManager::ResolvePath("textures/example.png");
    EXPECT_FALSE(resolvedInside.empty());
    EXPECT_NE(resolvedInside.find("rm_trailing_slash_test"), std::string::npos);

    const std::string escaped = Engine::ResourceManager::ResolvePath("../../outside_secret.txt");
    EXPECT_TRUE(escaped.empty());

    if (!prevRoot.empty()) {
        Engine::ResourceManager::SetAssetRoot(prevRoot);
    }
    std::filesystem::remove_all(tempRoot, ec);
}

} // namespace
