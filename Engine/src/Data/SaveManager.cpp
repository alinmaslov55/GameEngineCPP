#include "Engine/Data/SaveManager.h"

#include "Engine/Audio/AudioManager.h"
#include "Engine/Core/Application.h"
#include "Engine/Core/Input.h"
#include "Engine/Data/GameplayDefinitions.h"
#include "Engine/Utils/Logger.h"

#include <SDL3/SDL.h>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Engine {

namespace {

static std::filesystem::path s_DirectoryOverride;

static nlohmann::json ProfileToJson(const PlayerProfileData& p) {
    nlohmann::json j;
    j["SchemaVersion"] = p.SchemaVersion;
    j["BankedAshes"] = p.BankedAshes;
    j["TotalRuns"] = p.TotalRuns;
    j["BestSurvivalSeconds"] = p.BestSurvivalSeconds;
    j["UnlockedRootRanks"] = p.UnlockedRootRanks;
    j["DiscoveredRunes"] = p.DiscoveredRunes;
    j["RescuedCompanions"] = p.RescuedCompanions;
    j["BusVolumes"] = p.BusVolumes;
    j["Fullscreen"] = p.Fullscreen;
    j["VSync"] = p.VSync;
    j["CustomActionBindings"] = p.CustomActionBindings.is_null() ? nlohmann::json::object() : p.CustomActionBindings;
    return j;
}

static bool ProfileFromJson(const nlohmann::json& j, PlayerProfileData& out) {
    if (!j.is_object()) {
        return false;
    }
    out.SchemaVersion = JsonHelper::Get(j, "SchemaVersion", 1u);
    if (j.contains("SchemaVersion") && j["SchemaVersion"].is_number_integer() && !j["SchemaVersion"].is_number_unsigned() && j["SchemaVersion"].get<int64_t>() < 0) {
        out.SchemaVersion = 1u;
    }
    out.BankedAshes = (j.contains("BankedAshes") && j["BankedAshes"].is_number_unsigned()) ? j["BankedAshes"].get<uint64_t>() : 0ULL;
    out.TotalRuns = (j.contains("TotalRuns") && j["TotalRuns"].is_number_unsigned()) ? j["TotalRuns"].get<uint32_t>() : 0u;
    out.BestSurvivalSeconds = (j.contains("BestSurvivalSeconds") && j["BestSurvivalSeconds"].is_number_unsigned()) ? j["BestSurvivalSeconds"].get<uint32_t>() : 0u;

    out.UnlockedRootRanks.clear();
    if (j.contains("UnlockedRootRanks") && j["UnlockedRootRanks"].is_object()) {
        for (auto it = j["UnlockedRootRanks"].begin(); it != j["UnlockedRootRanks"].end(); ++it) {
            if (it.value().is_number_unsigned()) {
                out.UnlockedRootRanks[it.key()] = it.value().get<uint32_t>();
            }
        }
    }

    out.DiscoveredRunes.clear();
    if (j.contains("DiscoveredRunes") && j["DiscoveredRunes"].is_array()) {
        for (const auto& item : j["DiscoveredRunes"]) {
            if (item.is_string()) {
                out.DiscoveredRunes.push_back(item.get<std::string>());
            }
        }
    }

    out.RescuedCompanions.clear();
    if (j.contains("RescuedCompanions") && j["RescuedCompanions"].is_array()) {
        for (const auto& item : j["RescuedCompanions"]) {
            if (item.is_string()) {
                out.RescuedCompanions.push_back(item.get<std::string>());
            }
        }
    }

    if (j.contains("BusVolumes") && j["BusVolumes"].is_array()) {
        for (size_t i = 0; i < out.BusVolumes.size() && i < j["BusVolumes"].size(); ++i) {
            if (j["BusVolumes"][i].is_number()) {
                out.BusVolumes[i] = j["BusVolumes"][i].get<float>();
            }
        }
    }

    out.Fullscreen = JsonHelper::Get(j, "Fullscreen", false);
    out.VSync = JsonHelper::Get(j, "VSync", true);

    if (j.contains("CustomActionBindings") && !j["CustomActionBindings"].is_null()) {
        out.CustomActionBindings = j["CustomActionBindings"];
    } else {
        out.CustomActionBindings = nlohmann::json::object();
    }

    return true;
}

static bool TryLoadFile(const std::filesystem::path& path, PlayerProfileData& outData) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
        return false;
    }

    std::ifstream inFile(path, std::ios::in | std::ios::binary);
    if (!inFile.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }

    nlohmann::json envelope = nlohmann::json::parse(content, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object()) {
        return false;
    }

    if (!envelope.contains("CRC32") || !envelope["CRC32"].is_number_unsigned()) {
        return false;
    }
    if (!envelope.contains("Payload") || !envelope["Payload"].is_object()) {
        return false;
    }

    const uint32_t expectedCrc = envelope["CRC32"].get<uint32_t>();
    const std::string compactPayload = envelope["Payload"].dump();
    const uint32_t actualCrc = Checksum::CRC32(compactPayload);

    if (actualCrc != expectedCrc) {
        ENGINE_CORE_WARN("SaveManager: Checksum mismatch in '{}' (expected: 0x{:08X}, actual: 0x{:08X})",
                         path.string(), expectedCrc, actualCrc);
        return false;
    }

    return ProfileFromJson(envelope["Payload"], outData);
}

