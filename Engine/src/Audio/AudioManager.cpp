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
std::mutex s_AudioLifecycleMutex;
std::mutex s_AudioMutex;

// Stream Tracks (SDOF-safe Meyer's singleton; ordered after s_MusicMutex and before m_TrackMutex)
struct TrackRegistryState {
    std::mutex Mutex;
    std::vector<AudioStreamTrack*> Tracks;
    std::atomic<bool> Alive{ true };
    ~TrackRegistryState();
};

std::atomic<bool> s_TrackRegistryDestroyed{ false };

TrackRegistryState::~TrackRegistryState() {
    Alive.store(false, std::memory_order_release);
    s_TrackRegistryDestroyed.store(true, std::memory_order_release);
}

TrackRegistryState& GetTrackRegistry() {
    static TrackRegistryState instance;
    return instance;
}

// Audio Buses
std::array<float, static_cast<size_t>(AudioBus::Count)> s_BusVolumes = {
    1.0f, // Master
    1.0f, // Music
    1.0f, // SFX
    1.0f, // Ambience
    1.0f  // UI
};

std::array<std::atomic<bool>, static_cast<size_t>(AudioBus::Count)> s_BusPaused = {
    false, false, false, false, false
};

// Handle representation: 12-bit slot index (up to 4096 voices), 20-bit generation
constexpr uint32_t kVoiceIndexBits = 12u;
constexpr uint32_t kVoiceIndexMask = (1u << kVoiceIndexBits) - 1u; // 0xFFF
constexpr uint32_t kVoiceGenerationBits = 20u;
constexpr uint32_t kVoiceGenerationMask = (1u << kVoiceGenerationBits) - 1u; // 0xFFFFF

void SetStreamPaused(SDL_AudioStream* stream, bool paused) noexcept {
    if (!stream || s_PlaybackDevice == 0) {
        return;
    }
    if (paused) {
        SDL_UnbindAudioStream(stream);
    } else if (SDL_GetAudioStreamDevice(stream) == 0) {
        SDL_BindAudioStream(s_PlaybackDevice, stream);
    }
}

// Listener State
glm::vec3 s_ListenerPos{ 0.0f, 0.0f, 0.0f };
glm::vec3 s_ListenerRight{ 1.0f, 0.0f, 0.0f };

// Voice Pool
constexpr size_t kSpatialChunkFrames = 2048u;
constexpr size_t kSpatialTargetBufferedFrames = 4096u;

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
    size_t CursorFrame = 0;
    bool Active = false;
    bool UserPaused = false;
    bool BusPaused = false;
    std::vector<int16_t> PannedBuffer;
};

[[nodiscard]] bool IsSlotPaused(const VoiceSlot& slot) noexcept {
    return slot.UserPaused || slot.BusPaused;
}

std::vector<VoiceSlot> s_VoicePool;
std::atomic<uint64_t (*)() noexcept> s_TestClockMs{ nullptr };

