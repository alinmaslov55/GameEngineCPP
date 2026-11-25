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

struct MusicState {
    std::mutex Mutex;

    MusicMode Mode = MusicMode::Stopped;
    CombatMusicTier CombatTier = CombatMusicTier::MediumTorchExploration;
    float TorchFuel = 1.0f;
    uint32_t NearbyEnemyCount = 0;

    float SlewRate = MusicDirector::kDefaultSlewRate;
    float CurrentTransitionSlewRate = MusicDirector::kDefaultSlewRate;
    bool InTransition = false;
    float MasterVolume = 1.0f;

    // 3 Synchronized Stems
    std::array<std::unique_ptr<AudioStreamTrack>, MusicDirector::kNumStems> Stems;
    std::array<float, MusicDirector::kNumStems> StemWeights = { 0.0f, 0.0f, 0.0f };
    std::array<float, MusicDirector::kNumStems> StemTargetWeights = { 0.3f, 1.0f, 0.0f };

    // Single Track (Sanctuary Hub / Menu)
    std::unique_ptr<AudioStreamTrack> SingleTrack;
    float SingleTrackWeight = 0.0f;
    float SingleTrackTargetWeight = 0.0f;
};

MusicState& GetMusicState() {
    static MusicState state;
    return state;
}

void EnsureInitializedLocked(MusicState& state) {
    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (!state.Stems[i]) {
            state.Stems[i] = std::make_unique<AudioStreamTrack>();
        }
    }
    if (!state.SingleTrack) {
        state.SingleTrack = std::make_unique<AudioStreamTrack>();
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

void SlewWeightsInternal(MusicState& state, float unscaledDt) noexcept {
    const float rate = state.InTransition ? state.CurrentTransitionSlewRate : state.SlewRate;
    const float maxStep = rate * std::max(0.0f, unscaledDt);
    bool allTargetsReached = true;

    if (state.Mode == MusicMode::SynchronizedStems) {
        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            const float diff = state.StemTargetWeights[i] - state.StemWeights[i];
            if (std::abs(diff) <= maxStep) {
                state.StemWeights[i] = state.StemTargetWeights[i];
            } else if (diff > 0.0f) {
                state.StemWeights[i] += maxStep;
                allTargetsReached = false;
            } else {
                state.StemWeights[i] -= maxStep;
                allTargetsReached = false;
            }
            state.StemWeights[i] = std::clamp(state.StemWeights[i], 0.0f, 1.0f);
        }

        // Slew single track down to 0 if crossfading
        if (state.SingleTrackWeight > 0.0f) {
            state.SingleTrackWeight = std::max(0.0f, state.SingleTrackWeight - maxStep);
            if (state.SingleTrackWeight > 0.0f) {
                allTargetsReached = false;
            }
        }
        if (state.SingleTrackWeight == 0.0f && state.SingleTrack && state.SingleTrack->IsPlaying()) {
            state.SingleTrack->Pause();
        }
    } else if (state.Mode == MusicMode::SingleTrack) {
        const float diff = state.SingleTrackTargetWeight - state.SingleTrackWeight;
        if (std::abs(diff) <= maxStep) {
            state.SingleTrackWeight = state.SingleTrackTargetWeight;
        } else if (diff > 0.0f) {
            state.SingleTrackWeight += maxStep;
            allTargetsReached = false;
        } else {
            state.SingleTrackWeight -= maxStep;
            allTargetsReached = false;
        }
        state.SingleTrackWeight = std::clamp(state.SingleTrackWeight, 0.0f, 1.0f);

        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            if (state.StemWeights[i] > 0.0f) {
                state.StemWeights[i] = std::max(0.0f, state.StemWeights[i] - maxStep);
                if (state.StemWeights[i] > 0.0f) {
                    allTargetsReached = false;
                }
            }
            if (state.StemWeights[i] == 0.0f && state.Stems[i] && state.Stems[i]->IsPlaying()) {
                state.Stems[i]->Stop();
            }
        }
    } else if (state.Mode == MusicMode::Stopped) {
        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            state.StemWeights[i] = std::max(0.0f, state.StemWeights[i] - maxStep);
            if (state.StemWeights[i] > 0.0f) {
                allTargetsReached = false;
            }
            if (state.StemWeights[i] == 0.0f && state.Stems[i] && state.Stems[i]->IsPlaying()) {
                state.Stems[i]->Stop();
            }
        }
        if (state.SingleTrackWeight > 0.0f) {
            state.SingleTrackWeight = std::max(0.0f, state.SingleTrackWeight - maxStep);
            if (state.SingleTrackWeight > 0.0f) {
                allTargetsReached = false;
            }
        }
        if (state.SingleTrackWeight == 0.0f && state.SingleTrack && state.SingleTrack->IsPlaying()) {
            state.SingleTrack->Stop();
        }
    }

    if (state.InTransition && allTargetsReached) {
        state.InTransition = false;
    }
}

