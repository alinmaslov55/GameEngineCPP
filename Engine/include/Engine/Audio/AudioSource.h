#ifndef AUDIO_AUDIOSOURCE_H
#define AUDIO_AUDIOSOURCE_H

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <memory>

namespace Engine {

class AudioSource final : public Component {
public:
    AudioSource() = default;

    ~AudioSource() override {
        Stop();
    }

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    void OnDisable() override {
        Stop();
    }

    void OnDestroy() override {
        Stop();
    }

    void OnPoolRelease() override {
        Stop();
    }

    void SetClip(const std::shared_ptr<AudioClip>& clip) {
        m_Clip = clip;
        if (!m_Clip || !m_Clip->IsValid()) {
            ENGINE_CORE_WARN("AudioSource: SetClip called with invalid or empty clip");
        }
    }

    void Play() {
        if (!m_Clip || !m_Clip->IsValid()) {
            ENGINE_CORE_WARN("AudioSource::Play failed: No valid clip");
            return;
        }

        if (m_ActiveVoice != kInvalidVoiceHandle && AudioManager::IsVoicePlaying(m_ActiveVoice)) {
            AudioManager::StopVoice(m_ActiveVoice);
            m_ActiveVoice = kInvalidVoiceHandle;
        }

        SoundPlayParams params = m_Params;
        if (params.SpatializedXZ) {
            if (auto* go = GetGameObject()) {
                if (auto* transform = go->GetTransform()) {
                    params.WorldPos = transform->GetWorldPosition();
                }
            }
        }

        m_ActiveVoice = AudioManager::PlayOneShot(m_Clip, params);
    }

    void Stop() {
        if (m_ActiveVoice != kInvalidVoiceHandle) {
            AudioManager::StopVoice(m_ActiveVoice);
            m_ActiveVoice = kInvalidVoiceHandle;
        }
    }

    void Pause() {
        if (m_ActiveVoice != kInvalidVoiceHandle) {
            AudioManager::PauseVoice(m_ActiveVoice);
        }
    }

    void Resume() {
        if (m_ActiveVoice != kInvalidVoiceHandle) {
            AudioManager::ResumeVoice(m_ActiveVoice);
        }
    }

    [[nodiscard]] bool IsPlaying() const noexcept {
        if (m_ActiveVoice == kInvalidVoiceHandle) {
            return false;
        }
        return AudioManager::IsVoicePlaying(m_ActiveVoice);
    }

    [[nodiscard]] const std::shared_ptr<AudioClip>& GetClip() const noexcept { return m_Clip; }
    [[nodiscard]] VoiceHandle GetActiveVoice() const noexcept { return m_ActiveVoice; }

    void SetBus(AudioBus bus) noexcept { m_Params.Bus = bus; }
    [[nodiscard]] AudioBus GetBus() const noexcept { return m_Params.Bus; }

    void SetVolume(float volume) noexcept { m_Params.Volume = volume; }
    [[nodiscard]] float GetVolume() const noexcept { return m_Params.Volume; }

    void SetPitchRange(float minPitch, float maxPitch) noexcept {
        m_Params.PitchMin = minPitch;
        m_Params.PitchMax = maxPitch;
    }
    [[nodiscard]] float GetPitchMin() const noexcept { return m_Params.PitchMin; }
    [[nodiscard]] float GetPitchMax() const noexcept { return m_Params.PitchMax; }

    void SetLooping(bool loop) noexcept { m_Params.Loop = loop; }
    [[nodiscard]] bool IsLooping() const noexcept { return m_Params.Loop; }

    void SetSpatialized(bool spatial) noexcept { m_Params.SpatializedXZ = spatial; }
    [[nodiscard]] bool IsSpatialized() const noexcept { return m_Params.SpatializedXZ; }

    void SetAttenuationDistances(float minDistance, float maxDistance) noexcept {
        m_Params.MinDistance = minDistance;
        m_Params.MaxDistance = maxDistance;
    }
    [[nodiscard]] float GetMinDistance() const noexcept { return m_Params.MinDistance; }
    [[nodiscard]] float GetMaxDistance() const noexcept { return m_Params.MaxDistance; }

    void SetPriority(uint8_t priority) noexcept { m_Params.Priority = priority; }
    [[nodiscard]] uint8_t GetPriority() const noexcept { return m_Params.Priority; }

    [[nodiscard]] const SoundPlayParams& GetParams() const noexcept { return m_Params; }
    void SetParams(const SoundPlayParams& params) noexcept { m_Params = params; }

private:
    std::shared_ptr<AudioClip> m_Clip{ nullptr };
    SoundPlayParams m_Params;
    VoiceHandle m_ActiveVoice{ kInvalidVoiceHandle };
};

} // namespace Engine

#endif // AUDIO_AUDIOSOURCE_H
