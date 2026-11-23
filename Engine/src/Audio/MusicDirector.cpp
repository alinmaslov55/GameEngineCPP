#include "Engine/Audio/MusicDirector.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioStreamTrack.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace Engine {

namespace {

std::mutex s_MusicMutex;

MusicMode s_Mode = MusicMode::Stopped;
CombatMusicTier s_CombatTier = CombatMusicTier::MediumTorchExploration;
float s_TorchFuel = 1.0f;
uint32_t s_NearbyEnemyCount = 0;

float s_SlewRate = MusicDirector::kDefaultSlewRate;
float s_CurrentTransitionSlewRate = MusicDirector::kDefaultSlewRate;
bool s_InTransition = false;
float s_MasterVolume = 1.0f;

// 3 Synchronized Stems
std::array<std::unique_ptr<AudioStreamTrack>, MusicDirector::kNumStems> s_Stems;
std::array<float, MusicDirector::kNumStems> s_StemWeights = { 0.0f, 0.0f, 0.0f };
std::array<float, MusicDirector::kNumStems> s_StemTargetWeights = { 0.3f, 1.0f, 0.0f };

// Single Track (Sanctuary Hub / Menu)
std::unique_ptr<AudioStreamTrack> s_SingleTrack;
float s_SingleTrackWeight = 0.0f;
float s_SingleTrackTargetWeight = 0.0f;

void EnsureInitializedLocked() {
    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (!s_Stems[i]) {
            s_Stems[i] = std::make_unique<AudioStreamTrack>();
        }
    }
    if (!s_SingleTrack) {
        s_SingleTrack = std::make_unique<AudioStreamTrack>();
    }
}

CombatMusicTier EvaluateCombatTier(float torchFuel01, uint32_t nearbyEnemyCount) noexcept {
    if (torchFuel01 >= 0.80f || nearbyEnemyCount >= 6) {
        return CombatMusicTier::AshFrenzyCombat;
    }
    if (torchFuel01 < 0.25f) {
        return CombatMusicTier::PitchBlackAmbience;
    }
    return CombatMusicTier::MediumTorchExploration;
}

std::array<float, MusicDirector::kNumStems> GetTierTargetWeights(CombatMusicTier tier) noexcept {
    switch (tier) {
        case CombatMusicTier::PitchBlackAmbience:
            return { 1.0f, 0.15f, 0.0f };
        case CombatMusicTier::MediumTorchExploration:
            return { 0.3f, 1.0f, 0.0f };
        case CombatMusicTier::AshFrenzyCombat:
            return { 0.1f, 0.6f, 1.0f };
        default:
            return { 0.3f, 1.0f, 0.0f };
    }
}

