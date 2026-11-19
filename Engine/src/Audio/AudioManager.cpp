#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioStreamTrack.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

namespace Engine {

namespace {

std::atomic<bool> s_AudioInitialized{ false };
SDL_AudioDeviceID s_PlaybackDevice = 0;
std::mutex s_AudioMutex;

// Stream Tracks
std::vector<AudioStreamTrack*> s_ActiveStreamTracks;

// Audio Buses
std::array<float, static_cast<size_t>(AudioBus::Count)> s_BusVolumes = {
    1.0f, // Master
    1.0f, // Music
    1.0f, // SFX
    1.0f, // Ambience
    1.0f  // UI
};

std::array<bool, static_cast<size_t>(AudioBus::Count)> s_BusPaused = {
    false, false, false, false, false
};

// Listener State
glm::vec3 s_ListenerPos{ 0.0f, 0.0f, 0.0f };
glm::vec3 s_ListenerRight{ 1.0f, 0.0f, 0.0f };

// Voice Pool
struct VoiceSlot {
    VoiceHandle Handle = kInvalidVoiceHandle;
    uint32_t Generation = 1;
    SDL_AudioStream* Stream = nullptr;
    std::shared_ptr<AudioClip> Clip;
    SoundPlayParams Params;
    float EffectiveGain = 1.0f;
    float CalculatedAttenuation = 1.0f;
    float CalculatedPan = 0.0f;
    float PitchRatio = 1.0f;
    uint64_t StartTimeMs = 0;
    bool Active = false;
    bool Paused = false;
    std::vector<int16_t> PannedBuffer;
};

std::vector<VoiceSlot> s_VoicePool;

uint64_t GetCurrentTimeMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

float ComputeEffectiveGain(const SoundPlayParams& params, float attenuation) noexcept {
    const float masterVol = s_BusVolumes[static_cast<size_t>(AudioBus::Master)];
    const float busVol = s_BusVolumes[static_cast<size_t>(params.Bus)];
    const float atten = params.SpatializedXZ ? attenuation : 1.0f;
    return std::clamp(masterVol * busVol * params.Volume * atten, 0.0f, 1.0f);
}

void ApplySpatialParamsToVoice(VoiceSlot& slot,
                               const glm::vec3& listenerPos,
                               const glm::vec3& listenerRight)
{
    if (!slot.Params.SpatializedXZ) {
        slot.CalculatedAttenuation = 1.0f;
        slot.CalculatedPan = 0.0f;
    } else {
        const glm::vec2 toSourceXZ(
            slot.Params.WorldPos.x - listenerPos.x,
            slot.Params.WorldPos.z - listenerPos.z
        );
        const float dist = glm::length(toSourceXZ);
        slot.CalculatedAttenuation = SpatialAudio::CalculateDistanceAttenuation(
            dist, slot.Params.MinDistance, slot.Params.MaxDistance
        );
        slot.CalculatedPan = SpatialAudio::CalculateStereoPan(
            toSourceXZ, glm::vec2(listenerRight.x, listenerRight.z)
        );
    }
    slot.EffectiveGain = ComputeEffectiveGain(slot.Params, slot.CalculatedAttenuation);
    if (slot.Stream) {
        SDL_SetAudioStreamGain(slot.Stream, slot.EffectiveGain);
    }
}

void PutVoiceData(VoiceSlot& slot,
                  const glm::vec3& listenerPos,
                  const glm::vec3& listenerRight)
{
    if (!slot.Clip || !slot.Clip->IsValid() || !slot.Stream) {
        return;
    }

    ApplySpatialParamsToVoice(slot, listenerPos, listenerRight);

    // Apply pitch frequency ratio
    float pitchRatio = 1.0f;
    if (slot.Params.PitchMax > slot.Params.PitchMin) {
        const float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        pitchRatio = slot.Params.PitchMin + t * (slot.Params.PitchMax - slot.Params.PitchMin);
    } else {
        pitchRatio = slot.Params.PitchMin;
    }
    slot.PitchRatio = pitchRatio;
    SDL_SetAudioStreamFrequencyRatio(slot.Stream, pitchRatio);

    const SDL_AudioSpec& clipSpec = slot.Clip->GetSpec();
    const uint8_t* clipBuffer = slot.Clip->GetBuffer();
    const uint32_t clipBytes = slot.Clip->GetLength();

    if (slot.Params.SpatializedXZ) {
        // Spatialized: Render into stereo PCM with constant-power L/R gains
        const SpatialAudio::PanGains gains = SpatialAudio::CalculateConstantPowerGains(slot.CalculatedPan);
        const auto* srcSamples = reinterpret_cast<const int16_t*>(clipBuffer);
        const size_t totalSamples = clipBytes / sizeof(int16_t);

        SDL_AudioSpec stereoSpec{};
        stereoSpec.format = SDL_AUDIO_S16;
        stereoSpec.channels = 2;
        stereoSpec.freq = clipSpec.freq;

        if (clipSpec.channels == 1) {
            slot.PannedBuffer.resize(totalSamples * 2u);
            for (size_t i = 0; i < totalSamples; ++i) {
                const float s = static_cast<float>(srcSamples[i]);
                const float left = std::clamp(s * gains.Left, -32768.0f, 32767.0f);
                const float right = std::clamp(s * gains.Right, -32768.0f, 32767.0f);
                slot.PannedBuffer[i * 2u + 0] = static_cast<int16_t>(left);
                slot.PannedBuffer[i * 2u + 1] = static_cast<int16_t>(right);
            }
        } else {
            slot.PannedBuffer.resize(totalSamples);
            constexpr float kSqrt2 = 1.41421356f;
            const size_t frameCount = totalSamples / 2u;
            for (size_t i = 0; i < frameCount; ++i) {
                const float sL = static_cast<float>(srcSamples[i * 2u + 0]);
                const float sR = static_cast<float>(srcSamples[i * 2u + 1]);
                const float left = std::clamp(sL * gains.Left * kSqrt2, -32768.0f, 32767.0f);
                const float right = std::clamp(sR * gains.Right * kSqrt2, -32768.0f, 32767.0f);
                slot.PannedBuffer[i * 2u + 0] = static_cast<int16_t>(left);
                slot.PannedBuffer[i * 2u + 1] = static_cast<int16_t>(right);
            }
        }

        SDL_SetAudioStreamFormat(slot.Stream, &stereoSpec, s_PlaybackDevice != 0 ? nullptr : &stereoSpec);
        SDL_PutAudioStreamData(slot.Stream, slot.PannedBuffer.data(), static_cast<int>(slot.PannedBuffer.size() * sizeof(int16_t)));
    } else {
        SDL_SetAudioStreamFormat(slot.Stream, &clipSpec, s_PlaybackDevice != 0 ? nullptr : &clipSpec);
        SDL_PutAudioStreamData(slot.Stream, clipBuffer, static_cast<int>(clipBytes));
    }

    if (!slot.Paused) {
        SDL_ResumeAudioStreamDevice(slot.Stream);
    }
}

} // namespace

void AudioManager::Init(uint32_t maxVoices) {
    if (s_AudioInitialized.load(std::memory_order_acquire)) {
        return;
    }

    ENGINE_CORE_INFO("Initializing AudioManager (maxVoices={})...", maxVoices);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        ENGINE_CORE_ERROR("Failed to initialize SDL Audio: {}", SDL_GetError());
        return;
    }

