#include <gtest/gtest.h>

#include "Engine/Data/DataRegistry.h"
#include "Engine/Data/GameplayData.h"
#include "Engine/Data/GameplayDefinitions.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/Components/PointLightComponent.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Scene.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Engine;

// Test 1: Load from memory and find items, verifying order and field parsing
TEST(DataRegistryTests, LoadFromMemoryAndFind) {
    const std::string jsonContent = R"([
        {
            "Id": "root_iron_sinew",
            "Name": "Iron Sinew",
            "Description": "Increases maximum health.",
            "Category": "Physical",
            "MaxRank": 5,
            "BaseCostAshes": 100,
            "CostMultiplier": 1.5,
            "StatModifier": "MaxHealth",
            "ModifierPerRank": 20.0,
            "Icon": "tex_icon_root_health"
        },
        {
            "Id": "root_heavy_hand",
            "Name": "Heavy Hand",
            "Description": "Increases base melee damage.",
            "Category": "Physical",
            "MaxRank": 5,
            "BaseCostAshes": 120,
            "CostMultiplier": 1.5,
            "StatModifier": "BaseDamage",
            "ModifierPerRank": 5.0,
            "Icon": "tex_icon_root_damage"
        },
        {
            "Id": "root_stone_skin",
            "Name": "Stone Skin",
            "Description": "Reduces damage taken.",
            "Category": "Physical",
            "MaxRank": 5,
            "BaseCostAshes": 150,
            "CostMultiplier": 1.6,
            "StatModifier": "DamageReduction",
            "ModifierPerRank": 0.04,
            "Icon": "tex_icon_root_armor"
        },
        {
            "Id": "root_battering_ram",
            "Name": "Battering Ram",
            "Description": "Increases knockback impulse.",
            "Category": "Physical",
            "MaxRank": 5,
            "BaseCostAshes": 110,
            "CostMultiplier": 1.4,
            "StatModifier": "KnockbackImpulse",
            "ModifierPerRank": 3.0,
            "Icon": "tex_icon_root_knockback"
        },
        {
            "Id": "root_anvil",
            "Name": "Anvil Mastery",
            "Description": "Decreases repair costs.",
            "Category": "Physical",
            "MaxRank": 5,
            "BaseCostAshes": 130,
            "CostMultiplier": 1.5,
            "StatModifier": "RepairEfficiency",
            "ModifierPerRank": 0.15,
            "Icon": "tex_icon_root_anvil"
        }
    ])";

    DataRegistry<RootDefinition> registry;
    EXPECT_TRUE(registry.LoadFromString(jsonContent, ParseRootDefinition));
    EXPECT_EQ(registry.Count(), 5u);
    EXPECT_FALSE(registry.Empty());

    const auto* anvil = registry.Find("root_anvil");
    ASSERT_NE(anvil, nullptr);
    EXPECT_EQ(anvil->Id, "root_anvil");
    EXPECT_EQ(anvil->Name, "Anvil Mastery");
    EXPECT_EQ(anvil->Description, "Decreases repair costs.");
    EXPECT_EQ(anvil->Category, "Physical");
    EXPECT_EQ(anvil->MaxRank, 5u);
    EXPECT_EQ(anvil->BaseCostAshes, 130u);
    EXPECT_FLOAT_EQ(anvil->CostMultiplier, 1.5f);
    EXPECT_EQ(anvil->StatModifier, "RepairEfficiency");
    EXPECT_FLOAT_EQ(anvil->ModifierPerRank, 0.15f);
    EXPECT_EQ(anvil->Icon, "tex_icon_root_anvil");

    EXPECT_EQ(registry.Find("non_existent_id"), nullptr);

    // Verify ordering
    const auto& all = registry.GetAll();
    ASSERT_EQ(all.size(), 5u);
    EXPECT_EQ(all[0].Id, "root_iron_sinew");
    EXPECT_EQ(all[1].Id, "root_heavy_hand");
    EXPECT_EQ(all[2].Id, "root_stone_skin");
    EXPECT_EQ(all[3].Id, "root_battering_ram");
    EXPECT_EQ(all[4].Id, "root_anvil");
}

