#include "Engine/Data/GameplayData.h"
#include "Engine/Utils/Logger.h"

#include <filesystem>

namespace Engine {

namespace {

struct GameplayDataStorage {
    DataRegistry<RootDefinition> Roots;
    DataRegistry<BranchDefinition> Branches;
    DataRegistry<RuneDefinition> Runes;
    DataRegistry<WeaponDefinition> Weapons;
    DataRegistry<DropDefinition> Drops;
    DataRegistry<EnemyDefinition> Enemies;
    bool Initialized = false;
};

static GameplayDataStorage s_Storage;

} // namespace

bool GameplayData::Init(const std::string& dataDirectory) {
    std::filesystem::path dir(dataDirectory);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        ENGINE_CORE_ERROR("GameplayData::Init: Data directory does not exist: '{}'", dataDirectory);
        return false;
    }

    ENGINE_CORE_INFO("GameplayData::Init: Loading tables from '{}'...", dataDirectory);

    bool allOk = true;

    auto loadTable = [&](auto& registry, const std::string& filename, auto parser) {
        std::string fullPath = (dir / filename).string();
        if (std::filesystem::exists(fullPath, ec)) {
            if (!registry.LoadDirectoryOrFile(fullPath, parser)) {
                ENGINE_CORE_WARN("GameplayData::Init: Failed to load table '{}'", fullPath);
                allOk = false;
            }
        } else {
            ENGINE_CORE_WARN("GameplayData::Init: File not found: '{}'", fullPath);
            allOk = false;
        }
    };

    loadTable(s_Storage.Roots, "roots.json", ParseRootDefinition);
    loadTable(s_Storage.Branches, "branches.json", ParseBranchDefinition);
    loadTable(s_Storage.Runes, "runes.json", ParseRuneDefinition);
    loadTable(s_Storage.Weapons, "weapons.json", ParseWeaponDefinition);
    loadTable(s_Storage.Drops, "drops.json", ParseDropDefinition);
    loadTable(s_Storage.Enemies, "enemies.json", ParseEnemyDefinition);

    s_Storage.Initialized = true;
    ENGINE_CORE_INFO("GameplayData: Initialized. Roots={}, Branches={}, Runes={}, Weapons={}, Drops={}, Enemies={}",
                     s_Storage.Roots.Count(),
                     s_Storage.Branches.Count(),
                     s_Storage.Runes.Count(),
                     s_Storage.Weapons.Count(),
                     s_Storage.Drops.Count(),
                     s_Storage.Enemies.Count());
    return allOk;
}

void GameplayData::Shutdown() {
    s_Storage.Roots.Clear();
    s_Storage.Branches.Clear();
    s_Storage.Runes.Clear();
    s_Storage.Weapons.Clear();
    s_Storage.Drops.Clear();
    s_Storage.Enemies.Clear();
    s_Storage.Initialized = false;
    ENGINE_CORE_INFO("GameplayData: Shutdown complete.");
}

bool GameplayData::ReloadAll() {
    if (!s_Storage.Initialized) {
        ENGINE_CORE_WARN("GameplayData::ReloadAll: Not initialized");
        return false;
    }

    ENGINE_CORE_INFO("GameplayData::ReloadAll: Hot-reloading all data tables...");
    bool ok = true;
    ok &= s_Storage.Roots.ReloadAll();
    ok &= s_Storage.Branches.ReloadAll();
    ok &= s_Storage.Runes.ReloadAll();
    ok &= s_Storage.Weapons.ReloadAll();
    ok &= s_Storage.Drops.ReloadAll();
    ok &= s_Storage.Enemies.ReloadAll();

    ENGINE_CORE_INFO("GameplayData::ReloadAll: Reload finished (Roots={}, Branches={}, Runes={}, Weapons={}, Drops={}, Enemies={})",
                     s_Storage.Roots.Count(),
                     s_Storage.Branches.Count(),
                     s_Storage.Runes.Count(),
                     s_Storage.Weapons.Count(),
                     s_Storage.Drops.Count(),
                     s_Storage.Enemies.Count());
    return ok;
}

bool GameplayData::IsInitialized() noexcept {
    return s_Storage.Initialized;
}

const DataRegistry<RootDefinition>& GameplayData::Roots() noexcept {
    return s_Storage.Roots;
}

const DataRegistry<BranchDefinition>& GameplayData::Branches() noexcept {
    return s_Storage.Branches;
}

const DataRegistry<RuneDefinition>& GameplayData::Runes() noexcept {
    return s_Storage.Runes;
}

const DataRegistry<WeaponDefinition>& GameplayData::Weapons() noexcept {
    return s_Storage.Weapons;
}

const DataRegistry<DropDefinition>& GameplayData::Drops() noexcept {
    return s_Storage.Drops;
}

const DataRegistry<EnemyDefinition>& GameplayData::Enemies() noexcept {
    return s_Storage.Enemies;
}

const RootDefinition* GameplayData::FindRoot(std::string_view id) noexcept {
    return s_Storage.Roots.Find(id);
}

const BranchDefinition* GameplayData::FindBranch(std::string_view id) noexcept {
    return s_Storage.Branches.Find(id);
}

const RuneDefinition* GameplayData::FindRune(std::string_view id) noexcept {
    return s_Storage.Runes.Find(id);
}

const WeaponDefinition* GameplayData::FindWeapon(std::string_view id) noexcept {
    return s_Storage.Weapons.Find(id);
}

const DropDefinition* GameplayData::FindDrop(std::string_view id) noexcept {
    return s_Storage.Drops.Find(id);
}

const EnemyDefinition* GameplayData::FindEnemy(std::string_view id) noexcept {
    return s_Storage.Enemies.Find(id);
}

} // namespace Engine
