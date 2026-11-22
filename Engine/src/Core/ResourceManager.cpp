#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Renderer/Texture.h"

#include <stb_image.h>
#include <SDL3/SDL.h>

#include <system_error>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
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
    std::vector<uint8_t> CompressedPayload;
    int Width = 0;
    int Height = 0;
    int BPP = 0;
    bool IsCompressed = false;
};

std::string ResolveTextureDiskPath(const std::string& resolvedPath) {
    if (resolvedPath.empty()) {
        return resolvedPath;
    }
    std::filesystem::path p(resolvedPath);
    std::string ext = p.extension().generic_string();
    std::string extLower = ext;
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg") {
        std::filesystem::path ddsPath = p;
        ddsPath.replace_extension(".dds");
        std::error_code ecDds, ecPng;
        bool ddsExists = std::filesystem::exists(ddsPath, ecDds);
        bool pngExists = std::filesystem::exists(p, ecPng);
        if (ddsExists && !pngExists) {
            ENGINE_CORE_DEBUG("ResourceManager: Found pre-compressed DDS sibling '{}' (source '{}' not found)",
                              ddsPath.generic_string(), resolvedPath);
            return ddsPath.generic_string();
        }
        if (ddsExists && pngExists) {
            auto ddsMtime = std::filesystem::last_write_time(ddsPath, ecDds);
            auto pngMtime = std::filesystem::last_write_time(p, ecPng);
            if (!ecDds && !ecPng && ddsMtime >= pngMtime) {
                ENGINE_CORE_DEBUG("ResourceManager: Selecting newer/equal pre-compressed DDS sibling '{}' over '{}'",
                                  ddsPath.generic_string(), resolvedPath);
                return ddsPath.generic_string();
            } else {
                ENGINE_CORE_DEBUG("ResourceManager: Selecting newer source image '{}' over DDS sibling '{}'",
                                  resolvedPath, ddsPath.generic_string());
                return resolvedPath;
            }
        }
    }
    return resolvedPath;
}

std::mutex s_PendingUploadsMutex;
std::deque<PendingTextureUpload> s_PendingUploads;
static std::mutex s_DeferredGarbageMutex;
static std::vector<std::shared_ptr<Texture>> s_DeferredTextureGarbage;
std::mutex s_AsyncWaitMutex;
std::condition_variable s_AsyncLoadCV;
std::atomic<uint32_t> s_ActiveAsyncLoads{ 0 };
std::atomic<bool> s_AsyncTextureLoadingEnabled{ true };

static void QueueTextureGarbage(std::shared_ptr<Texture> tex) {
    if (!tex) return;
    std::lock_guard lock(s_DeferredGarbageMutex);
    s_DeferredTextureGarbage.push_back(std::move(tex));
}

static void DrainDeferredTextureGarbage() {
    std::vector<std::shared_ptr<Texture>> toDestroy;
    {
        std::lock_guard lock(s_DeferredGarbageMutex);
        if (s_DeferredTextureGarbage.empty()) return;
        toDestroy = std::move(s_DeferredTextureGarbage);
    }
    toDestroy.clear();
}