// Test 2: Schema validation skips malformed entries gracefully without crashing
TEST(DataRegistryTests, SchemaValidationGracefulSkip) {
    const std::string malformedJson = R"([
        {
            "Id": "weapon_valid_1",
            "Name": "Valid Mace",
            "WeaponType": "Maul",
            "Tier": 1,
            "Damage": 30.0
        },
        {
            "MissingId": true,
            "Name": "Bad Weapon Without Id"
        },
        "A string that is not an object",
        {
            "Id": "weapon_valid_2",
            "Name": "Valid Pike",
            "WeaponType": "Halberd",
            "Tier": 2,
            "Damage": 45.0
        },
        {
            "Id": "",
            "Name": "Empty String Id"
        },
        {
            "Id": "weapon_valid_3",
            "Name": "Valid Dagger",
            "WeaponType": "Sickle",
            "Tier": 3,
            "Damage": 20.0
        }
    ])";

    DataRegistry<WeaponDefinition> registry;
    EXPECT_TRUE(registry.LoadFromString(malformedJson, ParseWeaponDefinition));
    EXPECT_EQ(registry.Count(), 3u);

    EXPECT_NE(registry.Find("weapon_valid_1"), nullptr);
    EXPECT_NE(registry.Find("weapon_valid_2"), nullptr);
    EXPECT_NE(registry.Find("weapon_valid_3"), nullptr);
    EXPECT_EQ(registry.Find("Bad Weapon Without Id"), nullptr);
}

// Test 3: ReloadAll reloads modified data from disk
TEST(DataRegistryTests, ReloadAllHotReload) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "DataRegistryTest_Reload";
    fs::create_directories(tempDir);
    const fs::path testFile = tempDir / "test_weapons.json";

    auto writeJson = [&](float damageVal) {
        std::ofstream out(testFile);
        out << "[\n"
            << "  {\n"
            << "    \"Id\": \"test_maul\",\n"
            << "    \"Name\": \"Testing Maul\",\n"
            << "    \"Damage\": " << damageVal << "\n"
            << "  }\n"
            << "]\n";
    };

    writeJson(25.0f);

    DataRegistry<WeaponDefinition> registry;
    EXPECT_TRUE(registry.LoadDirectoryOrFile(testFile.string(), ParseWeaponDefinition));
    EXPECT_EQ(registry.Count(), 1u);

    const auto* initialDef = registry.Find("test_maul");
    ASSERT_NE(initialDef, nullptr);
    EXPECT_FLOAT_EQ(initialDef->Damage, 25.0f);

    // Modify file on disk and call ReloadAll
    writeJson(42.5f);
    EXPECT_TRUE(registry.ReloadAll());

    const auto* reloadedDef = registry.Find("test_maul");
    ASSERT_NE(reloadedDef, nullptr);
    EXPECT_FLOAT_EQ(reloadedDef->Damage, 42.5f);

    // Cleanup
    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

// Test 4: Directory loading aggregates multiple JSON files
TEST(DataRegistryTests, DirectoryLoadingAggregatesMultipleFiles) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "DataRegistryTest_Directory";
    fs::create_directories(tempDir);

    {
        std::ofstream fileA(tempDir / "weapons_a.json");
        fileA << R"([
            { "Id": "wpn_a1", "Name": "Weapon A1", "Damage": 10.0 },
            { "Id": "wpn_a2", "Name": "Weapon A2", "Damage": 12.0 }
        ])";
    }

    {
        std::ofstream fileB(tempDir / "weapons_b.json");
        fileB << R"([
            { "Id": "wpn_b1", "Name": "Weapon B1", "Damage": 20.0 }
        ])";
    }

    DataRegistry<WeaponDefinition> registry;
    EXPECT_TRUE(registry.LoadDirectoryOrFile(tempDir.string(), ParseWeaponDefinition));
    EXPECT_EQ(registry.Count(), 3u);

    EXPECT_NE(registry.Find("wpn_a1"), nullptr);
    EXPECT_NE(registry.Find("wpn_a2"), nullptr);
    EXPECT_NE(registry.Find("wpn_b1"), nullptr);

    // Cleanup
    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

