#include <gtest/gtest.h>

#include "Engine/Audio/AudioManager.h"
#include "Engine/Core/Input.h"
#include "Engine/Data/SaveManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Engine;

// Test 1: Full round-trip profile save and load with complete field verification
TEST(SaveManagerTests, SaveAndLoadProfileRoundTrip) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "SaveManagerTest_RoundTrip";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SaveManager::SetSaveDirectoryOverride(tempDir);

    PlayerProfileData original;
    original.SchemaVersion = 1;
    original.BankedAshes = 1500;
    original.TotalRuns = 4;
    original.BestSurvivalSeconds = 340;
    original.UnlockedRootRanks = { {"root_iron_sinew", 3}, {"root_anvil", 2} };
    original.DiscoveredRunes = { "rune_ignite", "rune_chronos" };
    original.RescuedCompanions = { "companion_owl" };
    original.BusVolumes = { 0.9f, 0.7f, 0.85f, 0.6f, 1.0f };
    original.Fullscreen = true;
    original.VSync = false;
    original.CustomActionBindings = { {"Attack", "Mouse0"}, {"Dodge", "Space"} };

    EXPECT_FALSE(SaveManager::HasProfile(0));
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, original));
    EXPECT_TRUE(SaveManager::HasProfile(0));
    EXPECT_TRUE(fs::exists(tempDir / "profile_0.json"));

    PlayerProfileData loaded;
    EXPECT_TRUE(SaveManager::LoadProfile(0, loaded));

    EXPECT_EQ(loaded.SchemaVersion, original.SchemaVersion);
    EXPECT_EQ(loaded.BankedAshes, original.BankedAshes);
    EXPECT_EQ(loaded.TotalRuns, original.TotalRuns);
    EXPECT_EQ(loaded.BestSurvivalSeconds, original.BestSurvivalSeconds);
    EXPECT_EQ(loaded.UnlockedRootRanks, original.UnlockedRootRanks);
    EXPECT_EQ(loaded.DiscoveredRunes, original.DiscoveredRunes);
    EXPECT_EQ(loaded.RescuedCompanions, original.RescuedCompanions);
    EXPECT_EQ(loaded.BusVolumes, original.BusVolumes);
    EXPECT_EQ(loaded.Fullscreen, original.Fullscreen);
    EXPECT_EQ(loaded.VSync, original.VSync);
    EXPECT_EQ(loaded.CustomActionBindings["Attack"], "Mouse0");
    EXPECT_EQ(loaded.CustomActionBindings["Dodge"], "Space");

    // Test DeleteProfile
    EXPECT_TRUE(SaveManager::DeleteProfile(0));
    EXPECT_FALSE(SaveManager::HasProfile(0));

    SaveManager::ClearSaveDirectoryOverride();
    fs::remove_all(tempDir, ec);
}

// Test 2: Primary file corruption recovers automatically from backup file
TEST(SaveManagerTests, AutomaticBackupRecoveryOnCorruption) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "SaveManagerTest_Recovery";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SaveManager::SetSaveDirectoryOverride(tempDir);

    // Save Version 1 (will become backup)
    PlayerProfileData v1;
    v1.BankedAshes = 1000;
    v1.TotalRuns = 1;
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, v1));

    // Save Version 2 (primary)
    PlayerProfileData v2;
    v2.BankedAshes = 2000;
    v2.TotalRuns = 2;
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, v2));

    EXPECT_TRUE(fs::exists(tempDir / "profile_0.json"));
    EXPECT_TRUE(fs::exists(tempDir / "profile_0.json.bak"));

    // Corrupt primary file mid-bytes (simulating sudden power loss or process kill)
    {
        std::ofstream corruptFile(tempDir / "profile_0.json", std::ios::trunc | std::ios::binary);
        corruptFile << "{\"CRC32\": 999999, \"Payload\": { \"BankedAshes\": 2000, \"Total";
    }

    // Load should detect corrupted JSON, log warning, and automatically recover from v1 (.bak)
    PlayerProfileData recovered;
    EXPECT_TRUE(SaveManager::LoadProfile(0, recovered));
    EXPECT_EQ(recovered.BankedAshes, 1000ULL);
    EXPECT_EQ(recovered.TotalRuns, 1u);

    // Verify primary file was re-saved and is now valid again
    PlayerProfileData reloaded;
    EXPECT_TRUE(SaveManager::LoadProfile(0, reloaded));
    EXPECT_EQ(reloaded.BankedAshes, 1000ULL);

    SaveManager::ClearSaveDirectoryOverride();
    fs::remove_all(tempDir, ec);
}

