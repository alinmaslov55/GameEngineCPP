#ifndef DATA_GAMEPLAYDATA_H
#define DATA_GAMEPLAYDATA_H

#include "Engine/Data/DataRegistry.h"
#include "Engine/Data/GameplayDefinitions.h"

#include <string>
#include <string_view>

namespace Engine {

class GameplayData {
public:
    static bool Init(const std::string& dataDirectory = "Sandbox/assets/data");
    static void Shutdown();
    static bool ReloadAll();

    [[nodiscard]] static bool IsInitialized() noexcept;

    // Registries
    [[nodiscard]] static const DataRegistry<RootDefinition>& Roots() noexcept;
    [[nodiscard]] static const DataRegistry<BranchDefinition>& Branches() noexcept;
    [[nodiscard]] static const DataRegistry<RuneDefinition>& Runes() noexcept;
    [[nodiscard]] static const DataRegistry<WeaponDefinition>& Weapons() noexcept;
    [[nodiscard]] static const DataRegistry<DropDefinition>& Drops() noexcept;
    [[nodiscard]] static const DataRegistry<EnemyDefinition>& Enemies() noexcept;

    // Direct convenient lookups
    [[nodiscard]] static const RootDefinition* FindRoot(std::string_view id) noexcept;
    [[nodiscard]] static const BranchDefinition* FindBranch(std::string_view id) noexcept;
    [[nodiscard]] static const RuneDefinition* FindRune(std::string_view id) noexcept;
    [[nodiscard]] static const WeaponDefinition* FindWeapon(std::string_view id) noexcept;
    [[nodiscard]] static const DropDefinition* FindDrop(std::string_view id) noexcept;
    [[nodiscard]] static const EnemyDefinition* FindEnemy(std::string_view id) noexcept;
};

} // namespace Engine

#endif // DATA_GAMEPLAYDATA_H
