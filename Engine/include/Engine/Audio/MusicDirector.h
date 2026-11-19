#ifndef AUDIO_MUSICDIRECTOR_H
#define AUDIO_MUSICDIRECTOR_H

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Engine {

enum class CombatMusicTier : uint8_t {
    PitchBlackAmbience = 0,
    MediumTorchExploration,
    AshFrenzyCombat
};

enum class MusicMode : uint8_t {
    Stopped = 0,
    SynchronizedStems,
    SingleTrack
};

class MusicDirector {
public:
    static constexpr size_t kNumStems = 3;
    static constexpr float kDefaultSlewRate = 0.8f; // Gain change per second

    /// @brief Starts 3 sample-aligned streaming OGG stems simultaneously on AudioBus::Music.
    /// Stem 0: Pitch-Black Ambience | Stem 1: Medium Torch Exploration | Stem 2: 80%+ Ash Frenzy Combat
    static void PlaySynchronizedStems(const std::array<std::string, kNumStems>& oggFilepaths, float fadeInSec = 1.5f);

    /// @brief Starts 3 sample-aligned stems from in-memory OGG byte buffers (ideal for tests and embedded assets).
    static void PlaySynchronizedStemsFromMemory(
        const std::array<std::span<const uint8_t>, kNumStems>& oggBuffers,
        float fadeInSec = 1.5f,
        const std::array<std::string, kNumStems>& debugNames = { "stem_ambience", "stem_explore", "stem_frenzy" });

    /// @brief Plays a single looping music track on AudioBus::Music, crossfading from current music.
    static void PlaySingleTrack(const std::string& oggFilepath, float crossfadeSec = 1.5f);

    /// @brief Plays a single looping track from an in-memory OGG byte buffer.
    static void PlaySingleTrackFromMemory(std::span<const uint8_t> oggBuffer,
                                          float crossfadeSec = 1.5f,
                                          const std::string& debugName = "<memory>");

    /// @brief Evaluates combat music state (fuel & nearby enemy count), updates target weights, slews current weights,
    /// and refills audio buffers in sample-accurate lockstep.
    static void UpdateCombatState(float torchFuel01, uint32_t nearbyEnemyCount, float unscaledDt);

    /// @brief Per-frame update called by the engine loop; advances crossfades and lockstep stem refills.
    static void Update(float unscaledDt);

    /// @brief Smoothly fades out and stops all active music.
    static void Stop(float fadeOutSec = 1.0f);

    /// @brief Immediately shuts down all tracks and resets director state.
    static void Shutdown();

    // State Inspection & Configuration
    [[nodiscard]] static MusicMode GetMode() noexcept;
    [[nodiscard]] static CombatMusicTier GetCombatTier() noexcept;
    [[nodiscard]] static float GetTorchFuel() noexcept;
    [[nodiscard]] static uint32_t GetNearbyEnemyCount() noexcept;

    [[nodiscard]] static float GetStemWeight(size_t stemIndex) noexcept;
    [[nodiscard]] static float GetStemTargetWeight(size_t stemIndex) noexcept;
    [[nodiscard]] static float GetStemGain(size_t stemIndex) noexcept;
    [[nodiscard]] static uint64_t GetStemFrame(size_t stemIndex) noexcept;

    [[nodiscard]] static float GetSingleTrackWeight() noexcept;
    [[nodiscard]] static float GetSingleTrackGain() noexcept;

    [[nodiscard]] static bool IsPlayingStems() noexcept;
    [[nodiscard]] static bool IsPlayingSingleTrack() noexcept;

    static void SetSlewRate(float rate) noexcept;
    [[nodiscard]] static float GetSlewRate() noexcept;

    static void SetMasterMusicVolume(float volume01) noexcept;
    [[nodiscard]] static float GetMasterMusicVolume() noexcept;
};

} // namespace Engine

#endif // AUDIO_MUSICDIRECTOR_H
