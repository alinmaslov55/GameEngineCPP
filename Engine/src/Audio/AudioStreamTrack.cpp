#include "Engine/Audio/AudioStreamTrack.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

namespace Engine {

AudioStreamTrack::AudioStreamTrack() {
    AudioManager::RegisterStreamTrack(this);
}

AudioStreamTrack::~AudioStreamTrack() {
    AudioManager::UnregisterStreamTrack(this);
    Close();
}

AudioStreamTrack::AudioStreamTrack(AudioStreamTrack&& other) noexcept {
    AudioManager::RegisterStreamTrack(this);
    MoveFrom(std::move(other));
}

AudioStreamTrack& AudioStreamTrack::operator=(AudioStreamTrack&& other) noexcept {
    if (this != &other) {
        Close();
        MoveFrom(std::move(other));
    }
    return *this;
}

void AudioStreamTrack::MoveFrom(AudioStreamTrack&& other) noexcept {
    m_Filepath = std::move(other.m_Filepath);
    m_Vorbis = std::exchange(other.m_Vorbis, nullptr);
    m_Stream = std::exchange(other.m_Stream, nullptr);
    m_AttachedDevice = std::exchange(other.m_AttachedDevice, 0);
    m_OwnsDeviceStream = std::exchange(other.m_OwnsDeviceStream, false);
    m_Spec = other.m_Spec;
    other.m_Spec = {};
    m_OwnedCompressedData = std::move(other.m_OwnedCompressedData);
    m_DecodeBuffer = std::move(other.m_DecodeBuffer);
    m_TotalFrames = std::exchange(other.m_TotalFrames, 0);
    m_CurrentFrame = std::exchange(other.m_CurrentFrame, 0);
    m_LoopCount = std::exchange(other.m_LoopCount, 0);
    m_SetupMemoryBytes = std::exchange(other.m_SetupMemoryBytes, 0);
    m_TargetBufferedFrames = other.m_TargetBufferedFrames;
    m_Gain = other.m_Gain;
    m_Looping = other.m_Looping;
    m_IsPlaying = std::exchange(other.m_IsPlaying, false);
    m_EndOfStream = std::exchange(other.m_EndOfStream, false);
}

bool AudioStreamTrack::OpenFile(const std::string& filepath, bool loop) {
    Close();

    if (filepath.empty()) {
        ENGINE_CORE_ERROR("AudioStreamTrack::OpenFile: Empty filepath");
        return false;
    }

    std::string resolvedPath = filepath;
    if (!std::filesystem::exists(resolvedPath)) {
        const std::string candidate = ResourceManager::ResolvePath(filepath);
        if (!candidate.empty()) {
            resolvedPath = candidate;
        }
    }

    int vorbisErr = 0;
    m_Vorbis = stb_vorbis_open_filename(resolvedPath.c_str(), &vorbisErr, nullptr);
    if (!m_Vorbis) {
        ENGINE_CORE_ERROR("AudioStreamTrack: Failed to open OGG '{}' (stb_vorbis error={})", resolvedPath, vorbisErr);
        return false;
    }

    m_Filepath = resolvedPath;
    if (!FinalizeOpen(loop)) {
        Close();
        return false;
    }

    ENGINE_CORE_INFO("AudioStreamTrack opened '{}': {} Hz, {} ch, {} frames, ~{} KB RAM",
                     m_Filepath, m_Spec.freq, m_Spec.channels, m_TotalFrames,
                     GetEstimatedMemoryBytes() / 1024u);
    return true;
}

bool AudioStreamTrack::OpenMemory(std::span<const uint8_t> oggBytes,
                                  bool loop,
                                  const std::string& debugName) {
    Close();

    if (oggBytes.empty() || oggBytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        ENGINE_CORE_ERROR("AudioStreamTrack::OpenMemory: Invalid buffer size ({}) for '{}'", oggBytes.size(), debugName);
        return false;
    }

    m_OwnedCompressedData.assign(oggBytes.begin(), oggBytes.end());
    int vorbisErr = 0;
    m_Vorbis = stb_vorbis_open_memory(
        m_OwnedCompressedData.data(),
        static_cast<int>(m_OwnedCompressedData.size()),
        &vorbisErr,
        nullptr
    );
    if (!m_Vorbis) {
        m_OwnedCompressedData.clear();
        ENGINE_CORE_ERROR("AudioStreamTrack: Failed to open OGG from memory '{}' (stb_vorbis error={})",
                          debugName, vorbisErr);
        return false;
    }

    m_Filepath = debugName;
    if (!FinalizeOpen(loop)) {
        Close();
        return false;
    }

    ENGINE_CORE_DEBUG("AudioStreamTrack opened memory '{}': {} Hz, {} ch, {} frames",
                      m_Filepath, m_Spec.freq, m_Spec.channels, m_TotalFrames);
    return true;
}

bool AudioStreamTrack::FinalizeOpen(bool loop) {
    if (!m_Vorbis) {
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(m_Vorbis);
    if (info.channels <= 0 || info.sample_rate == 0u) {
        ENGINE_CORE_ERROR("AudioStreamTrack: Invalid OGG stream info (channels={}, rate={}) for '{}'",
                          info.channels, info.sample_rate, m_Filepath);
        return false;
    }

    m_Spec.format = SDL_AUDIO_S16;
    m_Spec.channels = info.channels;
    m_Spec.freq = static_cast<int>(info.sample_rate);
    m_TotalFrames = static_cast<uint64_t>(stb_vorbis_stream_length_in_samples(m_Vorbis));
    m_CurrentFrame = 0;
    m_LoopCount = 0;
    m_SetupMemoryBytes = info.setup_memory_required;
    m_Looping = loop;
    m_IsPlaying = false;
    m_EndOfStream = false;

    m_DecodeBuffer.resize(static_cast<size_t>(kChunkFrames) * static_cast<size_t>(m_Spec.channels));
    return true;
}

void AudioStreamTrack::Close() noexcept {
    ReleaseAudioStream();
    if (m_Vorbis) {
        stb_vorbis_close(m_Vorbis);
        m_Vorbis = nullptr;
    }
    m_OwnedCompressedData.clear();
    m_DecodeBuffer.clear();
    m_Spec = {};
    m_Filepath.clear();
    m_TotalFrames = 0;
    m_CurrentFrame = 0;
    m_LoopCount = 0;
    m_SetupMemoryBytes = 0;
    m_IsPlaying = false;
    m_EndOfStream = false;
}

void AudioStreamTrack::ReleaseAudioStream() noexcept {
    m_IsPlaying = false;
    if (m_Stream) {
        if (!m_OwnsDeviceStream && m_AttachedDevice != 0) {
            SDL_UnbindAudioStream(m_Stream);
        }
        SDL_DestroyAudioStream(m_Stream);
        m_Stream = nullptr;
    }
    m_AttachedDevice = 0;
    m_OwnsDeviceStream = false;
}

bool AudioStreamTrack::AttachDevice(SDL_AudioDeviceID deviceId) {
    if (!m_Vorbis || deviceId == 0) {
        return false;
    }
    ReleaseAudioStream();

    SDL_AudioStream* stream = SDL_CreateAudioStream(&m_Spec, nullptr);
    if (!stream) {
        ENGINE_CORE_ERROR("AudioStreamTrack::AttachDevice: SDL_CreateAudioStream failed: {}", SDL_GetError());
        return false;
    }
    if (!SDL_BindAudioStream(deviceId, stream)) {
        ENGINE_CORE_ERROR("AudioStreamTrack::AttachDevice: SDL_BindAudioStream failed: {}", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return false;
    }

    m_Stream = stream;
    m_AttachedDevice = deviceId;
    m_OwnsDeviceStream = false;
    SDL_SetAudioStreamGain(m_Stream, m_Gain);
    return true;
}

bool AudioStreamTrack::EnsureAudioStream() {
    if (m_Stream) {
        return true;
    }
    if (!m_Vorbis || !AudioManager::IsInitialized()) {
        return false;
    }

    m_Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &m_Spec, nullptr, nullptr);
    if (!m_Stream) {
        // Headless CI / systems without audio hardware gracefully continue decoding without OS playback
        ENGINE_CORE_WARN("AudioStreamTrack: No audio playback device available for '{}': {}", m_Filepath, SDL_GetError());
        return false;
    }

    m_OwnsDeviceStream = true;
    m_AttachedDevice = 0;
    SDL_SetAudioStreamGain(m_Stream, m_Gain);
    return true;
}

void AudioStreamTrack::Play() {
    if (!m_Vorbis) {
        ENGINE_CORE_WARN("AudioStreamTrack::Play called on unopened track");
        return;
    }

    if (m_EndOfStream) {
        SeekToStart();
    }

    m_IsPlaying = true;
    if (EnsureAudioStream()) {
        Update();
        if (m_OwnsDeviceStream) {
            SDL_ResumeAudioStreamDevice(m_Stream);
        }
    }
}

void AudioStreamTrack::Pause() {
    m_IsPlaying = false;
    if (m_Stream && m_OwnsDeviceStream) {
        SDL_PauseAudioStreamDevice(m_Stream);
    }
}

void AudioStreamTrack::Stop() {
    m_IsPlaying = false;
    if (m_Stream) {
        SDL_ClearAudioStream(m_Stream);
        if (m_OwnsDeviceStream) {
            SDL_PauseAudioStreamDevice(m_Stream);
        }
    }
    SeekToStart();
}

uint32_t AudioStreamTrack::DecodeFrames(int16_t* outInterleavedPcm, uint32_t frameCount) {
    if (!m_Vorbis || !outInterleavedPcm || frameCount == 0u || m_Spec.channels <= 0) {
        return 0u;
    }

    const int channels = m_Spec.channels;
    uint32_t totalDecoded = 0u;
    bool wrappedOnEmptyPass = false;

    while (totalDecoded < frameCount) {
        const uint32_t remainingFrames = frameCount - totalDecoded;
        const int maxShorts = static_cast<int>(remainingFrames) * channels;
        int16_t* dst = outInterleavedPcm + static_cast<size_t>(totalDecoded) * static_cast<size_t>(channels);

        const int framesRead = stb_vorbis_get_samples_short_interleaved(m_Vorbis, channels, dst, maxShorts);
        if (framesRead > 0) {
            totalDecoded += static_cast<uint32_t>(framesRead);
            m_CurrentFrame += static_cast<uint64_t>(framesRead);
            wrappedOnEmptyPass = false;
            continue;
        }

        // Reached EOF (or decoder error)
        if (m_Looping) {
            if (wrappedOnEmptyPass) {
                // Guard against empty/corrupt 0-frame streams causing an infinite loop
                m_EndOfStream = true;
                break;
            }
            if (!stb_vorbis_seek_start(m_Vorbis)) {
                m_EndOfStream = true;
                break;
            }
            m_CurrentFrame = 0;
            ++m_LoopCount;
            wrappedOnEmptyPass = true;
        } else {
            m_EndOfStream = true;
            break;
        }
    }

    return totalDecoded;
}

uint32_t AudioStreamTrack::PumpFrames(uint32_t exactFrames) {
    if (!m_Vorbis || exactFrames == 0u || m_Spec.channels <= 0) {
        return 0u;
    }

    const size_t channels = static_cast<size_t>(m_Spec.channels);
    if (m_DecodeBuffer.size() < static_cast<size_t>(kChunkFrames) * channels) {
        m_DecodeBuffer.resize(static_cast<size_t>(kChunkFrames) * channels);
    }

    uint32_t totalPumped = 0u;
    while (totalPumped < exactFrames) {
        const uint32_t batch = std::min(kChunkFrames, exactFrames - totalPumped);
        const uint32_t decoded = DecodeFrames(m_DecodeBuffer.data(), batch);
        if (decoded == 0u) {
            break;
        }

        if (m_Stream) {
            const int byteCount = static_cast<int>(static_cast<size_t>(decoded) * channels * sizeof(int16_t));
            SDL_PutAudioStreamData(m_Stream, m_DecodeBuffer.data(), byteCount);
        }

        totalPumped += decoded;
        if (decoded < batch) {
            break;
        }
    }
    return totalPumped;
}

uint32_t AudioStreamTrack::Update() {
    if (!m_Vorbis || !m_IsPlaying) {
        return 0u;
    }

    if (m_EndOfStream) {
        if (!m_Stream || GetQueuedFrames() == 0u) {
            m_IsPlaying = false;
        }
        return 0u;
    }

    if (!m_Stream) {
        // Headless fallback: advance one 4096-frame chunk when manually updated without an OS stream
        return PumpFrames(kChunkFrames);
    }

    uint32_t totalRefilled = 0u;
    uint32_t queued = GetQueuedFrames();
    while (queued < m_TargetBufferedFrames && !m_EndOfStream) {
        const uint32_t decoded = PumpFrames(kChunkFrames);
        if (decoded == 0u) {
            break;
        }
        totalRefilled += decoded;
        queued = GetQueuedFrames();
    }

    return totalRefilled;
}

bool AudioStreamTrack::SeekToStart() noexcept {
    if (!m_Vorbis) {
        return false;
    }
    const bool ok = (stb_vorbis_seek_start(m_Vorbis) != 0);
    if (ok) {
        m_CurrentFrame = 0;
        m_EndOfStream = false;
        if (m_Stream) {
            SDL_ClearAudioStream(m_Stream);
        }
    }
    return ok;
}

bool AudioStreamTrack::SeekToFrame(uint64_t frameIndex) noexcept {
    if (!m_Vorbis) {
        return false;
    }
    const unsigned int clamped = static_cast<unsigned int>(
        std::min<uint64_t>(frameIndex, static_cast<uint64_t>(std::numeric_limits<unsigned int>::max()))
    );
    const bool ok = (stb_vorbis_seek(m_Vorbis, clamped) != 0);
    if (ok) {
        m_CurrentFrame = clamped;
        m_EndOfStream = false;
        if (m_Stream) {
            SDL_ClearAudioStream(m_Stream);
        }
    }
    return ok;
}

void AudioStreamTrack::SetGain(float gain) noexcept {
    m_Gain = std::max(0.0f, gain);
    if (m_Stream) {
        SDL_SetAudioStreamGain(m_Stream, m_Gain);
    }
}

void AudioStreamTrack::SetTargetBufferedFrames(uint32_t targetFrames) noexcept {
    m_TargetBufferedFrames = std::max(kChunkFrames, targetFrames);
}

uint32_t AudioStreamTrack::GetQueuedFrames() const noexcept {
    if (!m_Stream || m_Spec.channels <= 0) {
        return 0u;
    }
    const int availBytes = SDL_GetAudioStreamAvailable(m_Stream);
    if (availBytes <= 0) {
        return 0u;
    }
    const uint32_t frameBytes = static_cast<uint32_t>(m_Spec.channels) * sizeof(int16_t);
    return (frameBytes > 0u) ? (static_cast<uint32_t>(availBytes) / frameBytes) : 0u;
}

size_t AudioStreamTrack::GetEstimatedMemoryBytes() const noexcept {
    size_t bytes = static_cast<size_t>(m_SetupMemoryBytes);
    bytes += m_DecodeBuffer.capacity() * sizeof(int16_t);
    bytes += m_OwnedCompressedData.capacity();
    if (m_Stream) {
        const int availBytes = SDL_GetAudioStreamAvailable(m_Stream);
        if (availBytes > 0) {
            bytes += static_cast<size_t>(availBytes);
        }
    }
    return bytes;
}

} // namespace Engine