void SlewWeightsInternal(float unscaledDt) noexcept {
    const float rate = s_InTransition ? s_CurrentTransitionSlewRate : s_SlewRate;
    const float maxStep = rate * std::max(0.0f, unscaledDt);
    bool allTargetsReached = true;

    if (s_Mode == MusicMode::SynchronizedStems) {
        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            const float diff = s_StemTargetWeights[i] - s_StemWeights[i];
            if (std::abs(diff) <= maxStep) {
                s_StemWeights[i] = s_StemTargetWeights[i];
            } else if (diff > 0.0f) {
                s_StemWeights[i] += maxStep;
                allTargetsReached = false;
            } else {
                s_StemWeights[i] -= maxStep;
                allTargetsReached = false;
            }
            s_StemWeights[i] = std::clamp(s_StemWeights[i], 0.0f, 1.0f);
        }

        // Slew single track down to 0 if crossfading
        if (s_SingleTrackWeight > 0.0f) {
            s_SingleTrackWeight = std::max(0.0f, s_SingleTrackWeight - maxStep);
            if (s_SingleTrackWeight > 0.0f) {
                allTargetsReached = false;
            }
            if (s_SingleTrackWeight == 0.0f && s_SingleTrack && s_SingleTrack->IsPlaying()) {
                s_SingleTrack->Pause();
            }
        }
    } else if (s_Mode == MusicMode::SingleTrack) {
        const float diff = s_SingleTrackTargetWeight - s_SingleTrackWeight;
        if (std::abs(diff) <= maxStep) {
            s_SingleTrackWeight = s_SingleTrackTargetWeight;
        } else if (diff > 0.0f) {
            s_SingleTrackWeight += maxStep;
            allTargetsReached = false;
        } else {
            s_SingleTrackWeight -= maxStep;
            allTargetsReached = false;
        }
        s_SingleTrackWeight = std::clamp(s_SingleTrackWeight, 0.0f, 1.0f);

        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            if (s_StemWeights[i] > 0.0f) {
                s_StemWeights[i] = std::max(0.0f, s_StemWeights[i] - maxStep);
                if (s_StemWeights[i] > 0.0f) {
                    allTargetsReached = false;
                }
                if (s_StemWeights[i] == 0.0f && s_Stems[i] && s_Stems[i]->IsPlaying()) {
                    s_Stems[i]->Stop();
                }
            }
        }
    } else if (s_Mode == MusicMode::Stopped) {
        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            s_StemWeights[i] = std::max(0.0f, s_StemWeights[i] - maxStep);
            if (s_StemWeights[i] > 0.0f) {
                allTargetsReached = false;
            }
            if (s_StemWeights[i] == 0.0f && s_Stems[i] && s_Stems[i]->IsPlaying()) {
                s_Stems[i]->Stop();
            }
        }
        if (s_SingleTrackWeight > 0.0f) {
            s_SingleTrackWeight = std::max(0.0f, s_SingleTrackWeight - maxStep);
            if (s_SingleTrackWeight > 0.0f) {
                allTargetsReached = false;
            }
            if (s_SingleTrackWeight == 0.0f && s_SingleTrack && s_SingleTrack->IsPlaying()) {
                s_SingleTrack->Stop();
            }
        }
    }

    if (s_InTransition && allTargetsReached) {
        s_InTransition = false;
    }
}

void ApplyGainsInternal() noexcept {
    const float masterBus = AudioManager::GetBusVolume(AudioBus::Master);
    const float musicBus = AudioManager::GetBusVolume(AudioBus::Music);
    const float baseGain = masterBus * musicBus * s_MasterVolume;

    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (s_Stems[i] && s_Stems[i]->IsOpen()) {
            const float gain = baseGain * s_StemWeights[i];
            s_Stems[i]->SetGain(gain);
        }
    }

    if (s_SingleTrack && s_SingleTrack->IsOpen()) {
        const float gain = baseGain * s_SingleTrackWeight;
        s_SingleTrack->SetGain(gain);
    }
}

void RefillSynchronizedStemsInternal() {
    if (s_Mode != MusicMode::SynchronizedStems) {
        return;
    }

    bool allOpen = true;
    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (!s_Stems[i] || !s_Stems[i]->IsOpen() || !s_Stems[i]->IsPlaying()) {
            allOpen = false;
            break;
        }
    }
    if (!allOpen) {
        return;
    }

    uint32_t minQueued = s_Stems[0]->GetQueuedFrames();
    for (size_t i = 1; i < MusicDirector::kNumStems; ++i) {
        minQueued = std::min(minQueued, s_Stems[i]->GetQueuedFrames());
    }

    constexpr uint32_t targetFrames = AudioStreamTrack::kDefaultTargetBufferedFrames;
    if (minQueued < targetFrames) {
        const uint32_t need = targetFrames - minQueued;
        const uint32_t framesToPump = ((need + AudioStreamTrack::kChunkFrames - 1) / AudioStreamTrack::kChunkFrames)
                                      * AudioStreamTrack::kChunkFrames;

        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            s_Stems[i]->PumpFrames(framesToPump);
        }
    }
}

} // namespace