static bool WriteAndReplacePrimaryAtomic(uint32_t slotIndex, const PlayerProfileData& data, bool backupCurrentPrimary) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path saveDir = SaveManager::GetSaveDirectory();
    if (!fs::exists(saveDir, ec)) {
        fs::create_directories(saveDir, ec);
    }

    const std::string baseName = "profile_" + std::to_string(slotIndex);
    const fs::path primaryPath = saveDir / (baseName + ".json");
    const fs::path tempPath = saveDir / (baseName + ".json.tmp");
    const fs::path backupPath = saveDir / (baseName + ".json.bak");

    nlohmann::json payloadJson = ProfileToJson(data);
    const std::string compactPayload = payloadJson.dump();
    const uint32_t crc = Checksum::CRC32(compactPayload);

    nlohmann::json envelope;
    envelope["CRC32"] = crc;
    envelope["Payload"] = payloadJson;

    const std::string envelopeStr = envelope.dump(2);

    // 1. Write to temp file
    {
        std::ofstream outFile(tempPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!outFile.is_open()) {
            ENGINE_CORE_ERROR("SaveManager: Failed to open temporary file '{}' for writing", tempPath.string());
            return false;
        }
        outFile.write(envelopeStr.data(), static_cast<std::streamsize>(envelopeStr.size()));
        outFile.flush();
        if (!outFile.good()) {
            ENGINE_CORE_ERROR("SaveManager: Stream write error on '{}'", tempPath.string());
            return false;
        }
    }

    // 2. If requested and primary exists, atomically write/replace backup before replacing primary
    if (backupCurrentPrimary && fs::exists(primaryPath, ec)) {
        const fs::path bakTempPath = saveDir / (baseName + ".json.bak.tmp");
        std::ifstream src(primaryPath, std::ios::in | std::ios::binary);
        std::ofstream dst(bakTempPath, std::ios::out | std::ios::trunc | std::ios::binary);
        bool bakOk = false;
        if (src.is_open() && dst.is_open()) {
            dst << src.rdbuf();
            dst.flush();
            bakOk = dst.good();
        }
        src.close();
        dst.close();
        if (bakOk) {
#ifdef _WIN32
            if (!MoveFileExW(bakTempPath.wstring().c_str(), backupPath.wstring().c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ENGINE_CORE_WARN("SaveManager: MoveFileExW failed for backup '{}' (error: {})",
                                 backupPath.string(), GetLastError());
            }
#else
            fs::rename(bakTempPath, backupPath, ec);
            if (ec) {
                ENGINE_CORE_WARN("SaveManager: Failed to rename backup '{}': {}", backupPath.string(), ec.message());
                ec.clear();
            }
#endif
        } else {
            ENGINE_CORE_WARN("SaveManager: Failed to stage atomic backup file '{}'", bakTempPath.string());
            fs::remove(bakTempPath, ec);
        }
    }

    // 3. Atomically replace primary with temp (WITHOUT deleting primary beforehand)
#ifdef _WIN32
    if (!MoveFileExW(tempPath.wstring().c_str(), primaryPath.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        ENGINE_CORE_ERROR("SaveManager: MoveFileExW failed to atomically replace '{}' with '{}' (error: {})",
                          primaryPath.string(), tempPath.string(), err);
        return false;
    }
#else
    fs::rename(tempPath, primaryPath, ec);
    if (ec) {
        ENGINE_CORE_ERROR("SaveManager: Failed to rename '{}' to '{}': {}",
                          tempPath.string(), primaryPath.string(), ec.message());
        return false;
    }
#endif

    ENGINE_CORE_INFO("SaveManager: Successfully saved profile slot {} to '{}' (CRC32: 0x{:08X})",
                     slotIndex, primaryPath.string(), crc);
    return true;
}

} // namespace

