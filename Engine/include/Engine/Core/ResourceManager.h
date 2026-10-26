#ifndef CORE_RESOURCEMANAGER_H
#define CORE_RESOURCEMANAGER_H

#include "Engine/Utils/Logger.h"
#include <filesystem>
#include <unordered_map>
#include <string>
#include <memory>

namespace Engine {

    // Internal templated cache to hold specific asset types
    template<typename T>
    struct AssetCache {
        static std::unordered_map<std::string, std::shared_ptr<T>> data;
    };

    // Define the static member
    template<typename T>
    std::unordered_map<std::string, std::shared_ptr<T>> AssetCache<T>::data;

    class ResourceManager {
    public:
        // Configures the root directory used to resolve relative asset paths
        static void SetAssetRoot(const std::filesystem::path& rootPath);
        static const std::filesystem::path& GetAssetRoot();

        // Automatically discovers the asset root relative to working directory or executable location
        static std::filesystem::path DiscoverAssetRoot(const std::string& projectSubdir = "Sandbox");

        // Resolves a relative or absolute asset path against the configured AssetRoot
        static std::string ResolvePath(const std::string& filepath);

        // Converts an absolute or resolved path into a portable forward-slash path relative to AssetRoot
        static std::string ToRelativePath(const std::string& filepath);

        // Loads an asset from disk, or returns the cached version if it already exists
        template<typename T>
        static std::shared_ptr<T> Load(const std::string& filepath) {
            if (filepath.empty()) {
                return nullptr;
            }

            std::string resolvedPath = ResolvePath(filepath);
            std::string cacheKey = ToRelativePath(resolvedPath);

            auto& cache = AssetCache<T>::data;

            // Check if the asset is already loaded
            auto it = cache.find(cacheKey);
            if (it != cache.end()) {
                ENGINE_CORE_TRACE("ResourceManager: Returning cached asset: '{}' (resolved: '{}')", cacheKey, resolvedPath);
                return it->second;
            }

            // Instantiate using the resolved path, cache under normalized relative key, and return
            std::shared_ptr<T> asset = std::make_shared<T>(resolvedPath);
            cache[cacheKey] = asset;

            ENGINE_CORE_INFO("ResourceManager: Loaded new asset into cache: '{}' (from '{}')", cacheKey, resolvedPath);
            return asset;
        }

        // Retrieves an asset without attempting to load it (returns nullptr if missing)
        template<typename T>
        static std::shared_ptr<T> Get(const std::string& filepath) {
            if (filepath.empty()) {
                return nullptr;
            }

            std::string cacheKey = ToRelativePath(ResolvePath(filepath));
            auto& cache = AssetCache<T>::data;
            auto it = cache.find(cacheKey);
            if (it != cache.end()) {
                ENGINE_CORE_TRACE("ResourceManager::Get: Found asset '{}'", cacheKey);
                return it->second;
            }
            ENGINE_CORE_DEBUG("ResourceManager::Get: Asset '{}' not found in cache", cacheKey);
            return nullptr;
        }

        // Clears all loaded assets of a specific type to free memory
        template<typename T>
        static void ClearType() {
            auto& cache = AssetCache<T>::data;
            ENGINE_CORE_INFO("ResourceManager: Cleared cache (freed {} entries)", cache.size());
            cache.clear();
        }
    };
} // namespace Engine

#endif // CORE_RESOURCEMANAGER_H