    std::lock_guard lock(s_AudioMutex);
    s_PlaybackDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (s_PlaybackDevice == 0) {
        ENGINE_CORE_WARN("AudioManager::Init: No physical audio device opened (operating in virtualized headless mode): {}", SDL_GetError());
    } else {
        ENGINE_CORE_INFO("AudioManager opened logical playback device ID {}", s_PlaybackDevice);
    }

    s_BusVolumes.fill(1.0f);
    s_BusPaused.fill(false);
    s_ListenerPos = glm::vec3(0.0f);
    s_ListenerRight = glm::vec3(1.0f, 0.0f, 0.0f);

    s_VoicePool.resize(maxVoices);
    SDL_AudioSpec defaultSpec{};
    defaultSpec.format = SDL_AUDIO_S16;
    defaultSpec.channels = 2;
    defaultSpec.freq = 44100;

    for (size_t i = 0; i < s_VoicePool.size(); ++i) {
        auto& slot = s_VoicePool[i];
        slot.Generation = 1;
        slot.Handle = kInvalidVoiceHandle;
        slot.Active = false;
        slot.Paused = false;
        slot.EffectiveGain = 1.0f;
        slot.CalculatedAttenuation = 1.0f;
        slot.CalculatedPan = 0.0f;
        slot.PitchRatio = 1.0f;
        slot.StartTimeMs = 0;

        if (s_PlaybackDevice != 0) {
            slot.Stream = SDL_CreateAudioStream(&defaultSpec, nullptr);
            if (slot.Stream) {
                SDL_BindAudioStream(s_PlaybackDevice, slot.Stream);
            }
        } else {
            slot.Stream = SDL_CreateAudioStream(&defaultSpec, &defaultSpec);
        }
    }