std::filesystem::path SaveManager::GetSaveDirectory(std::string_view appName) {
    if (!s_DirectoryOverride.empty()) {
        return s_DirectoryOverride;
    }

    char* prefPath = SDL_GetPrefPath("UmbralDevs", std::string(appName).c_str());
    if (prefPath) {
        std::filesystem::path p(prefPath);
        SDL_free(prefPath);
        return p;
    }

    return std::filesystem::current_path() / "saves";
}

void SaveManager::SetSaveDirectoryOverride(const std::filesystem::path& dir) {
    s_DirectoryOverride = dir;
}

void SaveManager::ClearSaveDirectoryOverride() {
    s_DirectoryOverride.clear();
}

bool SaveManager::SaveProfileAtomic(uint32_t slotIndex, const PlayerProfileData& data) {
    return WriteAndReplacePrimaryAtomic(slotIndex, data, /*backupCurrentPrimary=*/true);
}

bool SaveManager::LoadProfile(uint32_t slotIndex, PlayerProfileData& outData) {
    namespace fs = std::filesystem;
    const fs::path saveDir = GetSaveDirectory();
    const std::string baseName = "profile_" + std::to_string(slotIndex);
    const fs::path primaryPath = saveDir / (baseName + ".json");
    const fs::path backupPath = saveDir / (baseName + ".json.bak");

    // 1. Attempt primary load
    if (TryLoadFile(primaryPath, outData)) {
        ENGINE_CORE_INFO("SaveManager: Successfully loaded profile slot {} from '{}'", slotIndex, primaryPath.string());
        return true;
    }

    // 2. Attempt backup recovery
    ENGINE_CORE_WARN("SaveManager: Primary profile {} failed integrity check or missing, attempting backup recovery from '{}'...",
                     slotIndex, backupPath.string());
    if (TryLoadFile(backupPath, outData)) {
        ENGINE_CORE_INFO("SaveManager: Successfully recovered profile slot {} from backup '{}'!", slotIndex, backupPath.string());
        // Restore healthy primary from recovered data WITHOUT touching the backup file
        WriteAndReplacePrimaryAtomic(slotIndex, outData, /*backupCurrentPrimary=*/false);
        return true;
    }

    ENGINE_CORE_ERROR("SaveManager: Failed to load profile slot {} from both primary and backup files.", slotIndex);
    return false;
}

bool SaveManager::HasProfile(uint32_t slotIndex) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path saveDir = GetSaveDirectory();
    const std::string baseName = "profile_" + std::to_string(slotIndex);
    return fs::exists(saveDir / (baseName + ".json"), ec) || fs::exists(saveDir / (baseName + ".json.bak"), ec);
}

bool SaveManager::DeleteProfile(uint32_t slotIndex) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path saveDir = GetSaveDirectory();
    const std::string baseName = "profile_" + std::to_string(slotIndex);
    bool removedAny = false;
    removedAny |= fs::remove(saveDir / (baseName + ".json"), ec);
    removedAny |= fs::remove(saveDir / (baseName + ".json.bak"), ec);
    removedAny |= fs::remove(saveDir / (baseName + ".json.tmp"), ec);
    return removedAny;
}

void SaveManager::ApplyAudioAndVideoSettings(const PlayerProfileData& data) {
    for (size_t i = 0; i < data.BusVolumes.size() && i < static_cast<size_t>(AudioBus::Count); ++i) {
        AudioManager::SetBusVolume(static_cast<AudioBus>(i), data.BusVolumes[i]);
    }

    if (Application* app = Application::TryGet(); app && app->IsInitialized()) {
        app->GetWindow().SetFullscreen(data.Fullscreen);
        app->GetWindow().SetVSync(data.VSync);
    }

    if (!data.CustomActionBindings.is_null() && !data.CustomActionBindings.empty()) {
        Input::DeserializeBindingsFromString(data.CustomActionBindings.dump());
    }
}

void SaveManager::CaptureAudioAndVideoSettings(PlayerProfileData& outData) {
    for (size_t i = 0; i < outData.BusVolumes.size() && i < static_cast<size_t>(AudioBus::Count); ++i) {
        outData.BusVolumes[i] = AudioManager::GetBusVolume(static_cast<AudioBus>(i));
    }

    if (Application* app = Application::TryGet(); app && app->IsInitialized()) {
        outData.Fullscreen = app->GetWindow().IsFullscreen();
        outData.VSync = app->GetWindow().IsVSync();
    }

    const std::string bindingsStr = Input::SerializeBindingsToString();
    if (!bindingsStr.empty()) {
        try {
            outData.CustomActionBindings = nlohmann::json::parse(bindingsStr);
        } catch (const nlohmann::json::exception& e) {
            ENGINE_CORE_WARN("SaveManager: Failed to parse serialized action bindings: {}", e.what());
        }
    }
}

} // namespace Engine