uint64_t GetCurrentTimeMs() {
    if (auto* testClock = s_TestClockMs.load(std::memory_order_acquire)) {
        return testClock();
    }
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

float ComputeEffectiveGain(const SoundPlayParams& params, float attenuation) noexcept {
    const size_t masterIdx = static_cast<size_t>(AudioBus::Master);
    const size_t busIdx = static_cast<size_t>(params.Bus);
    const float masterVol = (masterIdx < s_BusVolumes.size()) ? s_BusVolumes[masterIdx] : 1.0f;
    const float busVol = (busIdx < s_BusVolumes.size()) ? s_BusVolumes[busIdx] : 1.0f;
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

void PumpSpatialVoiceChunks(VoiceSlot& slot) {
    if (!slot.Params.SpatializedXZ || !slot.Clip || !slot.Clip->IsValid() || !slot.Stream) {
        return;
    }

    const SDL_AudioSpec& clipSpec = slot.Clip->GetSpec();
    if (clipSpec.format != SDL_AUDIO_S16 || clipSpec.channels <= 0 || clipSpec.channels > 2) {
        return;
    }

    const uint8_t* clipBuffer = slot.Clip->GetBuffer();
    const uint32_t clipBytes = slot.Clip->GetLength();
    const size_t channels = static_cast<size_t>(clipSpec.channels);
    const size_t totalFrames = static_cast<size_t>(clipBytes) / (channels * sizeof(int16_t));
    if (totalFrames == 0u) {
        return;
    }

    const int queuedBytes = SDL_GetAudioStreamQueued(slot.Stream);
    size_t queuedFrames = (queuedBytes > 0) ? (static_cast<size_t>(queuedBytes) / (2u * sizeof(int16_t))) : 0u;
    const auto* srcSamples = reinterpret_cast<const int16_t*>(clipBuffer);
    const SpatialAudio::PanGains gains = SpatialAudio::CalculateConstantPowerGains(slot.CalculatedPan);

    while (queuedFrames < kSpatialTargetBufferedFrames) {
        if (slot.CursorFrame >= totalFrames) {
            if (slot.Params.Loop) {
                slot.CursorFrame = 0u;
            } else {
                break;
            }
        }

        const size_t framesToPush = std::min(kSpatialChunkFrames, totalFrames - slot.CursorFrame);
        slot.PannedBuffer.resize(framesToPush * 2u);

        if (channels == 1u) {
            for (size_t i = 0; i < framesToPush; ++i) {
                const float s = static_cast<float>(srcSamples[slot.CursorFrame + i]);
                const float left = std::clamp(s * gains.Left, -32768.0f, 32767.0f);
                const float right = std::clamp(s * gains.Right, -32768.0f, 32767.0f);
                slot.PannedBuffer[i * 2u + 0u] = static_cast<int16_t>(left);
                slot.PannedBuffer[i * 2u + 1u] = static_cast<int16_t>(right);
            }
        } else {
            constexpr float kSqrt2 = 1.41421356f;
            for (size_t i = 0; i < framesToPush; ++i) {
                const size_t srcIdx = (slot.CursorFrame + i) * 2u;
                const float sL = static_cast<float>(srcSamples[srcIdx + 0u]);
                const float sR = static_cast<float>(srcSamples[srcIdx + 1u]);
                const float left = std::clamp(sL * gains.Left * kSqrt2, -32768.0f, 32767.0f);
                const float right = std::clamp(sR * gains.Right * kSqrt2, -32768.0f, 32767.0f);
                slot.PannedBuffer[i * 2u + 0u] = static_cast<int16_t>(left);
                slot.PannedBuffer[i * 2u + 1u] = static_cast<int16_t>(right);
            }
        }

        SDL_PutAudioStreamData(
            slot.Stream,
            slot.PannedBuffer.data(),
            static_cast<int>(slot.PannedBuffer.size() * sizeof(int16_t))
        );
        slot.CursorFrame += framesToPush;
        queuedFrames += framesToPush;
    }
}

void PutVoiceData(VoiceSlot& slot,
                  const glm::vec3& listenerPos,
                  const glm::vec3& listenerRight)
{
    if (!slot.Clip || !slot.Clip->IsValid() || !slot.Stream) {
        return;
    }

    slot.CursorFrame = 0u;
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
        if (clipSpec.format != SDL_AUDIO_S16 || clipSpec.channels <= 0 || clipSpec.channels > 2) {
            ENGINE_CORE_WARN("AudioManager::PutVoiceData: Non-S16 or unsupported channel count ({}) for spatialized voice. Falling back to non-spatialized stream.",
                             clipSpec.channels);
            SDL_SetAudioStreamFormat(slot.Stream, &clipSpec, s_PlaybackDevice != 0 ? nullptr : &clipSpec);
            SDL_PutAudioStreamData(slot.Stream, clipBuffer, static_cast<int>(clipBytes));
            SetStreamPaused(slot.Stream, IsSlotPaused(slot));
            return;
        }

        // Spatialized: Stream stereo PCM chunks with live constant-power L/R gains
        SDL_AudioSpec stereoSpec{};
        stereoSpec.format = SDL_AUDIO_S16;
        stereoSpec.channels = 2;
        stereoSpec.freq = clipSpec.freq;

        SDL_SetAudioStreamFormat(slot.Stream, &stereoSpec, s_PlaybackDevice != 0 ? nullptr : &stereoSpec);
        PumpSpatialVoiceChunks(slot);
    } else {
        SDL_SetAudioStreamFormat(slot.Stream, &clipSpec, s_PlaybackDevice != 0 ? nullptr : &clipSpec);
        SDL_PutAudioStreamData(slot.Stream, clipBuffer, static_cast<int>(clipBytes));
    }

    SetStreamPaused(slot.Stream, IsSlotPaused(slot));
}

} // namespace

