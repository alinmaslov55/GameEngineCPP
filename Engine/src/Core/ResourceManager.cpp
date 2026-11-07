#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Renderer/Texture.h"

#include <stb_image.h>
#include <SDL3/SDL.h>

#include <system_error>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

namespace Engine {

namespace {
std::filesystem::path s_AssetRoot{};
std::unordered_map<std::string, ResourceManager::ResolvedPathInfo> s_PathResolutionCache;
std::mutex s_ResourceManagerMutex;

struct PendingTextureUpload {
    std::shared_ptr<Texture> TargetTexture;
    unsigned char* PixelData = nullptr;
    int Width = 0;
    int Height = 0;
    int BPP = 0;
};

std::mutex s_PendingUploadsMutex;
std::vector<PendingTextureUpload> s_PendingUploads;
std::atomic<uint32_t> s_ActiveAsyncLoads{ 0 };
std::atomic<bool> s_AsyncTextureLoadingEnabled{ true };

bool HasProjectMarkers(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return false;
    }
    return std::filesystem::exists(dir / "assets", ec) ||
           std::filesystem::exists(dir / "test.png", ec) ||
           std::filesystem::exists(dir / "Village.scene", ec);
}
} // namespace

void ResourceManager::SetAssetRoot(const std::filesystem::path& rootPath) {
    std::error_code ec;
    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(rootPath, ec);
    s_AssetRoot = ec ? rootPath.lexically_normal() : canonicalPath;
    s_PathResolutionCache.clear();
    ENGINE_CORE_INFO("ResourceManager: Asset root set to '{}'", s_AssetRoot.generic_string());
}

const std::filesystem::path& ResourceManager::GetAssetRoot() noexcept {
    return s_AssetRoot;
}

std::filesystem::path ResourceManager::DiscoverAssetRoot(const std::string& projectSubdir) {
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);

    if (!cwd.empty()) {
        // 1. Running directly inside Sandbox/ or a standalone distribution folder
        if (!projectSubdir.empty() && cwd.filename() == projectSubdir && HasProjectMarkers(cwd)) {
            SetAssetRoot(cwd);
            return s_AssetRoot;
        }
        // 2. Running from workspace root (e.g. C:/DEV/GameEngineCPP -> C:/DEV/GameEngineCPP/Sandbox)
        if (!projectSubdir.empty() && HasProjectMarkers(cwd / projectSubdir)) {
            SetAssetRoot(cwd / projectSubdir);
            return s_AssetRoot;
        }
        if (HasProjectMarkers(cwd)) {
            SetAssetRoot(cwd);
            return s_AssetRoot;
        }
    }

    // 3. Walk up from executable directory (e.g. build/Sandbox/Debug/EngineSandbox.exe -> Sandbox)
    const char* sdlBase = SDL_GetBasePath();
    if (sdlBase) {
        std::filesystem::path exeDir(sdlBase);
        std::filesystem::path probe = exeDir;
        for (int depth = 0; depth < 5 && !probe.empty(); ++depth) {
            if (!projectSubdir.empty() && HasProjectMarkers(probe / projectSubdir)) {
                SetAssetRoot(probe / projectSubdir);
                return s_AssetRoot;
            }
            if (depth > 0 && HasProjectMarkers(probe)) {
                SetAssetRoot(probe);
                return s_AssetRoot;
            }
            if (probe == probe.parent_path()) {
                break;
            }
            probe = probe.parent_path();
        }

        // 4. Fallback to assets copied next to executable
        if (HasProjectMarkers(exeDir)) {
            SetAssetRoot(exeDir);
            return s_AssetRoot;
        }
    }

    // Fallback to current working directory
    if (!cwd.empty()) {
        SetAssetRoot(cwd);
    }
    return s_AssetRoot;
}

std::string ResourceManager::ResolvePath(const std::string& filepath) {
    if (filepath.empty()) {
        return {};
    }

    std::filesystem::path inputPath(filepath);
    if (inputPath.is_absolute()) {
        return inputPath.lexically_normal().generic_string();
    }

    std::error_code ec;
    if (!s_AssetRoot.empty()) {
        std::filesystem::path candidate = (s_AssetRoot / inputPath).lexically_normal();
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.generic_string();
        }
        std::filesystem::path assetSubCandidate = (s_AssetRoot / "assets" / inputPath).lexically_normal();
        if (std::filesystem::exists(assetSubCandidate, ec)) {
            return assetSubCandidate.generic_string();
        }
        if (!std::filesystem::exists(inputPath, ec)) {
            return candidate.generic_string();
        }
    }

    return inputPath.lexically_normal().generic_string();
}

std::string ResourceManager::ToRelativePath(const std::string& filepath) {
    if (filepath.empty()) {
        return {};
    }

    std::filesystem::path p(filepath);
    if (p.is_relative()) {
        std::string gen = p.lexically_normal().generic_string();
        if (!gen.empty() && gen.rfind("..", 0) != 0) {
            return gen;
        }
    }

    std::error_code ec;
    std::filesystem::path fullPath = std::filesystem::path(filepath).lexically_normal();
    if (!s_AssetRoot.empty()) {
        std::filesystem::path rel = std::filesystem::proximate(fullPath, s_AssetRoot, ec);
        if (!ec) {
            std::string relStr = rel.generic_string();
            if (!relStr.empty() && relStr.rfind("..", 0) != 0) {
                return relStr;
            }
        }
    }

    fullPath = std::filesystem::weakly_canonical(filepath, ec);
    if (!ec && !s_AssetRoot.empty()) {
        std::filesystem::path rel = std::filesystem::proximate(fullPath, s_AssetRoot, ec);
        if (!ec) {
            std::string relStr = rel.generic_string();
            if (!relStr.empty() && relStr.rfind("..", 0) != 0) {
                return relStr;
            }
        }
    }

    return fullPath.generic_string();
}