void ApplyGainsInternal(MusicState& state) noexcept {
    const float masterBus = AudioManager::GetBusVolume(AudioBus::Master);
    const float musicBus = AudioManager::GetBusVolume(AudioBus::Music);
    const float baseGain = masterBus * musicBus * state.MasterVolume;

    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (state.Stems[i] && state.Stems[i]->IsOpen()) {
            const float gain = baseGain * state.StemWeights[i];
            state.Stems[i]->SetGain(gain);
        }
    }

    if (state.SingleTrack && state.SingleTrack->IsOpen()) {
        const float gain = baseGain * state.SingleTrackWeight;
        state.SingleTrack->SetGain(gain);
    }
}

void RefillSynchronizedStemsInternal(MusicState& state) {
    // Keep refilling stems while all 3 are actively playing (including during crossfade/fade-out)
    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (!state.Stems[i] || !state.Stems[i]->IsOpen() || !state.Stems[i]->IsPlaying()) {
            return;
        }
    }

    uint32_t minQueued = state.Stems[0]->GetQueuedFrames();
    for (size_t i = 1; i < MusicDirector::kNumStems; ++i) {
        minQueued = std::min(minQueued, state.Stems[i]->GetQueuedFrames());
    }

    constexpr uint32_t targetFrames = AudioStreamTrack::kDefaultTargetBufferedFrames;
    if (minQueued < targetFrames) {
        const uint32_t need = targetFrames - minQueued;
        const uint32_t framesToPump = ((need + AudioStreamTrack::kChunkFrames - 1) / AudioStreamTrack::kChunkFrames)
                                      * AudioStreamTrack::kChunkFrames;

        for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
            state.Stems[i]->PumpFrames(framesToPump);
        }
    }
}

void CommitSynchronizedStemsLocked(
    MusicState& state,
    std::array<std::unique_ptr<AudioStreamTrack>, MusicDirector::kNumStems>& stagedStems,
    float fadeInSec)
{
    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        if (state.Stems[i]) {
            state.Stems[i]->Close();
        }
        state.Stems[i] = std::move(stagedStems[i]);
    }

    state.CombatTier = EvaluateCombatTier(state.TorchFuel, state.NearbyEnemyCount);
    state.StemTargetWeights = GetTierTargetWeights(state.CombatTier);

    if (fadeInSec > 0.0f) {
        state.StemWeights.fill(0.0f);
        state.CurrentTransitionSlewRate = 1.0f / std::max(0.01f, fadeInSec);
        state.InTransition = true;
    } else {
        state.StemWeights = state.StemTargetWeights;
        if (state.SingleTrack && state.SingleTrack->IsPlaying()) {
            state.SingleTrack->Stop();
        }
        state.SingleTrackWeight = 0.0f;
        state.SingleTrackTargetWeight = 0.0f;
        state.InTransition = false;
    }

    state.Mode = MusicMode::SynchronizedStems;
    // Apply gains BEFORE Play() so streams never start at default gain 1.0 and pop
    ApplyGainsInternal(state);

    for (size_t i = 0; i < MusicDirector::kNumStems; ++i) {
        state.Stems[i]->Play();
    }
}

void CommitSingleTrackLocked(
    MusicState& state,
    std::unique_ptr<AudioStreamTrack>& stagedTrack,
    float crossfadeSec)
{
    if (state.SingleTrack) {
        state.SingleTrack->Close();
    }
    state.SingleTrack = std::move(stagedTrack);

    state.SingleTrackTargetWeight = 1.0f;
    if (crossfadeSec > 0.0f) {
        state.SingleTrackWeight = 0.0f;
        state.CurrentTransitionSlewRate = 1.0f / std::max(0.01f, crossfadeSec);
        state.InTransition = true;
    } else {
        state.SingleTrackWeight = 1.0f;
        for (auto& stem : state.Stems) {
            if (stem && stem->IsPlaying()) {
                stem->Stop();
            }
        }
        state.StemWeights.fill(0.0f);
        state.InTransition = false;
    }

    state.Mode = MusicMode::SingleTrack;
    // Apply gains BEFORE Play() so stream never starts at default gain 1.0 and pops
    ApplyGainsInternal(state);
    state.SingleTrack->Play();
}

} // namespace

