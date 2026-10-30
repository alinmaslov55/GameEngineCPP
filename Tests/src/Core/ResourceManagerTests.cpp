#include <gtest/gtest.h>
#include "Engine/Core/ResourceManager.h"
#include <filesystem>
#include <string>

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

} // namespace
