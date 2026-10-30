#ifndef SANDBOX_CONTENTBROWSERPANEL_H
#define SANDBOX_CONTENTBROWSERPANEL_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
class Texture;
}

namespace Sandbox {

enum class DioramaDropArchetype {
    AutoDetect = 0,     // Infers archetype from asset filename (grass/bush, tree/pine, banner, decal, or standee)
    SwayingFoliage = 1, // Walkthrough grass/shrubs: high wind sway + interactive player parting, no solid collider
    PineOrTree = 2,     // Tall tree standee: moderate canopy sway + compact trunk BoxCollider2D
    RitualBanner = 3,   // Cloth standard: lively wind sway + base BoxCollider2D + SquashStretch spring
    SolidStandee = 4,   // Rigid stone/wood prop: zero wind sway + static BoxCollider2D
    FlatDecal = 5       // Laid flat on XZ ground plane (rugs, flagstones, floor markings)
};

class ContentBrowserPanel {
public:
    void SetRootDirectory(const std::filesystem::path& rootDirectory);
    void OnImGuiRender(bool* pOpen);
    void Refresh();

    DioramaDropArchetype GetDropArchetype() const { return m_DropArchetype; }
    void SetDropArchetype(DioramaDropArchetype archetype) { m_DropArchetype = archetype; }

private:
    struct CachedAssetEntry {
        std::filesystem::path Path;
        std::string Filename;
        std::string FilenameLower;
        std::string ItemPath;
        bool IsDirectory = false;
        bool IsImage = false;
        std::shared_ptr<Engine::Texture> Thumbnail;
    };

    void RefreshEntries();

    std::filesystem::path m_BaseDirectory;
    std::filesystem::path m_CurrentDirectory;
    std::filesystem::path m_LastScannedDirectory;
    std::vector<CachedAssetEntry> m_CachedEntries;
    bool m_NeedsRefresh = true;

    DioramaDropArchetype m_DropArchetype = DioramaDropArchetype::AutoDetect;
    float m_ThumbnailSize = 74.0f;
    char m_SearchFilter[128] = "";
    std::string m_LastStatusMessage;
};

} // namespace Sandbox

#endif // SANDBOX_CONTENTBROWSERPANEL_H