void MusicDirector::PlaySynchronizedStems(const std::array<std::string, kNumStems>& oggFilepaths, float fadeInSec) {
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    EnsureInitializedLocked(state);
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    // Stage all 3 stems first so partial open failure leaves existing music state intact
    std::array<std::unique_ptr<AudioStreamTrack>, kNumStems> staged;
    for (size_t i = 0; i < kNumStems; ++i) {
        staged[i] = std::make_unique<AudioStreamTrack>();
        if (!staged[i]->OpenFile(oggFilepaths[i], /*loop=*/true)) {
            ENGINE_CORE_ERROR("MusicDirector: Failed to open stem {} ('{}')", i, oggFilepaths[i]);
            return;
        }
        staged[i]->SetAutoUpdate(false);
        if (deviceId != 0) {
            staged[i]->AttachDevice(deviceId);
        }
        staged[i]->SeekToStart();
        staged[i]->PumpFrames(AudioStreamTrack::kDefaultTargetBufferedFrames);
    }

    CommitSynchronizedStemsLocked(state, staged, fadeInSec);
    ENGINE_CORE_INFO("MusicDirector: Started 3 synchronized stems in tier {}", static_cast<int>(state.CombatTier));
}

void MusicDirector::PlaySynchronizedStemsFromMemory(
    const std::array<std::span<const uint8_t>, kNumStems>& oggBuffers,
    float fadeInSec,
    const std::array<std::string, kNumStems>& debugNames)
{
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    EnsureInitializedLocked(state);
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    std::array<std::unique_ptr<AudioStreamTrack>, kNumStems> staged;
    for (size_t i = 0; i < kNumStems; ++i) {
        staged[i] = std::make_unique<AudioStreamTrack>();
        if (!staged[i]->OpenMemory(oggBuffers[i], /*loop=*/true, debugNames[i])) {
            ENGINE_CORE_ERROR("MusicDirector: Failed to open stem {} from memory ('{}')", i, debugNames[i]);
            return;
        }
        staged[i]->SetAutoUpdate(false);
        if (deviceId != 0) {
            staged[i]->AttachDevice(deviceId);
        }
        staged[i]->SeekToStart();
        staged[i]->PumpFrames(AudioStreamTrack::kDefaultTargetBufferedFrames);
    }

    CommitSynchronizedStemsLocked(state, staged, fadeInSec);
    ENGINE_CORE_INFO("MusicDirector: Started 3 synchronized memory stems in tier {}", static_cast<int>(state.CombatTier));
}

void MusicDirector::PlaySingleTrack(const std::string& oggFilepath, float crossfadeSec) {
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    EnsureInitializedLocked(state);
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    auto staged = std::make_unique<AudioStreamTrack>();
    if (!staged->OpenFile(oggFilepath, /*loop=*/true)) {
        ENGINE_CORE_ERROR("MusicDirector: Failed to open single track '{}'", oggFilepath);
        return;
    }
    staged->SetAutoUpdate(true);
    if (deviceId != 0) {
        staged->AttachDevice(deviceId);
    }
    staged->SeekToStart();

    CommitSingleTrackLocked(state, staged, crossfadeSec);
    ENGINE_CORE_INFO("MusicDirector: Playing single track '{}'", oggFilepath);
}

void MusicDirector::PlaySingleTrackFromMemory(std::span<const uint8_t> oggBuffer,
                                              float crossfadeSec,
                                              const std::string& debugName)
{
    if (!AudioManager::IsInitialized()) {
        AudioManager::Init();
    }

    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    EnsureInitializedLocked(state);
    const SDL_AudioDeviceID deviceId = AudioManager::GetPlaybackDevice();

    auto staged = std::make_unique<AudioStreamTrack>();
    if (!staged->OpenMemory(oggBuffer, /*loop=*/true, debugName)) {
        ENGINE_CORE_ERROR("MusicDirector: Failed to open single track from memory '{}'", debugName);
        return;
    }
    staged->SetAutoUpdate(true);
    if (deviceId != 0) {
        staged->AttachDevice(deviceId);
    }
    staged->SeekToStart();

    CommitSingleTrackLocked(state, staged, crossfadeSec);
    ENGINE_CORE_INFO("MusicDirector: Playing single memory track '{}'", debugName);
}