void MusicDirector::PlaySynchronizedStems(const std::array<std::string, kNumStems>& oggFilepaths, float fadeInSec) {
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    std::lock_guard lock(s_MusicMutex);
    EnsureInitializedLocked();
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    // Pass 1: Open, attach device stream (unbound), seek, and pre-buffer all stems in lockstep
    for (size_t i = 0; i < kNumStems; ++i) {
        s_Stems[i]->Close();
        if (!s_Stems[i]->OpenFile(oggFilepaths[i], /*loop=*/true)) {
            ENGINE_CORE_ERROR("MusicDirector: Failed to open stem {} ('{}')", i, oggFilepaths[i]);
            return;
        }
        s_Stems[i]->SetAutoUpdate(false);
        if (deviceId != 0) {
            s_Stems[i]->AttachDevice(deviceId);
        }
        s_Stems[i]->SeekToStart();
        s_Stems[i]->PumpFrames(AudioStreamTrack::kDefaultTargetBufferedFrames);
    }

    // Pass 2: Play and bind simultaneously
    for (size_t i = 0; i < kNumStems; ++i) {
        s_Stems[i]->Play();
    }

    s_CombatTier = EvaluateCombatTier(s_TorchFuel, s_NearbyEnemyCount);
    s_StemTargetWeights = GetTierTargetWeights(s_CombatTier);

    if (fadeInSec > 0.0f) {
        s_StemWeights.fill(0.0f);
        s_CurrentTransitionSlewRate = 1.0f / std::max(0.01f, fadeInSec);
        s_InTransition = true;
    } else {
        s_StemWeights = s_StemTargetWeights;
        s_InTransition = false;
    }

    s_Mode = MusicMode::SynchronizedStems;
    ApplyGainsInternal();
    ENGINE_CORE_INFO("MusicDirector: Started 3 synchronized stems in tier {}", static_cast<int>(s_CombatTier));
}

void MusicDirector::PlaySynchronizedStemsFromMemory(
    const std::array<std::span<const uint8_t>, kNumStems>& oggBuffers,
    float fadeInSec,
    const std::array<std::string, kNumStems>& debugNames)
{
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    std::lock_guard lock(s_MusicMutex);
    EnsureInitializedLocked();
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    // Pass 1: Open, attach device stream (unbound), seek, and pre-buffer all stems in lockstep
    for (size_t i = 0; i < kNumStems; ++i) {
        s_Stems[i]->Close();
        if (!s_Stems[i]->OpenMemory(oggBuffers[i], /*loop=*/true, debugNames[i])) {
            ENGINE_CORE_ERROR("MusicDirector: Failed to open stem {} from memory ('{}')", i, debugNames[i]);
            return;
        }
        s_Stems[i]->SetAutoUpdate(false);
        if (deviceId != 0) {
            s_Stems[i]->AttachDevice(deviceId);
        }
        s_Stems[i]->SeekToStart();
        s_Stems[i]->PumpFrames(AudioStreamTrack::kDefaultTargetBufferedFrames);
    }

    // Pass 2: Play and bind simultaneously
    for (size_t i = 0; i < kNumStems; ++i) {
        s_Stems[i]->Play();
    }

    s_CombatTier = EvaluateCombatTier(s_TorchFuel, s_NearbyEnemyCount);
    s_StemTargetWeights = GetTierTargetWeights(s_CombatTier);

    if (fadeInSec > 0.0f) {
        s_StemWeights.fill(0.0f);
        s_CurrentTransitionSlewRate = 1.0f / std::max(0.01f, fadeInSec);
        s_InTransition = true;
    } else {
        s_StemWeights = s_StemTargetWeights;
        s_InTransition = false;
    }

    s_Mode = MusicMode::SynchronizedStems;
    ApplyGainsInternal();
    ENGINE_CORE_INFO("MusicDirector: Started 3 synchronized memory stems in tier {}", static_cast<int>(s_CombatTier));
}