    s_AudioInitialized.store(true, std::memory_order_release);
    ENGINE_CORE_INFO("AudioManager initialized successfully with {} voices", maxVoices);
}

void AudioManager::Shutdown() {
    ENGINE_CORE_INFO("Shutting down AudioManager...");

    {
        std::lock_guard lock(s_AudioMutex);
        for (AudioStreamTrack* track : s_ActiveStreamTracks) {
            if (track) {
                track->ReleaseAudioStream();
            }
        }
        s_ActiveStreamTracks.clear();

        for (auto& slot : s_VoicePool) {
            if (slot.Stream) {
                if (s_PlaybackDevice != 0) {
                    SDL_UnbindAudioStream(slot.Stream);
                }
                SDL_DestroyAudioStream(slot.Stream);
                slot.Stream = nullptr;
            }
            slot.Active = false;
            slot.Paused = false;
            slot.Clip.reset();
            slot.PannedBuffer.clear();
        }
        s_VoicePool.clear();

        if (s_PlaybackDevice != 0) {
            SDL_CloseAudioDevice(s_PlaybackDevice);
            s_PlaybackDevice = 0;
        }
    }

    if (s_AudioInitialized.exchange(false, std::memory_order_acq_rel)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    ENGINE_CORE_INFO("AudioManager shut down");
}

void AudioManager::Update() {
    Update(s_ListenerPos, s_ListenerRight);
}

void AudioManager::Update(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ) {
    if (!s_AudioInitialized.load(std::memory_order_acquire)) {
        return;
    }

    std::vector<AudioStreamTrack*> tracksCopy;
    {
        std::lock_guard lock(s_AudioMutex);
        s_ListenerPos = listenerWorldPos;
        s_ListenerRight = listenerRightXZ;

        for (size_t i = 0; i < s_VoicePool.size(); ++i) {
            auto& slot = s_VoicePool[i];
            if (!slot.Active) {
                continue;
            }

            if (slot.Stream && SDL_GetAudioStreamAvailable(slot.Stream) == 0) {
                if (slot.Params.Loop && slot.Clip && slot.Clip->IsValid()) {
                    PutVoiceData(slot, s_ListenerPos, s_ListenerRight);
                } else {
                    slot.Active = false;
                    slot.Paused = false;
                    slot.Generation++;
                    slot.Handle = kInvalidVoiceHandle;
                    slot.Clip.reset();
                    continue;
                }
            }

            if (slot.Params.SpatializedXZ) {
                ApplySpatialParamsToVoice(slot, s_ListenerPos, s_ListenerRight);
            }
        }

        tracksCopy = s_ActiveStreamTracks;
    }

    for (AudioStreamTrack* track : tracksCopy) {
        if (track && track->IsPlaying() && track->IsAutoUpdateEnabled()) {
            track->Update();
        }
    }
}

bool AudioManager::IsInitialized() noexcept {
    return s_AudioInitialized.load(std::memory_order_acquire);
}

void AudioManager::SetBusVolume(AudioBus bus, float volume01) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    std::lock_guard lock(s_AudioMutex);
    s_BusVolumes[busIdx] = std::clamp(volume01, 0.0f, 1.0f);

    for (auto& slot : s_VoicePool) {
        if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
            slot.EffectiveGain = ComputeEffectiveGain(slot.Params, slot.CalculatedAttenuation);
            if (slot.Stream) {
                SDL_SetAudioStreamGain(slot.Stream, slot.EffectiveGain);
            }
        }
    }
}

float AudioManager::GetBusVolume(AudioBus bus) noexcept {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return 0.0f;
    }
    std::lock_guard lock(s_AudioMutex);
    return s_BusVolumes[busIdx];
}