struct ActiveLoadGuard {
    ~ActiveLoadGuard() {
        {
            std::lock_guard lock(s_AsyncWaitMutex);
            s_ActiveAsyncLoads.fetch_sub(1, std::memory_order_acq_rel);
        }
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
        std::filesystem::path normalRoot = assetRoot.lexically_normal();
        std::filesystem::path candidate = (assetRoot / inputPath).lexically_normal();

        // Ensure normalized relative candidate does not escape assetRoot via ../ traversal
        auto [rootEnd, _] = std::mismatch(normalRoot.begin(), normalRoot.end(), candidate.begin(), candidate.end());
        if (rootEnd == normalRoot.end()) {
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
    return ResolvePathInfo(filepath).ResolvedPath;
}

std::string ResourceManager::ToRelativePath(const std::string& filepath) {
    return ResolvePathInfo(filepath).CacheKey;
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

    std::error_code ec;
    if (std::filesystem::exists(resolved, ec)) {
        std::lock_guard lock(s_PathMutex);
        if (s_AssetRootEpoch == epoch) {
            s_PathResolutionCache.try_emplace(filepath, info);
        }
        ENGINE_CORE_TRACE("ResourceManager: Cached path '{}' -> '{}' (key '{}')", filepath, resolved, key);
    }
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

    std::string actualDiskPath = ResolveTextureDiskPath(info.ResolvedPath);
    auto asset = std::make_shared<Texture>(actualDiskPath);

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

    ENGINE_CORE_INFO("ResourceManager: Loaded texture synchronously: '{}' (from '{}')", info.CacheKey, actualDiskPath);
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

    std::string actualDiskPath = ResolveTextureDiskPath(info.ResolvedPath);

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

        placeholder = Texture::CreatePlaceholder(actualDiskPath);
        cache[info.CacheKey] = placeholder;
    }

    s_ActiveAsyncLoads.fetch_add(1, std::memory_order_acq_rel);
    auto loadGuard = std::make_shared<ActiveLoadGuard>();
    ENGINE_CORE_INFO("ResourceManager: Dispatched async load for '{}' (placeholder ID: {})",
                     info.CacheKey, placeholder->GetRendererID());

    std::weak_ptr<Texture> placeholderWeak = placeholder;
    const std::string originalPath = info.ResolvedPath;
    const std::string cacheKey = info.CacheKey;

    JobSystem::Dispatch([placeholderWeak, actualDiskPath, originalPath, cacheKey, loadGuard]() {
        auto handleFailure = [&]() {
            std::shared_ptr<Texture> doomed;
            {
                std::lock_guard lock(AssetCache<Texture>::mutex);
                auto& cache = AssetCache<Texture>::data;
                auto it = cache.find(cacheKey);
                if (it != cache.end()) {
                    auto target = placeholderWeak.lock();
                    if (!target || it->second == target) {
                        doomed = it->second;
                        cache.erase(it);
                    }
                }
            }
            if (doomed) {
                QueueTextureGarbage(std::move(doomed));
            }
        };

        bool ddsAttempted = false;
        bool ddsSucceeded = false;
        std::vector<uint8_t> ddsBuffer;
        Texture::DDSInfo dds;

        if (Texture::IsDDS(actualDiskPath)) {
            ddsAttempted = true;
            std::ifstream file(actualDiskPath, std::ios::binary | std::ios::ate);
            if (file) {
                std::streamsize fileSize = file.tellg();
                if (fileSize >= 128) {
                    file.seekg(0, std::ios::beg);
                    ddsBuffer.resize(static_cast<size_t>(fileSize));
                    if (file.read(reinterpret_cast<char*>(ddsBuffer.data()), fileSize)) {
                        dds = Texture::ParseDDS(ddsBuffer.data(), ddsBuffer.size());
                        if (dds.Valid) {
                            ddsSucceeded = true;
                        }
                    }
                }
            }

            if (ddsSucceeded) {
                auto target = placeholderWeak.lock();
                if (!target) {
                    return;
                }
                ENGINE_CORE_DEBUG("ResourceManager::LoadTextureAsync decoded DDS '{}' ({}x{}, mips={})", actualDiskPath, dds.Width, dds.Height, dds.MipCount);
                {
                    std::lock_guard lock(s_PendingUploadsMutex);
                    PendingTextureUpload upload;
                    upload.TargetTexture = std::move(target);
                    upload.CompressedPayload = std::move(ddsBuffer);
                    upload.Width = dds.Width;
                    upload.Height = dds.Height;
                    upload.BPP = 4;
                    upload.IsCompressed = true;
                    s_PendingUploads.push_back(std::move(upload));
                }
                return;
            }

            // DDS failed: check if we can fall back to original image
            std::error_code ec;
            if (actualDiskPath != originalPath && !originalPath.empty() && std::filesystem::exists(originalPath, ec)) {
                ENGINE_CORE_WARN("ResourceManager::LoadTextureAsync: DDS '{}' corrupt or invalid, falling back to original image '{}'",
                                 actualDiskPath, originalPath);
            } else {
                ENGINE_CORE_ERROR("ResourceManager::LoadTextureAsync: Failed to parse DDS '{}'", actualDiskPath);
                handleFailure();
                return;
            }
        }

        const std::string& stbiPath = (ddsAttempted && !ddsSucceeded) ? originalPath : actualDiskPath;
        stbi_set_flip_vertically_on_load_thread(1);
        int w = 0, h = 0, bpp = 0;
        std::unique_ptr<unsigned char[], StbiPixelDeleter> pixelData(
            stbi_load(stbiPath.c_str(), &w, &h, &bpp, 4));

        if (!pixelData) {
            ENGINE_CORE_ERROR("ResourceManager::LoadTextureAsync: Failed to load '{}'", stbiPath);
            handleFailure();
            return;
        }

        auto target = placeholderWeak.lock();
        if (!target) {
            return;
        }

        ENGINE_CORE_DEBUG("ResourceManager::LoadTextureAsync decoded '{}' ({}x{}, bpp={})", stbiPath, w, h, bpp);
        {
            std::lock_guard lock(s_PendingUploadsMutex);
            PendingTextureUpload upload;
            upload.TargetTexture = std::move(target);
            upload.PixelData = std::move(pixelData);
            upload.Width = w;
            upload.Height = h;
            upload.BPP = bpp;
            upload.IsCompressed = false;
            s_PendingUploads.push_back(std::move(upload));
        }
    });

    return placeholder;
}

size_t ResourceManager::ProcessPendingUploads(uint32_t maxUploads) {
    DrainDeferredTextureGarbage();
    std::vector<PendingTextureUpload> batch;
    {
        std::lock_guard lock(s_PendingUploadsMutex);
        if (s_PendingUploads.empty()) {
            return 0;
        }

        if (maxUploads == 0 || maxUploads >= s_PendingUploads.size()) {
            batch.reserve(s_PendingUploads.size());
            while (!s_PendingUploads.empty()) {
                batch.push_back(std::move(s_PendingUploads.front()));
                s_PendingUploads.pop_front();
            }
        } else {
            batch.reserve(maxUploads);
            for (uint32_t i = 0; i < maxUploads && !s_PendingUploads.empty(); ++i) {
                batch.push_back(std::move(s_PendingUploads.front()));
                s_PendingUploads.pop_front();
            }
        }
    }

    ENGINE_CORE_DEBUG("ResourceManager: Uploading {} decoded async textures to GPU", batch.size());
    for (auto& upload : batch) {
        if (upload.TargetTexture) {
            if (upload.IsCompressed && !upload.CompressedPayload.empty()) {
                Texture::DDSInfo dds = Texture::ParseDDS(upload.CompressedPayload.data(), upload.CompressedPayload.size());
                if (dds.Valid) {
                    upload.TargetTexture->PopulateFromCompressedData(dds.Width, dds.Height, dds.GLFormat, dds.Mips);
                }
            } else if (upload.PixelData) {
                upload.TargetTexture->PopulateFromData(upload.Width, upload.Height, upload.BPP, upload.PixelData.get());
            }
        }
        upload.PixelData.reset();
        upload.CompressedPayload.clear();
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
    std::lock_guard lock(s_PendingUploadsMutex);
    return s_ActiveAsyncLoads.load(std::memory_order_acquire) + static_cast<uint32_t>(s_PendingUploads.size());
}

std::shared_ptr<AudioClip> ResourceManager::LoadAudioClip(const std::string& filepath) {
    return Load<AudioClip>(filepath);
}

} // namespace Engine