// Test 3: Checksum mismatch detects manual file tampering and falls back to backup
TEST(SaveManagerTests, ChecksumMismatchTamperDetection) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "SaveManagerTest_Tamper";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SaveManager::SetSaveDirectoryOverride(tempDir);

    PlayerProfileData v1;
    v1.BankedAshes = 500;
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, v1));

    PlayerProfileData v2;
    v2.BankedAshes = 600;
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, v2));

    // Tamper with primary file: modify payload value without recalculating CRC32
    {
        std::ifstream inFile(tempDir / "profile_0.json");
        nlohmann::json envelope;
        inFile >> envelope;
        inFile.close();

        // Illegitimately boost ashes to 999,999
        envelope["Payload"]["BankedAshes"] = 999999;

        std::ofstream outFile(tempDir / "profile_0.json", std::ios::trunc);
        outFile << envelope.dump(2);
    }

    // Load should fail CRC32 on primary and fall back to valid backup v1
    PlayerProfileData loaded;
    EXPECT_TRUE(SaveManager::LoadProfile(0, loaded));
    EXPECT_EQ(loaded.BankedAshes, 500ULL); // Restored from untouched .bak

    SaveManager::ClearSaveDirectoryOverride();
    fs::remove_all(tempDir, ec);
}

// Test 4: Audio and video settings application to AudioManager
TEST(SaveManagerTests, AudioAndVideoSettingsApplication) {
    AudioManager::Init();

    PlayerProfileData profile;
    profile.BusVolumes = { 0.75f, 0.60f, 0.80f, 0.50f, 0.90f };

    SaveManager::ApplyAudioAndVideoSettings(profile);

    EXPECT_FLOAT_EQ(AudioManager::GetBusVolume(AudioBus::Master), 0.75f);
    EXPECT_FLOAT_EQ(AudioManager::GetBusVolume(AudioBus::Music), 0.60f);
    EXPECT_FLOAT_EQ(AudioManager::GetBusVolume(AudioBus::SFX), 0.80f);
    EXPECT_FLOAT_EQ(AudioManager::GetBusVolume(AudioBus::Ambience), 0.50f);
    EXPECT_FLOAT_EQ(AudioManager::GetBusVolume(AudioBus::UI), 0.90f);

    PlayerProfileData captured;
    SaveManager::CaptureAudioAndVideoSettings(captured);
    EXPECT_FLOAT_EQ(captured.BusVolumes[0], 0.75f);
    EXPECT_FLOAT_EQ(captured.BusVolumes[1], 0.60f);
    EXPECT_FLOAT_EQ(captured.BusVolumes[2], 0.80f);
    EXPECT_FLOAT_EQ(captured.BusVolumes[3], 0.50f);
    EXPECT_FLOAT_EQ(captured.BusVolumes[4], 0.90f);

    AudioManager::Shutdown();
}

