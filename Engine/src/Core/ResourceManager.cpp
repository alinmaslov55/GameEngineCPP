#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Renderer/Texture.h"

#include <stb_image.h>
#include <SDL3/SDL.h>

#include <system_error>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Engine {

namespace {
std::filesystem::path s_AssetRoot{};
uint64_t s_AssetRootEpoch = 0;
std::unordered_map<std::string, ResourceManager::ResolvedPathInfo> s_PathResolutionCache;
std::mutex s_PathMutex;

struct StbiPixelDeleter {
    void operator()(unsigned char* ptr) const noexcept {
        if (ptr) {
            stbi_image_free(ptr);
        }
    }
};

struct PendingTextureUpload {
    std::shared_ptr<Texture> TargetTexture;
    std::unique_ptr<unsigned char[], StbiPixelDeleter> PixelData;
    int Width = 0;
    int Height = 0;
    int BPP = 0;
};

std::mutex s_PendingUploadsMutex;
std::vector<PendingTextureUpload> s_PendingUploads;
std::mutex s_AsyncWaitMutex;
std::condition_variable s_AsyncLoadCV;
std::atomic<uint32_t> s_ActiveAsyncLoads{ 0 };
std::atomic<bool> s_AsyncTextureLoadingEnabled{ true };

struct ActiveLoadGuard {
    ~ActiveLoadGuard() {
        s_ActiveAsyncLoads.fetch_sub(1, std::memory_order_acq_rel);
        s_AsyncLoadCV.notify_all();
    }
};

bool HasProjectMarkers(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return false;
    }
    return std::filesystem::exists(dir / "assets", ec) ||
           std::filesystem::exists(dir / "test.png", ec) ||
           std::filesystem::exists(dir / "Village.scene", ec);
}

std::string ResolvePathWithRoot(const std::string& filepath, const std::filesystem::path& assetRoot) {
    if (filepath.empty()) {
        return {};
    }

    std::filesystem::path inputPath(filepath);
    if (inputPath.is_absolute()) {
        return inputPath.lexically_normal().generic_string();
    }

    std::error_code ec;
    if (!assetRoot.empty()) {
        std::filesystem::path candidate = (assetRoot / inputPath).lexically_normal();
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.generic_string();
        }
        std::filesystem::path assetSubCandidate = (assetRoot / "assets" / inputPath).lexically_normal();
        if (std::filesystem::exists(assetSubCandidate, ec)) {
            return assetSubCandidate.generic_string();
        }
        if (!std::filesystem::exists(inputPath, ec)) {
            return candidate.generic_string();
        }
    }

    return inputPath.lexically_normal().generic_string();
}

std::string ToRelativePathWithRoot(const std::string& filepath, const std::filesystem::path& assetRoot) {
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
    if (!assetRoot.empty()) {
        std::filesystem::path rel = std::filesystem::proximate(fullPath, assetRoot, ec);
        if (!ec) {
            std::string relStr = rel.generic_string();
            if (!relStr.empty() && relStr.rfind("..", 0) != 0) {
                return relStr;
            }
        }
    }

    fullPath = std::filesystem::weakly_canonical(filepath, ec);
    if (!ec && !assetRoot.empty()) {
        std::filesystem::path rel = std::filesystem::proximate(fullPath, assetRoot, ec);
        if (!ec) {
            std::string relStr = rel.generic_string();
            if (!relStr.empty() && relStr.rfind("..", 0) != 0) {
                return relStr;
            }
        }
    }

    return fullPath.generic_string();
}
} // namespace

void ResourceManager::SetAssetRoot(const std::filesystem::path& rootPath) {
    std::error_code ec;
    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(rootPath, ec);
    std::filesystem::path normalizedRoot = ec ? rootPath.lexically_normal() : canonicalPath;
    {
        std::lock_guard lock(s_PathMutex);
        s_AssetRoot = normalizedRoot;
        ++s_AssetRootEpoch;
        s_PathResolutionCache.clear();
    }
    ENGINE_CORE_INFO("ResourceManager: Asset root set to '{}'", normalizedRoot.generic_string());
}