void AudioManager::SetClockForTesting(uint64_t (*clockFn)() noexcept) noexcept {
    s_TestClockMs.store(clockFn, std::memory_order_release);
}

void AudioManager::Init(uint32_t maxVoices) {
    std::lock_guard lifecycleLock(s_AudioLifecycleMutex);
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
    for (auto& paused : s_BusPaused) {
        paused.store(false, std::memory_order_release);
    }
    s_ListenerPos = glm::vec3(0.0f);
    s_ListenerRight = glm::vec3(1.0f, 0.0f, 0.0f);

    const uint32_t clampedVoices = std::clamp(maxVoices, 1u, kVoiceIndexMask + 1u);
    s_VoicePool.resize(clampedVoices);
    SDL_AudioSpec defaultSpec{};
    defaultSpec.format = SDL_AUDIO_S16;
    defaultSpec.channels = 2;
    defaultSpec.freq = 44100;

    for (size_t i = 0; i < s_VoicePool.size(); ++i) {
        auto& slot = s_VoicePool[i];
        slot.Generation = 1;
        slot.Handle = kInvalidVoiceHandle;
        slot.Active = false;
        slot.UserPaused = false;
        slot.BusPaused = false;
        slot.CursorFrame = 0u;
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
    ENGINE_CORE_INFO("AudioManager initialized successfully with {} voices", clampedVoices);
}

void AudioManager::Shutdown() {
    std::lock_guard lifecycleLock(s_AudioLifecycleMutex);
    ENGINE_CORE_INFO("Shutting down AudioManager...");

    if (!s_AudioInitialized.exchange(false, std::memory_order_acq_rel)) {
        ENGINE_CORE_INFO("AudioManager shut down");
        return;
    }

    if (!s_TrackRegistryDestroyed.load(std::memory_order_acquire)) {
        auto& registry = GetTrackRegistry();
        if (registry.Alive.load(std::memory_order_acquire)) {
            std::vector<AudioStreamTrack*> tracksToRelease;
            {
                std::lock_guard trackLock(registry.Mutex);
                tracksToRelease = std::move(registry.Tracks);
                registry.Tracks.clear();
            }
            for (AudioStreamTrack* track : tracksToRelease) {
                if (track) {
                    track->ReleaseAudioStream();
                }
            }
        }
    }

    {
        std::lock_guard lock(s_AudioMutex);
        for (auto& slot : s_VoicePool) {
            if (slot.Stream) {
                if (s_PlaybackDevice != 0) {
                    SDL_UnbindAudioStream(slot.Stream);
                }
                SDL_DestroyAudioStream(slot.Stream);
                slot.Stream = nullptr;
            }
            slot.Active = false;
            slot.UserPaused = false;
            slot.BusPaused = false;
            slot.CursorFrame = 0u;
            slot.Clip.reset();
            slot.PannedBuffer.clear();
        }
        s_VoicePool.clear();

        if (s_PlaybackDevice != 0) {
            SDL_CloseAudioDevice(s_PlaybackDevice);
            s_PlaybackDevice = 0;
        }
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ENGINE_CORE_INFO("AudioManager shut down");
}

namespace {
void UpdateInternal(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ, bool overrideListener) {
    {
        std::lock_guard lock(s_AudioMutex);
        if (overrideListener) {
            s_ListenerPos = listenerWorldPos;
            s_ListenerRight = listenerRightXZ;
        }

        const uint64_t nowMs = GetCurrentTimeMs();

        for (size_t i = 0; i < s_VoicePool.size(); ++i) {
            auto& slot = s_VoicePool[i];
            if (!slot.Active) {
                continue;
            }

            bool voiceFinished = false;
            if (slot.Stream) {
                const bool useElapsedDuration =
                    (s_PlaybackDevice == 0) || (s_TestClockMs.load(std::memory_order_acquire) != nullptr);
                if (!useElapsedDuration) {
                    if (slot.Params.SpatializedXZ && slot.Clip && slot.Clip->IsValid() &&
                        slot.Clip->GetSpec().format == SDL_AUDIO_S16 &&
                        slot.Clip->GetSpec().channels > 0 && slot.Clip->GetSpec().channels <= 2)
                    {
                        const size_t totalFrames = static_cast<size_t>(slot.Clip->GetLength()) /
                            (static_cast<size_t>(slot.Clip->GetSpec().channels) * sizeof(int16_t));
                        voiceFinished = (!slot.Params.Loop && slot.CursorFrame >= totalFrames &&
                                         SDL_GetAudioStreamAvailable(slot.Stream) == 0);
                    } else {
                        voiceFinished = (SDL_GetAudioStreamAvailable(slot.Stream) == 0);
                    }
                } else if (!IsSlotPaused(slot) && !slot.Params.Loop && slot.Clip && slot.Clip->IsValid()) {
                    const uint64_t elapsedMs = (nowMs >= slot.StartTimeMs) ? (nowMs - slot.StartTimeMs) : 0u;
                    const uint32_t sampleRate = slot.Clip->GetSpec().freq > 0 ? static_cast<uint32_t>(slot.Clip->GetSpec().freq) : 44100u;
                    const uint32_t channels = slot.Clip->GetSpec().channels > 0 ? static_cast<uint32_t>(slot.Clip->GetSpec().channels) : 2u;
                    const uint32_t bytesPerSec = sampleRate * channels * sizeof(int16_t);
                    const uint64_t clipDurationMs = (bytesPerSec > 0) ? ((static_cast<uint64_t>(slot.Clip->GetLength()) * 1000u) / bytesPerSec) : 0u;
                    const float speed = std::max(0.1f, slot.PitchRatio);
                    const uint64_t scaledDurationMs = static_cast<uint64_t>(static_cast<float>(clipDurationMs) / speed);
                    if (elapsedMs >= scaledDurationMs) {
                        voiceFinished = true;
                    }
                }
            }

            if (voiceFinished) {
                if (slot.Params.Loop && slot.Clip && slot.Clip->IsValid()) {
                    PutVoiceData(slot, s_ListenerPos, s_ListenerRight);
                } else {
                    if (slot.Stream) {
                        SDL_ClearAudioStream(slot.Stream);
                        SetStreamPaused(slot.Stream, true);
                    }
                    slot.Active = false;
                    slot.UserPaused = false;
                    slot.BusPaused = false;
                    slot.CursorFrame = 0u;
                    slot.Generation = (slot.Generation >= kVoiceGenerationMask) ? 1u : (slot.Generation + 1u);
                    slot.Handle = kInvalidVoiceHandle;
                    slot.Clip.reset();
                    continue;
                }
            }

            if (slot.Params.SpatializedXZ) {
                ApplySpatialParamsToVoice(slot, s_ListenerPos, s_ListenerRight);
                PumpSpatialVoiceChunks(slot);
            }
        }
    }

    if (!s_TrackRegistryDestroyed.load(std::memory_order_acquire)) {
        auto& registry = GetTrackRegistry();
        if (registry.Alive.load(std::memory_order_acquire)) {
            std::lock_guard trackLock(registry.Mutex);
            for (AudioStreamTrack* track : registry.Tracks) {
                if (track && track->IsPlaying() && track->IsAutoUpdateEnabled() && !track->IsBusPaused()) {
                    track->Update();
                }
            }
        }
    }
}
} // namespace

void AudioManager::Update() {
    if (!s_AudioInitialized.load(std::memory_order_acquire)) {
        return;
    }
    UpdateInternal({}, {}, false);
}

void AudioManager::Update(const glm::vec3& listenerWorldPos, const glm::vec3& listenerRightXZ) {
    if (!s_AudioInitialized.load(std::memory_order_acquire)) {
        return;
    }
    UpdateInternal(listenerWorldPos, listenerRightXZ, true);
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

bool AudioManager::IsBusPaused(AudioBus bus) noexcept {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= s_BusPaused.size()) {
        return false;
    }
    return s_BusPaused[busIdx].load(std::memory_order_acquire);
}

void AudioManager::StopBus(AudioBus bus) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    {
        std::lock_guard lock(s_AudioMutex);
        for (auto& slot : s_VoicePool) {
            if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
                if (slot.Stream) {
                    SDL_ClearAudioStream(slot.Stream);
                    SetStreamPaused(slot.Stream, true);
                }
                slot.Active = false;
                slot.UserPaused = false;
                slot.BusPaused = false;
                slot.CursorFrame = 0u;
                slot.Generation = (slot.Generation >= kVoiceGenerationMask) ? 1u : (slot.Generation + 1u);
                slot.Handle = kInvalidVoiceHandle;
                slot.Clip.reset();
            }
        }
    }

    if ((bus == AudioBus::Master || bus == AudioBus::Music) &&
        !s_TrackRegistryDestroyed.load(std::memory_order_acquire))
    {
        auto& registry = GetTrackRegistry();
        if (registry.Alive.load(std::memory_order_acquire)) {
            std::lock_guard trackLock(registry.Mutex);
            for (AudioStreamTrack* track : registry.Tracks) {
                if (track) {
                    track->Stop();
                }
            }
        }
    }
}

