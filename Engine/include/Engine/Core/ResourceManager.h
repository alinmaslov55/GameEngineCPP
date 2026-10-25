#ifndef CORE_RESOURCEMANAGER_H
#define CORE_RESOURCEMANAGER_H

#include "Engine/Utils/Logger.h"
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
        // Loads an asset from disk, or returns the cached version if it already exists
        template<typename T>
        static std::shared_ptr<T> Load(const std::string& filepath) {
            auto& cache = AssetCache<T>::data;

            // Check if the asset is already loaded
            if (cache.find(filepath) != cache.end()) {
                ENGINE_CORE_TRACE("ResourceManager: Returning cached asset: '{}'", filepath);
                return cache[filepath];
            }

            // If not, instantiate it, cache it, and return it
            std::shared_ptr<T> asset = std::make_shared<T>(filepath);
            cache[filepath] = asset;

            ENGINE_CORE_INFO("ResourceManager: Loaded new asset into cache: '{}'", filepath);
            return asset;
        }

        // Retrieves an asset without attempting to load it (returns nullptr if missing)
        template<typename T>
        static std::shared_ptr<T> Get(const std::string& filepath) {
            auto& cache = AssetCache<T>::data;
            if (cache.find(filepath) != cache.end()) {
                ENGINE_CORE_TRACE("ResourceManager::Get: Found asset '{}'", filepath);
                return cache[filepath];
            }
            ENGINE_CORE_DEBUG("ResourceManager::Get: Asset '{}' not found in cache", filepath);
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