std::filesystem::path ResourceManager::GetAssetRoot() {
    std::lock_guard lock(s_PathMutex);
    return s_AssetRoot;
}

std::filesystem::path ResourceManager::DiscoverAssetRoot(const std::string& projectSubdir) {
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);

    if (!cwd.empty()) {
        // 1. Running directly inside Sandbox/ or a standalone distribution folder
        if (!projectSubdir.empty() && cwd.filename() == projectSubdir && HasProjectMarkers(cwd)) {
            SetAssetRoot(cwd);
            auto root = GetAssetRoot();
            ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched project subdir CWD '{}'", root.string());
            return root;
        }
        // 2. Running from workspace root (e.g. C:/DEV/GameEngineCPP -> C:/DEV/GameEngineCPP/Sandbox)
        if (!projectSubdir.empty() && HasProjectMarkers(cwd / projectSubdir)) {
            SetAssetRoot(cwd / projectSubdir);
            auto root = GetAssetRoot();
            ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched CWD/{} '{}'", projectSubdir, root.string());
            return root;
        }
        if (HasProjectMarkers(cwd)) {
            SetAssetRoot(cwd);
            auto root = GetAssetRoot();
            ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched CWD markers '{}'", root.string());
            return root;
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
                auto root = GetAssetRoot();
                ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched exe ancestor subdir '{}' (depth {})",
                                  root.string(), depth);
                return root;
            }
            if (depth > 0 && HasProjectMarkers(probe)) {
                SetAssetRoot(probe);
                auto root = GetAssetRoot();
                ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched exe ancestor '{}' (depth {})",
                                  root.string(), depth);
                return root;
            }
            if (probe == probe.parent_path()) {
                break;
            }
            probe = probe.parent_path();
        }

        // 4. Fallback to assets copied next to executable
        if (HasProjectMarkers(exeDir)) {
            SetAssetRoot(exeDir);
            auto root = GetAssetRoot();
            ENGINE_CORE_DEBUG("ResourceManager::DiscoverAssetRoot matched exeDir '{}'", root.string());
            return root;
        }
    }

    // Fallback to current working directory
    if (!cwd.empty()) {
        SetAssetRoot(cwd);
        auto root = GetAssetRoot();
        ENGINE_CORE_WARN("ResourceManager::DiscoverAssetRoot found no project markers; falling back to CWD '{}'",
                         root.string());
        return root;
    }
    return GetAssetRoot();
}

std::string ResourceManager::ResolvePath(const std::string& filepath) {
    return ResolvePathWithRoot(filepath, GetAssetRoot());
}

std::string ResourceManager::ToRelativePath(const std::string& filepath) {
    return ToRelativePathWithRoot(filepath, GetAssetRoot());
}

ResourceManager::ResolvedPathInfo ResourceManager::ResolvePathInfo(const std::string& filepath) {
    if (filepath.empty()) {
        return {};
    }

    uint64_t epoch = 0;
    std::filesystem::path assetRoot;
    {
        std::lock_guard lock(s_PathMutex);
        auto it = s_PathResolutionCache.find(filepath);
        if (it != s_PathResolutionCache.end()) {
            return it->second;
        }
        assetRoot = s_AssetRoot;
        epoch = s_AssetRootEpoch;
    }

    std::string resolved = ResolvePathWithRoot(filepath, assetRoot);
    std::string key = ToRelativePathWithRoot(resolved, assetRoot);
    ResolvedPathInfo info{ resolved, key };

    {
        std::lock_guard lock(s_PathMutex);
        if (s_AssetRootEpoch == epoch) {
            s_PathResolutionCache.try_emplace(filepath, info);
        }
    }
    ENGINE_CORE_TRACE("ResourceManager: Cached path '{}' -> '{}' (key '{}')", filepath, resolved, key);
    return info;
}