// Test 5: SceneManager async fade transition state machine and scene swap
TEST(SaveManagerTests, SceneManagerAsyncFadeTransition) {
    auto sceneA = std::make_shared<Scene>("SanctuaryHub");
    SceneManager::LoadScene(sceneA);

    EXPECT_EQ(SceneManager::GetActiveScene()->GetName(), "SanctuaryHub");
    EXPECT_FALSE(SceneManager::IsTransitioning());
    EXPECT_FLOAT_EQ(SceneManager::GetFadeAlpha(), 0.0f);
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::None);

    bool sceneBInstantiated = false;
    SceneManager::TransitionToScene([&]() {
        sceneBInstantiated = true;
        return std::make_shared<Scene>("ProceduralRun");
    }, 0.4f);

    EXPECT_TRUE(SceneManager::IsTransitioning());
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::FadeOut);

    // Step 0.1s (half duration is 0.2s): should be mid-fade out
    SceneManager::UpdateTransition(0.1f);
    EXPECT_TRUE(SceneManager::IsTransitioning());
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::FadeOut);
    EXPECT_NEAR(SceneManager::GetFadeAlpha(), 0.5f, 0.05f);
    EXPECT_FALSE(sceneBInstantiated);
    EXPECT_EQ(SceneManager::GetActiveScene()->GetName(), "SanctuaryHub");

    // Step 0.12s (total 0.22s >= 0.2s): peak blackout reached, swap happens!
    SceneManager::UpdateTransition(0.12f);
    EXPECT_TRUE(sceneBInstantiated);
    ASSERT_NE(SceneManager::GetActiveScene(), nullptr);
    EXPECT_EQ(SceneManager::GetActiveScene()->GetName(), "ProceduralRun");
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::FadeIn);
    EXPECT_NEAR(SceneManager::GetFadeAlpha(), 0.9f, 0.15f);

    // Step 0.1s: fading back in
    SceneManager::UpdateTransition(0.1f);
    EXPECT_TRUE(SceneManager::IsTransitioning());
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::FadeIn);

    // Step 0.15s: transition fully finishes
    SceneManager::UpdateTransition(0.15f);
    EXPECT_FALSE(SceneManager::IsTransitioning());
    EXPECT_EQ(SceneManager::GetTransitionState(), SceneTransitionState::None);
    EXPECT_FLOAT_EQ(SceneManager::GetFadeAlpha(), 0.0f);
    EXPECT_EQ(SceneManager::GetActiveScene()->GetName(), "ProceduralRun");

    SceneManager::Shutdown();
}

// Test 6: Backup recovery heals primary file without overwriting or destroying valid .bak
TEST(SaveManagerTests, BackupRecoveryHealsPrimaryWithoutOverwritingBackup) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "SaveManagerTest_NonDestructiveBackup";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SaveManager::SetSaveDirectoryOverride(tempDir);

    // Save initial healthy profile v1 (BankedAshes = 4242)
    PlayerProfileData v1;
    v1.BankedAshes = 4242;
    v1.TotalRuns = 12;
    v1.BestSurvivalSeconds = 999;
    EXPECT_TRUE(SaveManager::SaveProfileAtomic(0, v1));

    const fs::path primaryPath = tempDir / "profile_0.json";
    const fs::path backupPath = tempDir / "profile_0.json.bak";

    // Copy primary to backup manually so backup is guaranteed valid v1
    fs::copy_file(primaryPath, backupPath, fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec);

    // Read initial backup content and size
    const auto backupSizeInitial = fs::file_size(backupPath, ec);
    ASSERT_GT(backupSizeInitial, 0u);

    // Corrupt primary file with truncated / invalid payload
    {
        std::ofstream corruptOut(primaryPath, std::ios::trunc | std::ios::binary);
        corruptOut << "{\"CRC32\": 12345678, \"Payload\": { \"BankedAshes\": 999999, \"TotalRuns\": ";
    }

    // Call LoadProfile: must detect corrupt primary, fall back to .bak, and heal primary WITHOUT corrupting .bak
    PlayerProfileData recovered;
    EXPECT_TRUE(SaveManager::LoadProfile(0, recovered));
    EXPECT_EQ(recovered.BankedAshes, 4242ULL);
    EXPECT_EQ(recovered.TotalRuns, 12u);
    EXPECT_EQ(recovered.BestSurvivalSeconds, 999u);

    // Assert BOTH primary and backup files exist
    EXPECT_TRUE(fs::exists(primaryPath, ec));
    EXPECT_TRUE(fs::exists(backupPath, ec));

    // Assert backup file was NOT overwritten with corrupt data
    const auto backupSizeAfter = fs::file_size(backupPath, ec);
    EXPECT_EQ(backupSizeAfter, backupSizeInitial);

    // Verify backup file can still be parsed directly
    PlayerProfileData backupLoaded;
    EXPECT_TRUE(SaveManager::LoadProfile(0, backupLoaded));
    EXPECT_EQ(backupLoaded.BankedAshes, 4242ULL);

    // Overwrite backup with garbage and verify healed primary still loads fine
    {
        std::ofstream corruptBak(backupPath, std::ios::trunc | std::ios::binary);
        corruptBak << "GARBAGE";
    }
    PlayerProfileData primaryLoaded;
    EXPECT_TRUE(SaveManager::LoadProfile(0, primaryLoaded));
    EXPECT_EQ(primaryLoaded.BankedAshes, 4242ULL);

    SaveManager::ClearSaveDirectoryOverride();
    fs::remove_all(tempDir, ec);
}

