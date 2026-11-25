#ifndef AUDIO_AUDIOMANAGER_H
#define AUDIO_AUDIOMANAGER_H

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <vector>

namespace Engine {

class AudioClip;
class AudioStreamTrack;

enum class AudioBus : uint8_t {
    Master = 0,
    Music,
    SFX,
    Ambience,
    UI,
    Count
};

struct SoundPlayParams {
    AudioBus Bus = AudioBus::SFX;
    float Volume = 1.0f;
    float PitchMin = 0.94f;
    float PitchMax = 1.06f; // Randomized via SDL_SetAudioStreamFrequencyRatio
    bool Loop = false;
    bool SpatializedXZ = false;
    glm::vec3 WorldPos{ 0.0f };
    float MinDistance = 3.0f;
    float MaxDistance = 24.0f;
    uint8_t Priority = 128; // Higher = harder to steal. SFX default 128, UI 255
};

using VoiceHandle = uint32_t;
constexpr VoiceHandle kInvalidVoiceHandle = 0;

namespace SpatialAudio {

/// @brief Computes distance attenuation using smooth quadratic roll-off:
/// atten = clamp(1.0 - (dist - minD) / (maxD - minD), 0.0, 1.0)^2
[[nodiscard]] inline float CalculateDistanceAttenuation(
    float distanceXZ,
    float minDistance,
    float maxDistance) noexcept
{
    if (maxDistance <= minDistance) {
        return 0.0f;
    }
    if (distanceXZ <= minDistance) {
        return 1.0f;
    }
    if (distanceXZ >= maxDistance) {
        return 0.0f;
    }
    const float t = (distanceXZ - minDistance) / (maxDistance - minDistance);
    const float factor = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return factor * factor;
}

/// @brief Computes stereo pan in [-0.85, +0.85] from source offset relative to listener's right vector:
/// pan = clamp(dot(toSourceXZ, listenerRightXZ) / 10.0f, -0.85f, +0.85f)
[[nodiscard]] inline float CalculateStereoPan(
    const glm::vec2& toSourceXZ,
    const glm::vec2& listenerRightXZ) noexcept
{
    const float rightComponent = glm::dot(toSourceXZ, listenerRightXZ);
    return std::clamp(rightComponent / 10.0f, -0.85f, 0.85f);
}

struct PanGains {
    float Left = 0.70710678f;
    float Right = 0.70710678f;
};

/// @brief Constant-power stereo pan law: L = cos(theta), R = sin(theta) where theta = (pan + 1) * pi / 4.
/// Satisfies L^2 + R^2 == 1.0f for all pan in [-1.0, +1.0].
[[nodiscard]] inline PanGains CalculateConstantPowerGains(float pan) noexcept {
    const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
    constexpr float kPiOver4 = 0.7853981633974483f; // pi / 4
    const float theta = (clampedPan + 1.0f) * kPiOver4;
    return PanGains{
        .Left = std::cos(theta),
        .Right = std::sin(theta)
    };
}

} // namespace SpatialAudio

class AudioManager {
public:
    static void Init(uint32_t maxVoices = 48);
    static void Shutdown();
    static void Update();
    static void Update(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ);

    [[nodiscard]] static bool IsInitialized() noexcept;

    // Bus Controls
    static void SetBusVolume(AudioBus bus, float volume01);
    [[nodiscard]] static float GetBusVolume(AudioBus bus) noexcept;
    [[nodiscard]] static bool IsBusPaused(AudioBus bus) noexcept;
    static void StopBus(AudioBus bus);
    static void PauseBus(AudioBus bus);
    static void ResumeBus(AudioBus bus);

    // Voice Playback
    static VoiceHandle PlayOneShot(const std::shared_ptr<AudioClip>& clip, const SoundPlayParams& params = {});
    static VoiceHandle PlayRandomVariation(std::span<const std::shared_ptr<AudioClip>> variations,
                                           const SoundPlayParams& params = {});
    static void StopVoice(VoiceHandle handle);
    static void PauseVoice(VoiceHandle handle);
    static void ResumeVoice(VoiceHandle handle);
    static void SetVoiceWorldPosition(VoiceHandle handle, const glm::vec3& worldPos);
    [[nodiscard]] static bool IsVoicePlaying(VoiceHandle handle) noexcept;
    [[nodiscard]] static float GetVoiceEffectiveGain(VoiceHandle handle) noexcept;
    [[nodiscard]] static float GetVoiceFrequencyRatio(VoiceHandle handle) noexcept;
    [[nodiscard]] static uint32_t GetActiveVoiceCount() noexcept;

    // Listener State
    static void SetListener(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ);
    [[nodiscard]] static glm::vec3 GetListenerPosition() noexcept;
    [[nodiscard]] static glm::vec3 GetListenerRight() noexcept;

    // Internal stream-track registration (automatically called by AudioStreamTrack)
    static void RegisterStreamTrack(AudioStreamTrack* track);
    static void UnregisterStreamTrack(AudioStreamTrack* track);
    [[nodiscard]] static SDL_AudioDeviceID GetPlaybackDevice() noexcept;
};

} // namespace Engine

#endif // AUDIO_AUDIOMANAGER_H
