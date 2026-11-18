#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioStreamTrack.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace Engine {

namespace {

std::atomic<bool> s_AudioInitialized{ false };
std::mutex s_StreamRegistryMutex;
std::vector<AudioStreamTrack*> s_ActiveStreams;

} // namespace

void AudioManager::Init() {
    if (s_AudioInitialized.load(std::memory_order_acquire)) {
        return;
    }

    ENGINE_CORE_INFO("Initializing AudioManager...");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        ENGINE_CORE_ERROR("Failed to initialize SDL Audio: {}", SDL_GetError());
        return;
    }
    s_AudioInitialized.store(true, std::memory_order_release);
    ENGINE_CORE_INFO("AudioManager initialized successfully");
}

void AudioManager::Shutdown() {
    ENGINE_CORE_INFO("Shutting down AudioManager...");

    {
        std::vector<AudioStreamTrack*> tracksCopy;
        {
            std::lock_guard lock(s_StreamRegistryMutex);
            tracksCopy = s_ActiveStreams;
        }
        for (AudioStreamTrack* track : tracksCopy) {
            if (track) {
                track->ReleaseAudioStream();
            }
        }
    }

    if (s_AudioInitialized.exchange(false, std::memory_order_acq_rel)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    ENGINE_CORE_INFO("AudioManager shut down");
}

void AudioManager::Update() {
    std::vector<AudioStreamTrack*> tracksCopy;
    {
        std::lock_guard lock(s_StreamRegistryMutex);
        if (s_ActiveStreams.empty()) {
            return;
        }
        tracksCopy = s_ActiveStreams;
    }

    for (AudioStreamTrack* track : tracksCopy) {
        if (track && track->IsPlaying()) {
            track->Update();
        }
    }
}

bool AudioManager::IsInitialized() noexcept {
    return s_AudioInitialized.load(std::memory_order_acquire);
}

void AudioManager::RegisterStreamTrack(AudioStreamTrack* track) {
    if (!track) {
        return;
    }
    std::lock_guard lock(s_StreamRegistryMutex);
    if (std::find(s_ActiveStreams.begin(), s_ActiveStreams.end(), track) == s_ActiveStreams.end()) {
        s_ActiveStreams.push_back(track);
    }
}

void AudioManager::UnregisterStreamTrack(AudioStreamTrack* track) {
    if (!track) {
        return;
    }
    std::lock_guard lock(s_StreamRegistryMutex);
    s_ActiveStreams.erase(
        std::remove(s_ActiveStreams.begin(), s_ActiveStreams.end(), track),
        s_ActiveStreams.end()
    );
}

} // namespace Engine