void AudioManager::StopBus(AudioBus bus) {
    std::lock_guard lock(s_AudioMutex);
    for (auto& slot : s_VoicePool) {
        if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
            if (slot.Stream) {
                SDL_ClearAudioStream(slot.Stream);
                SDL_PauseAudioStreamDevice(slot.Stream);
            }
            slot.Active = false;
            slot.Paused = false;
            slot.Generation++;
            slot.Handle = kInvalidVoiceHandle;
            slot.Clip.reset();
        }
    }
}

void AudioManager::PauseBus(AudioBus bus) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    std::lock_guard lock(s_AudioMutex);
    s_BusPaused[busIdx] = true;
    for (auto& slot : s_VoicePool) {
        if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
            slot.Paused = true;
            if (slot.Stream) {
                SDL_PauseAudioStreamDevice(slot.Stream);
            }
        }
    }
}

void AudioManager::ResumeBus(AudioBus bus) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    std::lock_guard lock(s_AudioMutex);
    s_BusPaused[busIdx] = false;
    for (auto& slot : s_VoicePool) {
        if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
            if (bus == AudioBus::Master || !s_BusPaused[static_cast<size_t>(AudioBus::Master)]) {
                slot.Paused = false;
                if (slot.Stream) {
                    SDL_ResumeAudioStreamDevice(slot.Stream);
                }
            }
        }
    }
}

VoiceHandle AudioManager::PlayOneShot(const std::shared_ptr<AudioClip>& clip, const SoundPlayParams& params) {
    if (!clip || !clip->IsValid()) {
        ENGINE_CORE_WARN("AudioManager::PlayOneShot: invalid or null clip");
        return kInvalidVoiceHandle;
    }

    if (!s_AudioInitialized.load(std::memory_order_acquire)) {
        Init();
    }

    std::lock_guard lock(s_AudioMutex);
    if (s_VoicePool.empty()) {
        return kInvalidVoiceHandle;
    }

    // 1. Search for inactive or finished voice slot
    int chosenIndex = -1;
    for (size_t i = 0; i < s_VoicePool.size(); ++i) {
        auto& slot = s_VoicePool[i];
        if (!slot.Active || (slot.Stream && SDL_GetAudioStreamAvailable(slot.Stream) == 0 && !slot.Params.Loop)) {
            chosenIndex = static_cast<int>(i);
            break;
        }
    }

    // 2. If pool is saturated, perform priority-based voice stealing
    if (chosenIndex < 0) {
        float lowestScore = std::numeric_limits<float>::max();
        for (size_t i = 0; i < s_VoicePool.size(); ++i) {
            const auto& slot = s_VoicePool[i];
            if (slot.Params.Priority == 255) {
                continue; // Protected critical voice
            }
            const float priorityWeight = static_cast<float>(slot.Params.Priority);
            const float gainWeight = std::max(0.01f, slot.EffectiveGain);
            const float loopBonus = slot.Params.Loop ? 50.0f : 0.0f;
            const float score = priorityWeight * 10.0f + gainWeight * 100.0f + loopBonus;
            if (score < lowestScore) {
                lowestScore = score;
                chosenIndex = static_cast<int>(i);
            }
        }

        // Fallback: if all voices were 255, steal the first slot
        if (chosenIndex < 0) {
            chosenIndex = 0;
        }

        auto& stolen = s_VoicePool[static_cast<size_t>(chosenIndex)];
        if (stolen.Stream) {
            SDL_ClearAudioStream(stolen.Stream);
        }
        stolen.Active = false;
        stolen.Generation++;
        stolen.Handle = kInvalidVoiceHandle;
    }

    auto& slot = s_VoicePool[static_cast<size_t>(chosenIndex)];
    slot.Generation = (slot.Generation == 0xFFFFFF) ? 1 : (slot.Generation + 1);
    slot.Handle = (slot.Generation << 8) | (static_cast<uint32_t>(chosenIndex) & 0xFF);
    slot.Clip = clip;
    slot.Params = params;
    slot.StartTimeMs = GetCurrentTimeMs();
    slot.Active = true;
    slot.Paused = s_BusPaused[static_cast<size_t>(params.Bus)] || s_BusPaused[static_cast<size_t>(AudioBus::Master)];

    if (slot.Stream) {
        SDL_ClearAudioStream(slot.Stream);
    }
    PutVoiceData(slot, s_ListenerPos, s_ListenerRight);
    return slot.Handle;
}