void AudioManager::PauseBus(AudioBus bus) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    {
        std::lock_guard lock(s_AudioMutex);
        s_BusPaused[busIdx].store(true, std::memory_order_release);
        for (auto& slot : s_VoicePool) {
            if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
                slot.BusPaused = true;
                SetStreamPaused(slot.Stream, true);
            }
        }
    }

    if ((bus == AudioBus::Master || bus == AudioBus::Music) &&
        !s_TrackRegistryDestroyed.load(std::memory_order_acquire))
    {
        auto& registry = GetTrackRegistry();
        if (registry.Alive.load(std::memory_order_acquire)) {
            std::lock_guard trackLock(registry.Mutex);
            for (AudioStreamTrack* track : registry.Tracks) {
                if (track) {
                    track->SetBusPaused(true);
                }
            }
        }
    }
}

void AudioManager::ResumeBus(AudioBus bus) {
    const auto busIdx = static_cast<size_t>(bus);
    if (busIdx >= static_cast<size_t>(AudioBus::Count)) {
        return;
    }

    bool musicBusPaused = false;
    {
        std::lock_guard lock(s_AudioMutex);
        s_BusPaused[busIdx].store(false, std::memory_order_release);
        const bool masterPaused = s_BusPaused[static_cast<size_t>(AudioBus::Master)].load(std::memory_order_acquire);
        musicBusPaused = masterPaused || s_BusPaused[static_cast<size_t>(AudioBus::Music)].load(std::memory_order_acquire);
        for (auto& slot : s_VoicePool) {
            if (slot.Active && (bus == AudioBus::Master || slot.Params.Bus == bus)) {
                const size_t slotBusIdx = static_cast<size_t>(slot.Params.Bus);
                const bool slotBusPaused = (slotBusIdx < s_BusPaused.size())
                    ? s_BusPaused[slotBusIdx].load(std::memory_order_acquire)
                    : false;
                slot.BusPaused = masterPaused || slotBusPaused;
                SetStreamPaused(slot.Stream, IsSlotPaused(slot));
            }
        }
    }

    if ((bus == AudioBus::Master || bus == AudioBus::Music) &&
        !s_TrackRegistryDestroyed.load(std::memory_order_acquire))
    {
        auto& registry = GetTrackRegistry();
        if (registry.Alive.load(std::memory_order_acquire)) {
            std::lock_guard trackLock(registry.Mutex);
            for (AudioStreamTrack* track : registry.Tracks) {
                if (track) {
                    track->SetBusPaused(musicBusPaused);
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
            SetStreamPaused(stolen.Stream, true);
        }
        stolen.Active = false;
        stolen.Generation = (stolen.Generation >= kVoiceGenerationMask) ? 1u : (stolen.Generation + 1u);
        stolen.Handle = kInvalidVoiceHandle;
    }

    auto& slot = s_VoicePool[static_cast<size_t>(chosenIndex)];
    slot.Generation = (slot.Generation >= kVoiceGenerationMask) ? 1u : (slot.Generation + 1u);
    slot.Handle = (slot.Generation << kVoiceIndexBits) | (static_cast<uint32_t>(chosenIndex) & kVoiceIndexMask);
    slot.Clip = clip;
    slot.Params = params;
    slot.StartTimeMs = GetCurrentTimeMs();
    slot.Active = true;
    const size_t busIdx = static_cast<size_t>(params.Bus);
    const bool busIsPaused = (busIdx < s_BusPaused.size())
        ? s_BusPaused[busIdx].load(std::memory_order_acquire)
        : false;
    slot.UserPaused = false;
    slot.BusPaused = busIsPaused || s_BusPaused[static_cast<size_t>(AudioBus::Master)].load(std::memory_order_acquire);

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
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen) {
        if (slot.Stream) {
            SDL_ClearAudioStream(slot.Stream);
            SetStreamPaused(slot.Stream, true);
        }
        slot.Active = false;
        slot.UserPaused = false;
        slot.BusPaused = false;
        slot.CursorFrame = 0u;
        slot.Generation = (slot.Generation >= kVoiceGenerationMask) ? 1u : (slot.Generation + 1u);
        slot.Handle = kInvalidVoiceHandle;
        slot.Clip.reset();
    }
}

void AudioManager::PauseVoice(VoiceHandle handle) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen && !slot.UserPaused) {
        slot.UserPaused = true;
        SetStreamPaused(slot.Stream, true);
    }
}

