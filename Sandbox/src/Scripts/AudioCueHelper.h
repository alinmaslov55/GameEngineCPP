#ifndef SANDBOX_AUDIO_CUE_HELPER_H
#define SANDBOX_AUDIO_CUE_HELPER_H

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Core/ResourceManager.h>

namespace Sandbox {

/// @brief Plays a one-shot audio cue by relative asset path when AudioManager is initialized.
inline void PlaySfxCue(const char* assetRelPath, const Engine::SoundPlayParams& params = {}) {
    if (!assetRelPath || !Engine::AudioManager::IsInitialized()) {
        return;
    }
    if (auto clip = Engine::ResourceManager::Load<Engine::AudioClip>(assetRelPath)) {
        Engine::AudioManager::PlayOneShot(clip, params);
    }
}

} // namespace Sandbox

#endif // SANDBOX_AUDIO_CUE_HELPER_H