VoiceHandle AudioManager::PlayRandomVariation(std::span<const std::shared_ptr<AudioClip>> variations,
                                              const SoundPlayParams& params) {
    if (variations.empty()) {
        return kInvalidVoiceHandle;
    }
    if (variations.size() == 1) {
        return PlayOneShot(variations[0], params);
    }

    std::vector<size_t> validIndices;
    validIndices.reserve(variations.size());
    for (size_t i = 0; i < variations.size(); ++i) {
        if (variations[i] && variations[i]->IsValid()) {
            validIndices.push_back(i);
        }
    }

    if (validIndices.empty()) {
        return kInvalidVoiceHandle;
    }

    static size_t s_LastPicked = 0;
    size_t picked = validIndices[static_cast<size_t>(std::rand()) % validIndices.size()];
    if (validIndices.size() > 1 && picked == s_LastPicked) {
        picked = validIndices[static_cast<size_t>(std::rand()) % validIndices.size()];
    }
    s_LastPicked = picked;
    return PlayOneShot(variations[picked], params);
}

void AudioManager::StopVoice(VoiceHandle handle) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen) {
        if (slot.Stream) {
            SDL_ClearAudioStream(slot.Stream);
            SDL_PauseAudioStreamDevice(slot.Stream);
        }
        slot.Active = false;
        slot.Paused = false;
        slot.Generation++;
        slot.Handle = kInvalidVoiceHandle;
        slot.Clip.reset();
    }
}

void AudioManager::PauseVoice(VoiceHandle handle) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen && !slot.Paused) {
        slot.Paused = true;
        if (slot.Stream) {
            SDL_PauseAudioStreamDevice(slot.Stream);
        }
    }
}

void AudioManager::ResumeVoice(VoiceHandle handle) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen && slot.Paused) {
        slot.Paused = false;
        if (slot.Stream) {
            SDL_ResumeAudioStreamDevice(slot.Stream);
        }
    }
}

bool AudioManager::IsVoicePlaying(VoiceHandle handle) noexcept {
    if (handle == kInvalidVoiceHandle) {
        return false;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return false;
    }
    const auto& slot = s_VoicePool[index];
    return slot.Active && slot.Generation == gen && !slot.Paused;
}

float AudioManager::GetVoiceEffectiveGain(VoiceHandle handle) noexcept {
    if (handle == kInvalidVoiceHandle) {
        return 0.0f;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return 0.0f;
    }
    const auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen) {
        return slot.EffectiveGain;
    }
    return 0.0f;
}

float AudioManager::GetVoiceFrequencyRatio(VoiceHandle handle) noexcept {
    if (handle == kInvalidVoiceHandle) {
        return 1.0f;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & 0xFF;
    const uint32_t gen = handle >> 8;
    if (index >= s_VoicePool.size()) {
        return 1.0f;
    }
    const auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen) {
        return slot.PitchRatio;
    }
    return 1.0f;
}

uint32_t AudioManager::GetActiveVoiceCount() noexcept {
    std::lock_guard lock(s_AudioMutex);
    uint32_t count = 0;
    for (const auto& slot : s_VoicePool) {
        if (slot.Active) {
            count++;
        }
    }
    return count;
}

void AudioManager::SetListener(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ) {
    std::lock_guard lock(s_AudioMutex);
    s_ListenerPos = listenerWorldPos;
    s_ListenerRight = listenerRightXZ;
}

glm::vec3 AudioManager::GetListenerPosition() noexcept {
    std::lock_guard lock(s_AudioMutex);
    return s_ListenerPos;
}

glm::vec3 AudioManager::GetListenerRight() noexcept {
    std::lock_guard lock(s_AudioMutex);
    return s_ListenerRight;
}

void AudioManager::RegisterStreamTrack(AudioStreamTrack* track) {
    if (!track) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    if (std::find(s_ActiveStreamTracks.begin(), s_ActiveStreamTracks.end(), track) == s_ActiveStreamTracks.end()) {
        s_ActiveStreamTracks.push_back(track);
    }
}

void AudioManager::UnregisterStreamTrack(AudioStreamTrack* track) {
    if (!track) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    s_ActiveStreamTracks.erase(
        std::remove(s_ActiveStreamTracks.begin(), s_ActiveStreamTracks.end(), track),
        s_ActiveStreamTracks.end()
    );
}

SDL_AudioDeviceID AudioManager::GetPlaybackDevice() noexcept {
    std::lock_guard lock(s_AudioMutex);
    return s_PlaybackDevice;
}

} // namespace Engine