// Test 5: GameplayData static manager loads all production JSON tables and provides lookups
TEST(DataRegistryTests, GameplayDataRegistriesLoadAllRealAssets) {
    EXPECT_TRUE(GameplayData::Init("Sandbox/assets/data"));
    EXPECT_TRUE(GameplayData::IsInitialized());

    // 1. Roots (16 nodes)
    EXPECT_EQ(GameplayData::Roots().Count(), 16u);
    const auto* rootAnvil = GameplayData::FindRoot("root_anvil");
    ASSERT_NE(rootAnvil, nullptr);
    EXPECT_EQ(rootAnvil->Name, "Anvil Mastery");
    EXPECT_EQ(rootAnvil->StatModifier, "RepairEfficiency");
    EXPECT_EQ(rootAnvil->MaxRank, 5u);

    // 2. Branches (24 perks)
    EXPECT_EQ(GameplayData::Branches().Count(), 24u);
    const auto* branchAvatar = GameplayData::FindBranch("branch_avatar_of_ash");
    ASSERT_NE(branchAvatar, nullptr);
    EXPECT_EQ(branchAvatar->Rarity, "Legendary");
    EXPECT_EQ(branchAvatar->StatModifier, "AshFrenzyBoost");

    // 3. Runes (4 spells)
    EXPECT_EQ(GameplayData::Runes().Count(), 4u);
    const auto* runeChronos = GameplayData::FindRune("rune_chronos");
    ASSERT_NE(runeChronos, nullptr);
    EXPECT_FLOAT_EQ(runeChronos->FuelCost, 20.0f);
    EXPECT_FLOAT_EQ(runeChronos->Radius, 7.0f);

    // 4. Weapons (9 entries)
    EXPECT_EQ(GameplayData::Weapons().Count(), 9u);
    const auto* weaponMaulT3 = GameplayData::FindWeapon("weapon_maul_t3");
    ASSERT_NE(weaponMaulT3, nullptr);
    EXPECT_EQ(weaponMaulT3->Tier, 3u);
    EXPECT_FLOAT_EQ(weaponMaulT3->Damage, 78.0f);

    // 5. Drops (4 structures)
    EXPECT_EQ(GameplayData::Drops().Count(), 4u);
    const auto* dropBarricade = GameplayData::FindDrop("drop_barricade");
    ASSERT_NE(dropBarricade, nullptr);
    EXPECT_EQ(dropBarricade->WoodCost, 4u);
    EXPECT_FLOAT_EQ(dropBarricade->Health, 120.0f);

    // 6. Enemies (5 archetypes)
    EXPECT_EQ(GameplayData::Enemies().Count(), 5u);
    const auto* enemyWendigo = GameplayData::FindEnemy("enemy_wendigo");
    ASSERT_NE(enemyWendigo, nullptr);
    EXPECT_EQ(enemyWendigo->Archetype, "EliteStalker");
    EXPECT_FLOAT_EQ(enemyWendigo->Health, 180.0f);

    // Test ReloadAll on production assets
    EXPECT_TRUE(GameplayData::ReloadAll());
    EXPECT_EQ(GameplayData::Roots().Count(), 16u);
    EXPECT_EQ(GameplayData::Branches().Count(), 24u);
    EXPECT_EQ(GameplayData::Runes().Count(), 4u);
    EXPECT_EQ(GameplayData::Weapons().Count(), 9u);
    EXPECT_EQ(GameplayData::Drops().Count(), 4u);
    EXPECT_EQ(GameplayData::Enemies().Count(), 5u);

    // Test Shutdown cleans up
    GameplayData::Shutdown();
    EXPECT_FALSE(GameplayData::IsInitialized());
    EXPECT_TRUE(GameplayData::Roots().Empty());
    EXPECT_TRUE(GameplayData::Branches().Empty());
    EXPECT_TRUE(GameplayData::Runes().Empty());
    EXPECT_TRUE(GameplayData::Weapons().Empty());
    EXPECT_TRUE(GameplayData::Drops().Empty());
    EXPECT_TRUE(GameplayData::Enemies().Empty());
}

// Test 6: Single-object JSON with array fields parses correctly
TEST(DataRegistryTests, SingleObjectWithArrayFieldParsesCorrectly) {
    const std::string singleObjectJson = R"({
        "Id": "branch_fire_blade",
        "Name": "Fire Blade",
        "Description": "Infuses blade with fiery embers.",
        "Rarity": "Rare",
        "StatModifier": "FireDamage",
        "Value": 25.0,
        "Icon": "tex_icon_fire_blade",
        "Tags": ["Fire", "Melee", "Blade"]
    })";

    DataRegistry<BranchDefinition> registry;
    EXPECT_TRUE(registry.LoadFromString(singleObjectJson, ParseBranchDefinition));
    EXPECT_EQ(registry.Count(), 1u);

    const auto* def = registry.Find("branch_fire_blade");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->Id, "branch_fire_blade");
    EXPECT_EQ(def->Name, "Fire Blade");
    EXPECT_EQ(def->Rarity, "Rare");
    EXPECT_EQ(def->StatModifier, "FireDamage");
    EXPECT_FLOAT_EQ(def->Value, 25.0f);
    EXPECT_EQ(def->Icon, "tex_icon_fire_blade");
    ASSERT_EQ(def->Tags.size(), 3u);
    EXPECT_EQ(def->Tags[0], "Fire");
    EXPECT_EQ(def->Tags[1], "Melee");
    EXPECT_EQ(def->Tags[2], "Blade");
}