void MusicDirector::UpdateCombatState(float torchFuel01, uint32_t nearbyEnemyCount, float unscaledDt) {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    state.TorchFuel = std::clamp(torchFuel01, 0.0f, 1.0f);
    state.NearbyEnemyCount = nearbyEnemyCount;

    state.CombatTier = EvaluateCombatTier(state.TorchFuel, state.NearbyEnemyCount);
    if (state.Mode == MusicMode::SynchronizedStems) {
        state.StemTargetWeights = GetTierTargetWeights(state.CombatTier);
    }

    SlewWeightsInternal(state, unscaledDt);
    ApplyGainsInternal(state);
    RefillSynchronizedStemsInternal(state);
}

void MusicDirector::Update(float unscaledDt) {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    SlewWeightsInternal(state, unscaledDt);
    ApplyGainsInternal(state);
    RefillSynchronizedStemsInternal(state);
}

void MusicDirector::Stop(float fadeOutSec) {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (fadeOutSec <= 0.0f) {
        state.StemWeights.fill(0.0f);
        state.StemTargetWeights.fill(0.0f);
        state.SingleTrackWeight = 0.0f;
        state.SingleTrackTargetWeight = 0.0f;
        for (auto& stem : state.Stems) {
            if (stem) {
                stem->Stop();
            }
        }
        if (state.SingleTrack) {
            state.SingleTrack->Stop();
        }
        state.InTransition = false;
        state.Mode = MusicMode::Stopped;
    } else {
        state.StemTargetWeights.fill(0.0f);
        state.SingleTrackTargetWeight = 0.0f;
        state.CurrentTransitionSlewRate = 1.0f / std::max(0.01f, fadeOutSec);
        state.InTransition = true;
        state.Mode = MusicMode::Stopped;
    }
    ApplyGainsInternal(state);
}

void MusicDirector::Shutdown() {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    for (auto& stem : state.Stems) {
        if (stem) {
            stem->Close();
            stem.reset();
        }
    }
    if (state.SingleTrack) {
        state.SingleTrack->Close();
        state.SingleTrack.reset();
    }

    state.Mode = MusicMode::Stopped;
    state.CombatTier = CombatMusicTier::MediumTorchExploration;
    state.StemWeights.fill(0.0f);
    state.StemTargetWeights.fill(0.0f);
    state.SingleTrackWeight = 0.0f;
    state.SingleTrackTargetWeight = 0.0f;
    state.TorchFuel = 1.0f;
    state.NearbyEnemyCount = 0;
    state.SlewRate = kDefaultSlewRate;
    state.CurrentTransitionSlewRate = kDefaultSlewRate;
    state.InTransition = false;
    state.MasterVolume = 1.0f;
}

MusicMode MusicDirector::GetMode() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.Mode;
}

CombatMusicTier MusicDirector::GetCombatTier() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.CombatTier;
}

float MusicDirector::GetTorchFuel() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.TorchFuel;
}

uint32_t MusicDirector::GetNearbyEnemyCount() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.NearbyEnemyCount;
}

float MusicDirector::GetStemWeight(size_t stemIndex) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (stemIndex >= kNumStems) {
        return 0.0f;
    }
    return state.StemWeights[stemIndex];
}

float MusicDirector::GetStemTargetWeight(size_t stemIndex) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (stemIndex >= kNumStems) {
        return 0.0f;
    }
    return state.StemTargetWeights[stemIndex];
}

float MusicDirector::GetStemGain(size_t stemIndex) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (stemIndex >= kNumStems || !state.Stems[stemIndex]) {
        return 0.0f;
    }
    return state.Stems[stemIndex]->GetGain();
}

uint64_t MusicDirector::GetStemFrame(size_t stemIndex) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (stemIndex >= kNumStems || !state.Stems[stemIndex]) {
        return 0u;
    }
    return state.Stems[stemIndex]->GetCurrentFrame();
}

float MusicDirector::GetSingleTrackWeight() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.SingleTrackWeight;
}

float MusicDirector::GetSingleTrackGain() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    if (!state.SingleTrack) {
        return 0.0f;
    }
    return state.SingleTrack->GetGain();
}

bool MusicDirector::IsPlayingStems() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.Mode == MusicMode::SynchronizedStems;
}

bool MusicDirector::IsPlayingSingleTrack() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.Mode == MusicMode::SingleTrack;
}

void MusicDirector::SetSlewRate(float rate) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    state.SlewRate = std::max(0.01f, rate);
}

float MusicDirector::GetSlewRate() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.SlewRate;
}

void MusicDirector::SetMasterMusicVolume(float volume01) noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    state.MasterVolume = std::clamp(volume01, 0.0f, 1.0f);
    ApplyGainsInternal(state);
}

float MusicDirector::GetMasterMusicVolume() noexcept {
    auto& state = GetMusicState();
    std::lock_guard lock(state.Mutex);
    return state.MasterVolume;
}

} // namespace Engine
