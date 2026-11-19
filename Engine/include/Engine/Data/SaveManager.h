#ifndef DATA_SAVEMANAGER_H
#define DATA_SAVEMANAGER_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

struct PlayerProfileData {
    uint32_t SchemaVersion = 1;
    uint64_t BankedAshes = 0;
    uint32_t TotalRuns = 0;
    uint32_t BestSurvivalSeconds = 0;
    std::unordered_map<std::string, uint32_t> UnlockedRootRanks; // 16 Physical/Spiritual Roots
    std::vector<std::string> DiscoveredRunes;
    std::vector<std::string> RescuedCompanions;
    // User Settings:
    std::array<float, 5> BusVolumes{ 1.0f, 0.8f, 1.0f, 0.85f, 1.0f }; // Master, Music, SFX, Ambience, UI
    bool Fullscreen = false;
    bool VSync = true;
    nlohmann::json CustomActionBindings;
};

class Checksum {
public:
    [[nodiscard]] static uint32_t CRC32(std::string_view data) noexcept {
        static constexpr auto table = []() constexpr {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (size_t j = 0; j < 8; ++j) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                t[i] = c;
            }
            return t;
        }();

        uint32_t crc = 0xFFFFFFFFu;
        for (char ch : data) {
            const uint8_t byte = static_cast<uint8_t>(ch);
            crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
};

class SaveManager {
public:
    [[nodiscard]] static std::filesystem::path GetSaveDirectory(std::string_view appName = "UmbralPhysicsBrawler");
    static void SetSaveDirectoryOverride(const std::filesystem::path& dir);
    static void ClearSaveDirectoryOverride();

    static bool SaveProfileAtomic(uint32_t slotIndex, const PlayerProfileData& data);
    static bool LoadProfile(uint32_t slotIndex, PlayerProfileData& outData);
    [[nodiscard]] static bool HasProfile(uint32_t slotIndex);
    static bool DeleteProfile(uint32_t slotIndex);

    static void ApplyAudioAndVideoSettings(const PlayerProfileData& data);
    static void CaptureAudioAndVideoSettings(PlayerProfileData& outData);
};

} // namespace Engine

#endif // DATA_SAVEMANAGER_H