// Test 7: Directory loading deterministically sorts files alphabetically
TEST(DataRegistryTests, DirectoryIterationLoadsAlphabeticallySorted) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "DataRegistryTest_AlphabeticalSort";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    // Create 3 files out of alphabetical order
    {
        std::ofstream outC(tempDir / "c_items.json");
        outC << R"({ "Id": "item_c", "Name": "Item C", "Damage": 30.0 })";
    }
    {
        std::ofstream outA(tempDir / "a_items.json");
        outA << R"({ "Id": "item_a", "Name": "Item A", "Damage": 10.0 })";
    }
    {
        std::ofstream outB(tempDir / "b_items.json");
        outB << R"({ "Id": "item_b", "Name": "Item B", "Damage": 20.0 })";
    }

    DataRegistry<WeaponDefinition> registry;
    EXPECT_TRUE(registry.LoadDirectoryOrFile(tempDir.string(), ParseWeaponDefinition));
    ASSERT_EQ(registry.Count(), 3u);

    const auto& all = registry.GetAll();
    EXPECT_EQ(all[0].Id, "item_a");
    EXPECT_EQ(all[1].Id, "item_b");
    EXPECT_EQ(all[2].Id, "item_c");

    fs::remove_all(tempDir, ec);
}

// Test 8: Relative virtual paths resolved via ResourceManager
TEST(DataRegistryTests, VirtualPathResolutionViaResourceManager) {
    DataRegistry<RootDefinition> registry;
    EXPECT_TRUE(registry.LoadDirectoryOrFile("Sandbox/assets/data/roots.json", ParseRootDefinition));
    EXPECT_EQ(registry.Count(), 16u);

    const auto* ironSinew = registry.Find("root_iron_sinew");
    ASSERT_NE(ironSinew, nullptr);
    EXPECT_EQ(ironSinew->Name, "Iron Sinew");
}

// Test 9: Transparent heterogeneous lookup works with string_view and literals
TEST(DataRegistryTests, TransparentStringViewLookupNoAllocation) {
    const std::string jsonContent = R"([
        {
            "Id": "very_long_identifier_exceeding_small_string_optimization_threshold",
            "Name": "Long ID Weapon",
            "Damage": 50.0
        }
    ])";

    DataRegistry<WeaponDefinition> registry;
    EXPECT_TRUE(registry.LoadFromString(jsonContent, ParseWeaponDefinition));

    // Lookup using string_view without allocation
    std::string_view svId = "very_long_identifier_exceeding_small_string_optimization_threshold";
    const auto* def = registry.Find(svId);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->Name, "Long ID Weapon");
    EXPECT_FLOAT_EQ(def->Damage, 50.0f);

    // Lookup using const char* string literal
    EXPECT_NE(registry.Find("very_long_identifier_exceeding_small_string_optimization_threshold"), nullptr);
    EXPECT_EQ(registry.Find("non_existent_long_identifier_string_view_lookup"), nullptr);
}

// Test 10: Every PrefabPath in Sandbox/assets/data/drops.json resolves to an existing, loadable .prefab
TEST(DataRegistryTests, DropPrefabPathsResolveAndLoad) {
    ASSERT_TRUE(GameplayData::Init("Sandbox/assets/data"));
    const auto& drops = GameplayData::Drops().GetAll();
    ASSERT_FALSE(drops.empty());

    Engine::Scene testScene("DropPrefabVerifyScene");
    const std::filesystem::path assetsRoot("Sandbox/assets");
    for (const auto& drop : drops) {
        ASSERT_FALSE(drop.PrefabPath.empty()) << "Drop '" << drop.Id << "' has empty PrefabPath";
        const std::filesystem::path fullPath = assetsRoot / drop.PrefabPath;
        EXPECT_TRUE(std::filesystem::exists(fullPath)) << "Missing prefab file: " << fullPath.string();
        auto prefab = Prefab::LoadFromFile(fullPath.string());
        ASSERT_NE(prefab, nullptr) << "Failed to load prefab: " << fullPath.string();
        EXPECT_TRUE(prefab->IsValid()) << "Invalid prefab JSON: " << fullPath.string();

        auto* go = testScene.InstantiatePrefab(*prefab, { 1.0f, 0.0f, 2.0f });
        ASSERT_NE(go, nullptr) << "Failed to instantiate prefab: " << fullPath.string();
        EXPECT_NE(go->GetComponent<Engine::BoxCollider2D>(), nullptr) << "Missing BoxCollider2D on " << drop.Id;
        if (drop.Id == "drop_beacon") {
            auto* pl = go->GetComponent<Engine::PointLightComponent>();
            ASSERT_NE(pl, nullptr) << "drop_beacon spawned without PointLightComponent";
            EXPECT_EQ(pl->Semantic, Engine::LightSemantic::SafeLifeline);
            EXPECT_EQ(pl->Priority, Engine::LightPriority::High_Tactical);
        }
    }

    GameplayData::Shutdown();
}

TEST(DataRegistryTests, GameplayDataInitMissingDirectoryLeavesInitializedFalse) {
    GameplayData::Shutdown();
    EXPECT_FALSE(GameplayData::Init("non_existent_gameplay_data_dir_xyz"));
    EXPECT_FALSE(GameplayData::IsInitialized());
}