// Test 7: Atomic replace never leaves primary missing at any point
TEST(SaveManagerTests, AtomicReplaceNeverLeavesMissingPrimary) {
    namespace fs = std::filesystem;
    const fs::path tempDir = fs::temp_directory_path() / "SaveManagerTest_AtomicReplace";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    SaveManager::SetSaveDirectoryOverride(tempDir);

    const fs::path primaryPath = tempDir / "profile_1.json";

    for (uint32_t i = 1; i <= 20; ++i) {
        PlayerProfileData data;
        data.BankedAshes = i * 100;
        data.TotalRuns = i;

        EXPECT_TRUE(SaveManager::SaveProfileAtomic(1, data));
        EXPECT_TRUE(fs::exists(primaryPath, ec));
        EXPECT_TRUE(fs::is_regular_file(primaryPath, ec));

        PlayerProfileData verifyLoad;
        EXPECT_TRUE(SaveManager::LoadProfile(1, verifyLoad));
        EXPECT_EQ(verifyLoad.BankedAshes, static_cast<uint64_t>(i * 100));
        EXPECT_EQ(verifyLoad.TotalRuns, i);
    }

    SaveManager::ClearSaveDirectoryOverride();
    fs::remove_all(tempDir, ec);
}

// Test 8: Custom action bindings and window settings synchronization
TEST(SaveManagerTests, ApplyAndCaptureSettingsBindsWindowAndInput) {
    Input::Init();

    // Set custom action bindings
    ActionBinding dodgeBinding;
    dodgeBinding.PrimaryKey = SDL_SCANCODE_SPACE;
    dodgeBinding.PadButton = SDL_GAMEPAD_BUTTON_SOUTH;
    Input::BindAction("DodgeRoll", dodgeBinding);

    ActionBinding attackBinding;
    attackBinding.MouseButton = SDL_BUTTON_LEFT;
    attackBinding.PadTriggerAxis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    Input::BindAction("SlashAttack", attackBinding);

    PlayerProfileData profile;
    SaveManager::CaptureAudioAndVideoSettings(profile);

    // Action bindings should be serialized into CustomActionBindings JSON
    EXPECT_FALSE(profile.CustomActionBindings.is_null());
    EXPECT_FALSE(profile.CustomActionBindings.empty());
    EXPECT_TRUE(profile.CustomActionBindings.contains("bindings"));
    EXPECT_TRUE(profile.CustomActionBindings["bindings"].contains("DodgeRoll"));
    EXPECT_TRUE(profile.CustomActionBindings["bindings"].contains("SlashAttack"));

    // Clear bindings in Input
    Input::ClearActions();
    EXPECT_EQ(Input::GetActionBinding("DodgeRoll"), nullptr);
    EXPECT_EQ(Input::GetActionBinding("SlashAttack"), nullptr);

    // Apply settings back
    SaveManager::ApplyAudioAndVideoSettings(profile);

    const ActionBinding* restoredDodge = Input::GetActionBinding("DodgeRoll");
    ASSERT_NE(restoredDodge, nullptr);
    EXPECT_EQ(restoredDodge->PrimaryKey, SDL_SCANCODE_SPACE);
    EXPECT_EQ(restoredDodge->PadButton, SDL_GAMEPAD_BUTTON_SOUTH);

    const ActionBinding* restoredAttack = Input::GetActionBinding("SlashAttack");
    ASSERT_NE(restoredAttack, nullptr);
    EXPECT_EQ(restoredAttack->MouseButton, SDL_BUTTON_LEFT);
    EXPECT_EQ(restoredAttack->PadTriggerAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    Input::Shutdown();
}
