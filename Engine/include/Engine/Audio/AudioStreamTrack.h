#ifndef AUDIO_AUDIOSTREAMTRACK_H
#define AUDIO_AUDIOSTREAMTRACK_H

#include <SDL3/SDL.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct stb_vorbis;

namespace Engine {

/// @brief Low-memory streaming OGG Vorbis music decoder (< 256 KB RAM per stem).
/// Decodes incrementally in 4096-frame chunks into an SDL_AudioStream ring buffer
/// with seamless sample-accurate loop-back via stb_vorbis_seek_start.
class AudioStreamTrack {
public:
    static constexpr uint32_t kChunkFrames = 4096u;
    static constexpr uint32_t kDefaultTargetBufferedFrames = 12288u; // 3 chunks (~278ms @ 44.1kHz, ~48 KB S16 stereo)

    AudioStreamTrack();
    ~AudioStreamTrack();

    AudioStreamTrack(const AudioStreamTrack&) = delete;
    AudioStreamTrack& operator=(const AudioStreamTrack&) = delete;
    AudioStreamTrack(AudioStreamTrack&& other) noexcept;
    AudioStreamTrack& operator=(AudioStreamTrack&& other) noexcept;

    /// @brief Opens an OGG Vorbis file from disk for incremental chunked streaming.
    bool OpenFile(const std::string& filepath, bool loop = true);

    /// @brief Opens an OGG Vorbis stream from an in-memory buffer (copies compressed bytes for ownership).
    bool OpenMemory(std::span<const uint8_t> oggBytes,
                    bool loop = true,
                    const std::string& debugName = "<memory>");

    /// @brief Closes the vorbis decoder and releases the SDL audio stream.
    void Close() noexcept;

    /// @brief Releases only the SDL_AudioStream (called on AudioManager::Shutdown).
    void ReleaseAudioStream() noexcept;

    /// @brief Binds this track's SDL_AudioStream to a shared SDL3 playback device (AUD-02 / AUD-03).
    bool AttachDevice(SDL_AudioDeviceID deviceId);

    void Play();
    void Pause();
    void Stop();

    /// @brief Refills the SDL_AudioStream ring buffer in 4096-frame chunks up to targetBufferedFrames.
    /// @return Total PCM frames decoded and pushed during this call.
    uint32_t Update();

    /// @brief Decodes and pushes an exact frame count (used for phase-locked multi-stem AUD-03 playback).
    uint32_t PumpFrames(uint32_t exactFrames);

    /// @brief Decodes up to frameCount interleaved 16-bit PCM frames into outInterleavedPcm.
    /// When looping is enabled and EOF occurs mid-chunk, seeks back to frame 0 seamlessly within the same call.
    uint32_t DecodeFrames(int16_t* outInterleavedPcm, uint32_t frameCount);

    bool SeekToStart() noexcept;
    bool SeekToFrame(uint64_t frameIndex) noexcept;

    void SetLooping(bool loop) noexcept { m_Looping = loop; }
    [[nodiscard]] bool IsLooping() const noexcept { return m_Looping; }

    void SetAutoUpdate(bool enabled) noexcept { m_AutoUpdate = enabled; }
    [[nodiscard]] bool IsAutoUpdateEnabled() const noexcept { return m_AutoUpdate; }

    void SetGain(float gain) noexcept;
    [[nodiscard]] float GetGain() const noexcept { return m_Gain; }

    void SetTargetBufferedFrames(uint32_t targetFrames) noexcept;
    [[nodiscard]] uint32_t GetTargetBufferedFrames() const noexcept { return m_TargetBufferedFrames; }

    [[nodiscard]] bool IsOpen() const noexcept { return m_Vorbis != nullptr; }
    [[nodiscard]] bool IsPlaying() const noexcept { return m_IsPlaying; }
    [[nodiscard]] bool IsEndOfStream() const noexcept { return m_EndOfStream; }
    [[nodiscard]] const SDL_AudioSpec& GetSpec() const noexcept { return m_Spec; }
    [[nodiscard]] int GetSampleRate() const noexcept { return m_Spec.freq; }
    [[nodiscard]] int GetChannels() const noexcept { return m_Spec.channels; }
    [[nodiscard]] uint64_t GetTotalFrames() const noexcept { return m_TotalFrames; }
    [[nodiscard]] uint64_t GetCurrentFrame() const noexcept { return m_CurrentFrame; }
    [[nodiscard]] uint64_t GetLoopCount() const noexcept { return m_LoopCount; }
    [[nodiscard]] uint32_t GetQueuedFrames() const noexcept;
    [[nodiscard]] size_t GetEstimatedMemoryBytes() const noexcept;
    [[nodiscard]] const std::string& GetFilepath() const noexcept { return m_Filepath; }

private:
    bool FinalizeOpen(bool loop);
    bool EnsureAudioStream();
    void MoveFrom(AudioStreamTrack&& other) noexcept;

    std::string m_Filepath;
    stb_vorbis* m_Vorbis = nullptr;
    SDL_AudioStream* m_Stream = nullptr;
    SDL_AudioDeviceID m_AttachedDevice = 0;
    bool m_OwnsDeviceStream = false;

    SDL_AudioSpec m_Spec{};
    std::vector<uint8_t> m_OwnedCompressedData; // Non-empty only for OpenMemory()
    std::vector<int16_t> m_DecodeBuffer;        // Scratch buffer sized to kChunkFrames * channels

    uint64_t m_TotalFrames = 0;
    uint64_t m_CurrentFrame = 0;
    uint64_t m_LoopCount = 0;
    uint32_t m_SetupMemoryBytes = 0;
    uint32_t m_TargetBufferedFrames = kDefaultTargetBufferedFrames;
    float m_Gain = 1.0f;
    bool m_Looping = true;
    bool m_IsPlaying = false;
    bool m_EndOfStream = false;
    bool m_AutoUpdate = true;
};

} // namespace Engine

#endif // AUDIO_AUDIOSTREAMTRACK_H
