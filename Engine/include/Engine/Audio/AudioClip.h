#ifndef AUDIO_AUDIOCLIP_H
#define AUDIO_AUDIOCLIP_H

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

enum class AudioFormat : uint8_t {
    Unknown = 0,
    WAV,
    OGG
};

class AudioClip {
public:
    explicit AudioClip(const std::string& filepath);
    ~AudioClip() = default;

    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;
    AudioClip(AudioClip&&) noexcept = default;
    AudioClip& operator=(AudioClip&&) noexcept = default;

    /// @brief Decodes a WAV or OGG Vorbis audio clip directly from an in-memory byte buffer.
    [[nodiscard]] static std::shared_ptr<AudioClip> CreateFromMemory(
        std::span<const uint8_t> data,
        const std::string& debugName = "<memory>");

    /// @brief Sniffs audio format from leading magic bytes ("OggS" / "RIFF"+"WAVE") with extension fallback.
    [[nodiscard]] static AudioFormat DetectFormat(std::span<const uint8_t> headerBytes,
                                                  std::string_view filepath = {}) noexcept;

    [[nodiscard]] bool IsValid() const noexcept {
        return !m_Buffer.empty() && m_Spec.freq > 0 && m_Spec.channels > 0;
    }

    [[nodiscard]] AudioFormat GetSourceFormat() const noexcept { return m_SourceFormat; }
    [[nodiscard]] const SDL_AudioSpec& GetSpec() const noexcept { return m_Spec; }
    [[nodiscard]] const Uint8* GetBuffer() const noexcept { return m_Buffer.empty() ? nullptr : m_Buffer.data(); }
    [[nodiscard]] Uint32 GetLength() const noexcept { return static_cast<Uint32>(m_Buffer.size()); }
    [[nodiscard]] int GetSampleRate() const noexcept { return m_Spec.freq; }
    [[nodiscard]] int GetChannels() const noexcept { return m_Spec.channels; }
    [[nodiscard]] uint64_t GetFrameCount() const noexcept { return m_FrameCount; }
    [[nodiscard]] float GetDurationSeconds() const noexcept;
    [[nodiscard]] const std::string& GetFilepath() const noexcept { return m_Filepath; }

private:
    AudioClip() = default;

    bool LoadFromFile(const std::string& filepath);
    bool LoadFromMemory(std::span<const uint8_t> data, const std::string& debugName);
    bool DecodeWavFile(const std::string& filepath);
    bool DecodeWavMemory(std::span<const uint8_t> data, const std::string& debugName);
    bool DecodeOggFile(const std::string& filepath);
    bool DecodeOggMemory(std::span<const uint8_t> data, const std::string& debugName);
    void ComputeFrameCount() noexcept;

    std::string m_Filepath;
    AudioFormat m_SourceFormat = AudioFormat::Unknown;
    SDL_AudioSpec m_Spec{};
    std::vector<uint8_t> m_Buffer;
    uint64_t m_FrameCount = 0;
};

} // namespace Engine

#endif // AUDIO_AUDIOCLIP_H