ResourceManager::ResolvedPathInfo ResourceManager::ResolvePathInfo(const std::string& filepath) {
    if (filepath.empty()) {
        return {};
    }

    std::lock_guard lock(s_ResourceManagerMutex);
    auto it = s_PathResolutionCache.find(filepath);
    if (it != s_PathResolutionCache.end()) {
        return it->second;
    }

    std::string resolved = ResolvePath(filepath);
    std::string key = ToRelativePath(resolved);
    s_PathResolutionCache[filepath] = { resolved, key };
    return { resolved, key };
}

void ResourceManager::ClearPathCache() {
    std::lock_guard lock(s_ResourceManagerMutex);
    s_PathResolutionCache.clear();
}

void ResourceManager::SetAsyncTextureLoading(bool enabled) noexcept {
    s_AsyncTextureLoadingEnabled.store(enabled, std::memory_order_release);
}

bool ResourceManager::IsAsyncTextureLoadingEnabled() noexcept {
    return s_AsyncTextureLoadingEnabled.load(std::memory_order_acquire);
}

std::shared_ptr<Texture> ResourceManager::LoadTexture(const std::string& filepath) {
    if (filepath.empty()) {
        return nullptr;
    }

    ResolvedPathInfo info = ResolvePathInfo(filepath);

    {
        std::lock_guard lock(s_ResourceManagerMutex);
        auto& cache = AssetCache<Texture>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end()) {
            return it->second;
        }
    }

    auto asset = std::make_shared<Texture>(info.ResolvedPath);

    {
        std::lock_guard lock(s_ResourceManagerMutex);
        AssetCache<Texture>::data[info.CacheKey] = asset;
    }

    ENGINE_CORE_INFO("ResourceManager: Loaded texture synchronously: '{}' (from '{}')", info.CacheKey, info.ResolvedPath);
    return asset;
}

std::shared_ptr<Texture> ResourceManager::LoadTextureAsync(const std::string& filepath) {
    if (filepath.empty()) {
        return nullptr;
    }

    ResolvedPathInfo info = ResolvePathInfo(filepath);

    {
        std::lock_guard lock(s_ResourceManagerMutex);
        auto& cache = AssetCache<Texture>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end()) {
            ENGINE_CORE_TRACE("ResourceManager::LoadTextureAsync: Returning cached asset '{}'", info.CacheKey);
            return it->second;
        }
    }

    // If JobSystem is not initialized with active workers, fallback to synchronous decode
    if (!JobSystem::IsInitialized() || JobSystem::GetWorkerCount() == 0) {
        return LoadTexture(filepath);
    }

    // 1. Create 1x1 placeholder texture immediately
    auto placeholder = Texture::CreatePlaceholder(info.ResolvedPath);

    {
        std::lock_guard lock(s_ResourceManagerMutex);
        AssetCache<Texture>::data[info.CacheKey] = placeholder;
    }

    s_ActiveAsyncLoads.fetch_add(1, std::memory_order_release);
    ENGINE_CORE_INFO("ResourceManager: Dispatched async load for '{}' (placeholder ID: {})",
                     info.CacheKey, placeholder->GetRendererID());

    // 2. Offload stbi_load to background worker thread
    JobSystem::Dispatch([placeholder, resolvedPath = info.ResolvedPath]() {
        stbi_set_flip_vertically_on_load_thread(1);
        int w = 0, h = 0, bpp = 0;
        unsigned char* data = stbi_load(resolvedPath.c_str(), &w, &h, &bpp, 4);

        if (!data) {
            ENGINE_CORE_ERROR("ResourceManager::LoadTextureAsync: Failed to load '{}'", resolvedPath);
            s_ActiveAsyncLoads.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }

        // 3. Enqueue upload on main thread via PendingUploads queue
        {
            std::lock_guard lock(s_PendingUploadsMutex);
            s_PendingUploads.push_back({ placeholder, data, w, h, bpp });
        }
        s_ActiveAsyncLoads.fetch_sub(1, std::memory_order_acq_rel);
    });

    return placeholder;
}

size_t ResourceManager::ProcessPendingUploads(uint32_t maxUploads) {
    std::vector<PendingTextureUpload> batch;
    {
        std::lock_guard lock(s_PendingUploadsMutex);
        if (s_PendingUploads.empty()) {
            return 0;
        }

        if (maxUploads == 0 || maxUploads >= s_PendingUploads.size()) {
            batch = std::move(s_PendingUploads);
            s_PendingUploads.clear();
        } else {
            batch.assign(s_PendingUploads.begin(), s_PendingUploads.begin() + maxUploads);
            s_PendingUploads.erase(s_PendingUploads.begin(), s_PendingUploads.begin() + maxUploads);
        }
    }

    for (auto& upload : batch) {
        if (upload.TargetTexture && upload.PixelData) {
            upload.TargetTexture->PopulateFromData(upload.Width, upload.Height, upload.BPP, upload.PixelData);
        }
        if (upload.PixelData) {
            stbi_image_free(upload.PixelData);
        }
    }

    return batch.size();
}

void ResourceManager::WaitForPendingUploads() {
    while (s_ActiveAsyncLoads.load(std::memory_order_acquire) > 0) {
        if (!JobSystem::ExecuteOneJob()) {
            std::this_thread::yield();
        }
    }
    ProcessPendingUploads();
}

uint32_t ResourceManager::GetPendingUploadCount() noexcept {
    return s_ActiveAsyncLoads.load(std::memory_order_acquire);
}

} // namespace Engine