void ResourceManager::ClearPathCache() {
    size_t prevCount = 0;
    {
        std::lock_guard lock(s_PathMutex);
        prevCount = s_PathResolutionCache.size();
        s_PathResolutionCache.clear();
    }
    ENGINE_CORE_DEBUG("ResourceManager: Clearing path resolution cache ({} entries)", prevCount);
}

void ResourceManager::SetAsyncTextureLoading(bool enabled) noexcept {
    s_AsyncTextureLoadingEnabled.store(enabled, std::memory_order_release);
    ENGINE_CORE_INFO("ResourceManager: Async texture loading {}", enabled ? "enabled" : "disabled");
}

bool ResourceManager::IsAsyncTextureLoadingEnabled() noexcept {
    return s_AsyncTextureLoadingEnabled.load(std::memory_order_acquire);
}

std::shared_ptr<Texture> ResourceManager::LoadTexture(const std::string& filepath) {
    if (filepath.empty()) {
        ENGINE_CORE_WARN("ResourceManager::LoadTexture called with empty filepath");
        return nullptr;
    }

    ResolvedPathInfo info = ResolvePathInfo(filepath);

    {
        std::lock_guard lock(AssetCache<Texture>::mutex);
        auto& cache = AssetCache<Texture>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end() && it->second) {
            ENGINE_CORE_TRACE("ResourceManager::LoadTexture: Cache hit for '{}'", info.CacheKey);
            return it->second;
        }
    }

    auto asset = std::make_shared<Texture>(info.ResolvedPath);

    {
        std::lock_guard lock(AssetCache<Texture>::mutex);
        auto& cache = AssetCache<Texture>::data;
        auto [it, inserted] = cache.try_emplace(info.CacheKey, asset);
        if (!inserted) {
            if (it->second) {
                ENGINE_CORE_TRACE("ResourceManager::LoadTexture: Returning concurrently cached texture '{}'", info.CacheKey);
                return it->second;
            }
            it->second = asset;
        }
    }

    ENGINE_CORE_INFO("ResourceManager: Loaded texture synchronously: '{}' (from '{}')", info.CacheKey, info.ResolvedPath);
    return asset;
}

