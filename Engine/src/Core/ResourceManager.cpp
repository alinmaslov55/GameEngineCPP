#include "Engine/Core/ResourceManager.h"
#include <SDL3/SDL.h>
#include <system_error>

namespace Engine {

namespace {
std::filesystem::path s_AssetRoot{};

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
    ENGINE_CORE_INFO("ResourceManager: Asset root set to '{}'", s_AssetRoot.generic_string());
}

const std::filesystem::path& ResourceManager::GetAssetRoot() {
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
        if (std::filesystem::exists(candidate, ec) || !std::filesystem::exists(inputPath, ec)) {
            return candidate.generic_string();
        }
    }

    return inputPath.lexically_normal().generic_string();
}

std::string ResourceManager::ToRelativePath(const std::string& filepath) {
    if (filepath.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path fullPath = std::filesystem::weakly_canonical(filepath, ec);
    if (ec) {
        fullPath = std::filesystem::path(filepath).lexically_normal();
    }

    if (!s_AssetRoot.empty()) {
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

} // namespace Engine