void MusicDirector::PlaySingleTrack(const std::string& oggFilepath, float crossfadeSec) {
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    std::lock_guard lock(s_MusicMutex);
    EnsureInitializedLocked();
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    s_SingleTrack->Close();
    if (!s_SingleTrack->OpenFile(oggFilepath, /*loop=*/true)) {
        ENGINE_CORE_ERROR("MusicDirector: Failed to open single track '{}'", oggFilepath);
        return;
    }
    s_SingleTrack->SetAutoUpdate(true);
    if (deviceId != 0) {
        s_SingleTrack->AttachDevice(deviceId);
    }
    s_SingleTrack->SeekToStart();
    s_SingleTrack->Play();

    s_SingleTrackTargetWeight = 1.0f;
    if (crossfadeSec > 0.0f) {
        s_SingleTrackWeight = 0.0f;
        s_CurrentTransitionSlewRate = 1.0f / std::max(0.01f, crossfadeSec);
        s_InTransition = true;
    } else {
        s_SingleTrackWeight = 1.0f;
        for (auto& stem : s_Stems) {
            if (stem && stem->IsPlaying()) {
                stem->Stop();
            }
        }
        s_StemWeights.fill(0.0f);
        s_InTransition = false;
    }

    s_Mode = MusicMode::SingleTrack;
    ApplyGainsInternal();
    ENGINE_CORE_INFO("MusicDirector: Playing single track '{}'", oggFilepath);
}

void MusicDirector::PlaySingleTrackFromMemory(std::span<const uint8_t> oggBuffer,
                                              float crossfadeSec,
                                              const std::string& debugName)
{
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    std::lock_guard lock(s_MusicMutex);
    EnsureInitializedLocked();
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    s_SingleTrack->Close();
    if (!s_SingleTrack->OpenMemory(oggBuffer, /*loop=*/true, debugName)) {
        ENGINE_CORE_ERROR("MusicDirector: Failed to open single track from memory '{}'", debugName);
        return;
    }
    s_SingleTrack->SetAutoUpdate(true);
    if (deviceId != 0) {
        s_SingleTrack->AttachDevice(deviceId);
    }
    s_SingleTrack->SeekToStart();
    s_SingleTrack->Play();

    s_SingleTrackTargetWeight = 1.0f;
    if (crossfadeSec > 0.0f) {
        s_SingleTrackWeight = 0.0f;
        s_CurrentTransitionSlewRate = 1.0f / std::max(0.01f, crossfadeSec);
        s_InTransition = true;
    } else {
        s_SingleTrackWeight = 1.0f;
        for (auto& stem : s_Stems) {
            if (stem && stem->IsPlaying()) {
                stem->Stop();
            }
        }
        s_StemWeights.fill(0.0f);
        s_InTransition = false;
    }

    s_Mode = MusicMode::SingleTrack;
    ApplyGainsInternal();
    ENGINE_CORE_INFO("MusicDirector: Playing single memory track '{}'", debugName);
}

void MusicDirector::UpdateCombatState(float torchFuel01, uint32_t nearbyEnemyCount, float unscaledDt) {
    std::lock_guard lock(s_MusicMutex);
    s_TorchFuel = std::clamp(torchFuel01, 0.0f, 1.0f);
    s_NearbyEnemyCount = nearbyEnemyCount;

    s_CombatTier = EvaluateCombatTier(s_TorchFuel, s_NearbyEnemyCount);
    if (s_Mode == MusicMode::SynchronizedStems) {
        s_StemTargetWeights = GetTierTargetWeights(s_CombatTier);
    }

    SlewWeightsInternal(unscaledDt);
    ApplyGainsInternal();
    RefillSynchronizedStemsInternal();
}

void MusicDirector::Update(float unscaledDt) {
    std::lock_guard lock(s_MusicMutex);
    SlewWeightsInternal(unscaledDt);
    ApplyGainsInternal();
    RefillSynchronizedStemsInternal();
}

void MusicDirector::Stop(float fadeOutSec) {
    std::lock_guard lock(s_MusicMutex);
    if (fadeOutSec <= 0.0f) {
        s_StemWeights.fill(0.0f);
        s_StemTargetWeights.fill(0.0f);
        s_SingleTrackWeight = 0.0f;
        s_SingleTrackTargetWeight = 0.0f;
        for (auto& stem : s_Stems) {
            if (stem) {
                stem->Stop();
            }
        }
        if (s_SingleTrack) {
            s_SingleTrack->Stop();
        }
        s_InTransition = false;
        s_Mode = MusicMode::Stopped;
    } else {
        s_StemTargetWeights.fill(0.0f);
        s_SingleTrackTargetWeight = 0.0f;
        s_CurrentTransitionSlewRate = 1.0f / std::max(0.01f, fadeOutSec);
        s_InTransition = true;
        s_Mode = MusicMode::Stopped;
    }
    ApplyGainsInternal();
}

