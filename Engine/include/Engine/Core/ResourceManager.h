#ifndef CORE_RESOURCEMANAGER_H
#define CORE_RESOURCEMANAGER_H

#include "Engine/Utils/Logger.h"
#include "Engine/Renderer/Texture.h"
#include <concepts>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Engine {

// Internal templated cache to hold specific asset types
template<typename T>
struct AssetCache {
    inline static std::unordered_map<std::string, std::shared_ptr<T>> data;
};

class ResourceManager {
public:
    struct ResolvedPathInfo {
        std::string ResolvedPath;
        std::string CacheKey;
    };

    // Configures the root directory used to resolve relative asset paths
    static void SetAssetRoot(const std::filesystem::path& rootPath);
    [[nodiscard]] static const std::filesystem::path& GetAssetRoot() noexcept;

    // Automatically discovers the asset root relative to working directory or executable location
    static std::filesystem::path DiscoverAssetRoot(const std::string& projectSubdir = "Sandbox");

    // Resolves a relative or absolute asset path against the configured AssetRoot
    [[nodiscard]] static std::string ResolvePath(const std::string& filepath);

    // Converts an absolute or resolved path into a portable forward-slash path relative to AssetRoot
    [[nodiscard]] static std::string ToRelativePath(const std::string& filepath);

    // Resolves both target path and cache key with in-memory caching to avoid filesystem syscalls
    [[nodiscard]] static ResolvedPathInfo ResolvePathInfo(const std::string& filepath);

    // Clears the internal path resolution cache
    static void ClearPathCache();

    // Loads a texture asynchronously. Returns a 1x1 placeholder texture immediately,
    // decoding image bytes on a background worker thread and uploading to OpenGL on the main thread.
    [[nodiscard]] static std::shared_ptr<Texture> LoadTextureAsync(const std::string& filepath);

    // Loads a texture synchronously (blocking calling thread until decode & upload complete).
    [[nodiscard]] static std::shared_ptr<Texture> LoadTexture(const std::string& filepath);

    // Configures whether generic Load<Texture>() defaults to asynchronous background loading.
    static void SetAsyncTextureLoading(bool enabled) noexcept;
    [[nodiscard]] static bool IsAsyncTextureLoadingEnabled() noexcept;

    // Flushes and processes pending OpenGL texture uploads on the main thread.
    // If maxUploads > 0, processes at most maxUploads to bound frame time; otherwise processes all ready uploads.
    // Returns the number of uploads processed.
    static size_t ProcessPendingUploads(uint32_t maxUploads = 0);

    // Blocks the calling thread until all background texture decoding tasks complete and uploads them to OpenGL.
    static void WaitForPendingUploads();

    // Returns the number of background texture decoding jobs currently in flight.
    [[nodiscard]] static uint32_t GetPendingUploadCount() noexcept;

    // Loads an asset from disk, or returns the cached version if it already exists
    template<typename T>
        requires std::constructible_from<T, std::string>
    [[nodiscard]] static std::shared_ptr<T> Load(const std::string& filepath) {
        if (filepath.empty()) {
            return nullptr;
        }

        ResolvedPathInfo info = ResolvePathInfo(filepath);
        auto& cache = AssetCache<T>::data;

        // Check if the asset is already loaded
        auto it = cache.find(info.CacheKey);
        if (it != cache.end()) {
            ENGINE_CORE_TRACE("ResourceManager: Returning cached asset: '{}' (resolved: '{}')", info.CacheKey, info.ResolvedPath);
            return it->second;
        }

        // Instantiate using the resolved path, cache under normalized relative key, and return
        std::shared_ptr<T> asset = std::make_shared<T>(info.ResolvedPath);
        cache[info.CacheKey] = asset;

        ENGINE_CORE_INFO("ResourceManager: Loaded new asset into cache: '{}' (from '{}')", info.CacheKey, info.ResolvedPath);
        return asset;
    }

    // Retrieves an asset without attempting to load it (returns nullptr if missing)
    template<typename T>
    [[nodiscard]] static std::shared_ptr<T> Get(const std::string& filepath) {
        if (filepath.empty()) {
            return nullptr;
        }

        ResolvedPathInfo info = ResolvePathInfo(filepath);
        auto& cache = AssetCache<T>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end()) {
            ENGINE_CORE_TRACE("ResourceManager::Get: Found asset '{}'", info.CacheKey);
            return it->second;
        }
        ENGINE_CORE_DEBUG("ResourceManager::Get: Asset '{}' not found in cache", info.CacheKey);
        return nullptr;
    }

    // Reloads an already-cached asset in-place if supported, or loads it fresh into the cache
    template<typename T>
    static std::shared_ptr<T> Reload(const std::string& filepath) {
        if (filepath.empty()) {
            return nullptr;
        }
        ResolvedPathInfo info = ResolvePathInfo(filepath);
        auto& cache = AssetCache<T>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end() && it->second) {
            if constexpr (requires(T& obj) { obj.ReloadFromDisk(); }) {
                ENGINE_CORE_INFO("ResourceManager: Reloading cached asset in-place: '{}'", info.CacheKey);
                it->second->ReloadFromDisk();
                return it->second;
            }
            ENGINE_CORE_DEBUG("ResourceManager: Evicting cached asset for fresh reload: '{}'", info.CacheKey);
            cache.erase(it);
        }
        return Load<T>(filepath);
    }

    // Clears all loaded assets of a specific type to free memory
    template<typename T>
    static void ClearType() {
        auto& cache = AssetCache<T>::data;
        ENGINE_CORE_INFO("ResourceManager: Cleared cache (freed {} entries)", cache.size());
        cache.clear();
    }
};

template<>
[[nodiscard]] inline std::shared_ptr<Texture> ResourceManager::Load<Texture>(const std::string& filepath) {
    if (IsAsyncTextureLoadingEnabled()) {
        return LoadTextureAsync(filepath);
    }
    return LoadTexture(filepath);
}

template<>
inline void ResourceManager::ClearType<Texture>() {
    WaitForPendingUploads();
    auto& cache = AssetCache<Texture>::data;
    ENGINE_CORE_INFO("ResourceManager: Cleared Texture cache (freed {} entries)", cache.size());
    cache.clear();
}

} // namespace Engine

#endif // CORE_RESOURCEMANAGER_H
