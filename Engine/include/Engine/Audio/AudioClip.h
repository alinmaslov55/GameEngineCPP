#ifndef AUDIO_AUDIOCLIP_H
#define AUDIO_AUDIOCLIP_H
#include <SDL3/SDL.h>
#include <string>
#include "Engine/Utils/Logger.h"

namespace Engine {
class AudioClip {
public:
    AudioClip(const std::string& filepath) {
        ENGINE_CORE_INFO("Loading AudioClip from: '{}'", filepath);
        if (!SDL_LoadWAV(filepath.c_str(), &m_Spec, &m_AudioBuffer, &m_AudioLength)) {
            ENGINE_CORE_ERROR("Failed to load WAV '{}': {}", filepath, SDL_GetError());
        } else {
            ENGINE_CORE_INFO("AudioClip loaded successfully: '{}' ({} bytes, {} Hz, {} channels)",
                             filepath, m_AudioLength, m_Spec.freq, m_Spec.channels);
        }
    }

    ~AudioClip() {
        if (m_AudioBuffer) {
            ENGINE_CORE_DEBUG("AudioClip destroyed ({} bytes freed)", m_AudioLength);
            SDL_free(m_AudioBuffer);
        }
    }

    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    bool IsValid() const { return m_AudioBuffer != nullptr && m_AudioLength > 0; }
    const SDL_AudioSpec& GetSpec() const { return m_Spec; }
    Uint8* GetBuffer() const { return m_AudioBuffer; }
    Uint32 GetLength() const { return m_AudioLength; }

private:
    SDL_AudioSpec m_Spec{};
    Uint8* m_AudioBuffer = nullptr;
    Uint32 m_AudioLength = 0;
};
} // namespace Engine

#endif // AUDIO_AUDIOCLIP_H
