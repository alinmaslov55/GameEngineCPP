#ifndef AUDIO_AUDIOSOURCE_H
#define AUDIO_AUDIOSOURCE_H
#include "Engine/Scene/Component.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Utils/Logger.h"
#include <memory>

namespace Engine {
class AudioSource : public Component {
public:
    AudioSource() = default;
    
    ~AudioSource() {
        if (m_Stream) {
            ENGINE_CORE_DEBUG("AudioSource destroyed, closing audio stream");
            SDL_DestroyAudioStream(m_Stream);
        }
    }

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    void SetClip(const std::shared_ptr<AudioClip>& clip) {
        m_Clip = clip;
        
        if (m_Stream) {
            SDL_DestroyAudioStream(m_Stream);
            m_Stream = nullptr;
        }

        if (!m_Clip || !m_Clip->IsValid()) {
            ENGINE_CORE_WARN("AudioSource: SetClip called with invalid or empty clip");
            return;
        }

        m_Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &m_Clip->GetSpec(), nullptr, nullptr);
        if (!m_Stream) {
            ENGINE_CORE_ERROR("Failed to open audio stream: {}", SDL_GetError());
        } else {
            ENGINE_CORE_DEBUG("AudioSource: Audio stream opened successfully");
        }
    }

    void Play() {
        if (!m_Clip || !m_Clip->IsValid() || !m_Stream) {
            ENGINE_CORE_WARN("AudioSource::Play failed: No valid clip or audio stream");
            return;
        }
        if (SDL_GetAudioStreamAvailable(m_Stream) > 0) return;
        
        ENGINE_CORE_TRACE("AudioSource: Playing audio");
        SDL_PutAudioStreamData(m_Stream, m_Clip->GetBuffer(), m_Clip->GetLength());
        SDL_ResumeAudioStreamDevice(m_Stream);
    }

    void Stop() {
        if (m_Stream) {
            ENGINE_CORE_TRACE("AudioSource: Stopping audio stream");
            SDL_ClearAudioStream(m_Stream);
            SDL_PauseAudioStreamDevice(m_Stream);
        }
    }

    const std::shared_ptr<AudioClip>& GetClip() const { return m_Clip; }

private:
    std::shared_ptr<AudioClip> m_Clip{nullptr};
    SDL_AudioStream* m_Stream{nullptr};
};
} // namespace Engine

#endif // AUDIO_AUDIOSOURCE_H