void MusicDirector::Shutdown() {
    std::lock_guard lock(s_MusicMutex);
    for (auto& stem : s_Stems) {
        if (stem) {
            stem->Close();
            stem.reset();
        }
    }
    if (s_SingleTrack) {
        s_SingleTrack->Close();
        s_SingleTrack.reset();
    }

    s_Mode = MusicMode::Stopped;
    s_CombatTier = CombatMusicTier::MediumTorchExploration;
    s_StemWeights.fill(0.0f);
    s_StemTargetWeights.fill(0.0f);
    s_SingleTrackWeight = 0.0f;
    s_SingleTrackTargetWeight = 0.0f;
    s_TorchFuel = 1.0f;
    s_NearbyEnemyCount = 0;
    s_SlewRate = kDefaultSlewRate;
    s_CurrentTransitionSlewRate = kDefaultSlewRate;
    s_InTransition = false;
    s_MasterVolume = 1.0f;
}

MusicMode MusicDirector::GetMode() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_Mode;
}

CombatMusicTier MusicDirector::GetCombatTier() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_CombatTier;
}

float MusicDirector::GetTorchFuel() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_TorchFuel;
}

uint32_t MusicDirector::GetNearbyEnemyCount() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_NearbyEnemyCount;
}

float MusicDirector::GetStemWeight(size_t stemIndex) noexcept {
    std::lock_guard lock(s_MusicMutex);
    if (stemIndex >= kNumStems) {
        return 0.0f;
    }
    return s_StemWeights[stemIndex];
}

float MusicDirector::GetStemTargetWeight(size_t stemIndex) noexcept {
    std::lock_guard lock(s_MusicMutex);
    if (stemIndex >= kNumStems) {
        return 0.0f;
    }
    return s_StemTargetWeights[stemIndex];
}

float MusicDirector::GetStemGain(size_t stemIndex) noexcept {
    std::lock_guard lock(s_MusicMutex);
    if (stemIndex >= kNumStems || !s_Stems[stemIndex]) {
        return 0.0f;
    }
    return s_Stems[stemIndex]->GetGain();
}

uint64_t MusicDirector::GetStemFrame(size_t stemIndex) noexcept {
    std::lock_guard lock(s_MusicMutex);
    if (stemIndex >= kNumStems || !s_Stems[stemIndex]) {
        return 0u;
    }
    return s_Stems[stemIndex]->GetCurrentFrame();
}

float MusicDirector::GetSingleTrackWeight() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_SingleTrackWeight;
}

float MusicDirector::GetSingleTrackGain() noexcept {
    std::lock_guard lock(s_MusicMutex);
    if (!s_SingleTrack) {
        return 0.0f;
    }
    return s_SingleTrack->GetGain();
}

bool MusicDirector::IsPlayingStems() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_Mode == MusicMode::SynchronizedStems;
}

bool MusicDirector::IsPlayingSingleTrack() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_Mode == MusicMode::SingleTrack;
}

void MusicDirector::SetSlewRate(float rate) noexcept {
    std::lock_guard lock(s_MusicMutex);
    s_SlewRate = std::max(0.01f, rate);
}

float MusicDirector::GetSlewRate() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_SlewRate;
}

void MusicDirector::SetMasterMusicVolume(float volume01) noexcept {
    std::lock_guard lock(s_MusicMutex);
    s_MasterVolume = std::clamp(volume01, 0.0f, 1.0f);
    ApplyGainsInternal();
}

float MusicDirector::GetMasterMusicVolume() noexcept {
    std::lock_guard lock(s_MusicMutex);
    return s_MasterVolume;
}

} // namespace Engine