std::shared_ptr<Texture> ResourceManager::LoadTextureAsync(const std::string& filepath) {
    if (filepath.empty()) {
        ENGINE_CORE_WARN("ResourceManager::LoadTextureAsync called with empty filepath");
        return nullptr;
    }

    ResolvedPathInfo info = ResolvePathInfo(filepath);

    // Check cache first (before falling back if JobSystem is inactive)
    {
        std::lock_guard lock(AssetCache<Texture>::mutex);
        auto& cache = AssetCache<Texture>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end() && it->second) {
            ENGINE_CORE_TRACE("ResourceManager::LoadTextureAsync: Returning cached asset '{}'", info.CacheKey);
            return it->second;
        }
    }

    // If JobSystem is not initialized with active workers, fallback to synchronous decode
    if (!JobSystem::IsInitialized() || JobSystem::GetWorkerCount() == 0) {
        ENGINE_CORE_DEBUG("ResourceManager::LoadTextureAsync falling back to synchronous load for '{}' (JobSystem inactive)",
                          filepath);
        return LoadTexture(filepath);
    }

    // Hold AssetCache<Texture>::mutex across check + placeholder insert to prevent TOCTOU duplicate dispatches
    std::shared_ptr<Texture> placeholder;
    {
        std::lock_guard lock(AssetCache<Texture>::mutex);
        auto& cache = AssetCache<Texture>::data;
        auto it = cache.find(info.CacheKey);
        if (it != cache.end() && it->second) {
            ENGINE_CORE_TRACE("ResourceManager::LoadTextureAsync: Returning concurrently cached asset '{}'", info.CacheKey);
            return it->second;
        }

        placeholder = Texture::CreatePlaceholder(info.ResolvedPath);
        cache[info.CacheKey] = placeholder;
    }

    s_ActiveAsyncLoads.fetch_add(1, std::memory_order_acq_rel);
    auto loadGuard = std::make_shared<ActiveLoadGuard>();
    ENGINE_CORE_INFO("ResourceManager: Dispatched async load for '{}' (placeholder ID: {})",
                     info.CacheKey, placeholder->GetRendererID());

    // Offload stbi_load to background worker thread; RAII loadGuard ensures count drops even on failure or teardown
    JobSystem::Dispatch([placeholder, resolvedPath = info.ResolvedPath, cacheKey = info.CacheKey, loadGuard]() {
        stbi_set_flip_vertically_on_load_thread(1);
        int w = 0, h = 0, bpp = 0;
        std::unique_ptr<unsigned char[], StbiPixelDeleter> pixelData(
            stbi_load(resolvedPath.c_str(), &w, &h, &bpp, 4));

        if (!pixelData) {
            ENGINE_CORE_ERROR("ResourceManager::LoadTextureAsync: Failed to load '{}'", resolvedPath);
            std::lock_guard lock(AssetCache<Texture>::mutex);
            auto& cache = AssetCache<Texture>::data;
            auto it = cache.find(cacheKey);
            if (it != cache.end() && it->second == placeholder) {
                cache.erase(it);
            }
            return;
        }

        // Enqueue decoded pixels for main-thread OpenGL upload
        ENGINE_CORE_DEBUG("ResourceManager::LoadTextureAsync decoded '{}' ({}x{}, bpp={})", resolvedPath, w, h, bpp);
        {
            std::lock_guard lock(s_PendingUploadsMutex);
            s_PendingUploads.push_back({ placeholder, std::move(pixelData), w, h, bpp });
        }
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
            auto splitEnd = s_PendingUploads.begin() + maxUploads;
            batch.reserve(maxUploads);
            batch.insert(batch.end(),
                         std::make_move_iterator(s_PendingUploads.begin()),
                         std::make_move_iterator(splitEnd));
            s_PendingUploads.erase(s_PendingUploads.begin(), splitEnd);
        }
    }

    ENGINE_CORE_DEBUG("ResourceManager: Uploading {} decoded async textures to GPU", batch.size());
    for (auto& upload : batch) {
        if (upload.TargetTexture && upload.PixelData) {
            upload.TargetTexture->PopulateFromData(upload.Width, upload.Height, upload.BPP, upload.PixelData.get());
        }
        upload.PixelData.reset();
    }

    return batch.size();
}

void ResourceManager::WaitForPendingUploads() {
    const uint32_t initialActive = s_ActiveAsyncLoads.load(std::memory_order_acquire);
    if (initialActive > 0) {
        ENGINE_CORE_INFO("ResourceManager: Waiting for {} active async texture loads to finish...", initialActive);
    }
    while (s_ActiveAsyncLoads.load(std::memory_order_acquire) > 0) {
        if (JobSystem::ExecuteOneJob()) {
            continue;
        }
        if (!JobSystem::IsInitialized() || JobSystem::GetWorkerCount() == 0) {
            std::unique_lock lock(s_AsyncWaitMutex);
            s_AsyncLoadCV.wait_for(lock, std::chrono::milliseconds(1), []() {
                return s_ActiveAsyncLoads.load(std::memory_order_acquire) == 0;
            });
            if (s_ActiveAsyncLoads.load(std::memory_order_acquire) > 0 && !JobSystem::IsInitialized()) {
                ENGINE_CORE_WARN("ResourceManager::WaitForPendingUploads: Stopping wait because JobSystem shut down ({} active loads)",
                                 s_ActiveAsyncLoads.load(std::memory_order_acquire));
                break;
            }
        } else {
            std::unique_lock lock(s_AsyncWaitMutex);
            s_AsyncLoadCV.wait_for(lock, std::chrono::milliseconds(1), []() {
                return s_ActiveAsyncLoads.load(std::memory_order_acquire) == 0;
            });
        }
    }
    ProcessPendingUploads();
}

uint32_t ResourceManager::GetPendingUploadCount() noexcept {
    return s_ActiveAsyncLoads.load(std::memory_order_acquire);
}

} // namespace Engine