void AudioManager::ResumeVoice(VoiceHandle handle) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen && slot.UserPaused) {
        slot.UserPaused = false;
        SetStreamPaused(slot.Stream, IsSlotPaused(slot));
    }
}

void AudioManager::SetVoiceWorldPosition(VoiceHandle handle, const glm::vec3& worldPos) {
    if (handle == kInvalidVoiceHandle) {
        return;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
    if (index >= s_VoicePool.size()) {
        return;
    }

    auto& slot = s_VoicePool[index];
    if (slot.Active && slot.Generation == gen) {
        slot.Params.WorldPos = worldPos;
        if (slot.Params.SpatializedXZ) {
            ApplySpatialParamsToVoice(slot, s_ListenerPos, s_ListenerRight);
        }
    }
}

bool AudioManager::IsVoicePlaying(VoiceHandle handle) noexcept {
    if (handle == kInvalidVoiceHandle) {
        return false;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
    if (index >= s_VoicePool.size()) {
        return false;
    }
    const auto& slot = s_VoicePool[index];
    return slot.Active && slot.Generation == gen && !IsSlotPaused(slot);
}

float AudioManager::GetVoiceEffectiveGain(VoiceHandle handle) noexcept {
    if (handle == kInvalidVoiceHandle) {
        return 0.0f;
    }
    std::lock_guard lock(s_AudioMutex);
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
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
    const uint32_t index = handle & kVoiceIndexMask;
    const uint32_t gen = handle >> kVoiceIndexBits;
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
    if (!track || s_TrackRegistryDestroyed.load(std::memory_order_acquire)) {
        return;
    }
    auto& registry = GetTrackRegistry();
    if (!registry.Alive.load(std::memory_order_acquire)) {
        return;
    }
    const bool musicBusPaused =
        s_BusPaused[static_cast<size_t>(AudioBus::Master)].load(std::memory_order_acquire) ||
        s_BusPaused[static_cast<size_t>(AudioBus::Music)].load(std::memory_order_acquire);
    {
        std::lock_guard lock(registry.Mutex);
        if (std::find(registry.Tracks.begin(), registry.Tracks.end(), track) == registry.Tracks.end()) {
            registry.Tracks.push_back(track);
        }
    }
    if (musicBusPaused) {
        track->SetBusPaused(true);
    }
}

void AudioManager::UnregisterStreamTrack(AudioStreamTrack* track) {
    if (!track || s_TrackRegistryDestroyed.load(std::memory_order_acquire)) {
        return;
    }
    auto& registry = GetTrackRegistry();
    if (!registry.Alive.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(registry.Mutex);
    registry.Tracks.erase(
        std::remove(registry.Tracks.begin(), registry.Tracks.end(), track),
        registry.Tracks.end()
    );
}

SDL_AudioDeviceID AudioManager::GetPlaybackDevice() noexcept {
    std::lock_guard lock(s_AudioMutex);
    return s_PlaybackDevice;
}

} // namespace Engine
