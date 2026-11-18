#include "Engine/Audio/AudioClip.h"
#include "Engine/Utils/Logger.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

namespace Engine {

namespace {

bool EndsWithCaseInsensitive(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    const size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(text[offset + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

} // namespace

AudioClip::AudioClip(const std::string& filepath)
    : m_Filepath(filepath) {
    LoadFromFile(filepath);
}

std::shared_ptr<AudioClip> AudioClip::CreateFromMemory(std::span<const uint8_t> data,
                                                       const std::string& debugName) {
    auto clip = std::shared_ptr<AudioClip>(new AudioClip());
    clip->m_Filepath = debugName;
    clip->LoadFromMemory(data, debugName);
    return clip;
}

AudioFormat AudioClip::DetectFormat(std::span<const uint8_t> headerBytes,
                                    std::string_view filepath) noexcept {
    // 1. Prefer magic bytes over file extension
    if (headerBytes.size() >= 4u &&
        headerBytes[0] == 'O' && headerBytes[1] == 'g' &&
        headerBytes[2] == 'g' && headerBytes[3] == 'S') {
        return AudioFormat::OGG;
    }
    if (headerBytes.size() >= 12u &&
        headerBytes[0] == 'R' && headerBytes[1] == 'I' &&
        headerBytes[2] == 'F' && headerBytes[3] == 'F' &&
        headerBytes[8] == 'W' && headerBytes[9] == 'A' &&
        headerBytes[10] == 'V' && headerBytes[11] == 'E') {
        return AudioFormat::WAV;
    }

    // 2. Fallback to file extension
    if (EndsWithCaseInsensitive(filepath, ".ogg")) {
        return AudioFormat::OGG;
    }
    if (EndsWithCaseInsensitive(filepath, ".wav")) {
        return AudioFormat::WAV;
    }

    return AudioFormat::Unknown;
}

float AudioClip::GetDurationSeconds() const noexcept {
    if (m_Spec.freq <= 0 || m_FrameCount == 0u) {
        return 0.0f;
    }
    return static_cast<float>(static_cast<double>(m_FrameCount) / static_cast<double>(m_Spec.freq));
}

bool AudioClip::LoadFromFile(const std::string& filepath) {
    m_Buffer.clear();
    m_Spec = {};
    m_FrameCount = 0;
    m_SourceFormat = AudioFormat::Unknown;

    if (filepath.empty()) {
        ENGINE_CORE_ERROR("AudioClip: Empty filepath provided");
        return false;
    }

    ENGINE_CORE_INFO("Loading AudioClip from: '{}'", filepath);

    // Sniff magic bytes from the start of the file
    std::array<uint8_t, 12> magic{};
    size_t bytesRead = 0;
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            ENGINE_CORE_ERROR("AudioClip: Failed to open audio file '{}'", filepath);
            return false;
        }
        file.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        bytesRead = static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()));
    }

    const AudioFormat format = DetectFormat(std::span<const uint8_t>(magic.data(), bytesRead), filepath);
    if (format == AudioFormat::OGG) {
        return DecodeOggFile(filepath);
    }
    if (format == AudioFormat::WAV) {
        return DecodeWavFile(filepath);
    }

    // Fallback: try WAV first, then OGG
    if (DecodeWavFile(filepath)) {
        return true;
    }
    if (DecodeOggFile(filepath)) {
        return true;
    }

    ENGINE_CORE_ERROR("AudioClip: Unrecognized or unsupported audio format for '{}'", filepath);
    return false;
}

bool AudioClip::LoadFromMemory(std::span<const uint8_t> data, const std::string& debugName) {
    m_Buffer.clear();
    m_Spec = {};
    m_FrameCount = 0;
    m_SourceFormat = AudioFormat::Unknown;

    if (data.empty()) {
        ENGINE_CORE_ERROR("AudioClip: Empty memory buffer for '{}'", debugName);
        return false;
    }

    const AudioFormat format = DetectFormat(data, debugName);
    if (format == AudioFormat::OGG) {
        return DecodeOggMemory(data, debugName);
    }
    if (format == AudioFormat::WAV) {
        return DecodeWavMemory(data, debugName);
    }

    ENGINE_CORE_ERROR("AudioClip: Unrecognized memory audio format for '{}'", debugName);
    return false;
}

bool AudioClip::DecodeWavFile(const std::string& filepath) {
    SDL_AudioSpec spec{};
    Uint8* wavBuf = nullptr;
    Uint32 wavLen = 0;

    if (!SDL_LoadWAV(filepath.c_str(), &spec, &wavBuf, &wavLen)) {
        ENGINE_CORE_ERROR("AudioClip: Failed to load WAV '{}': {}", filepath, SDL_GetError());
        return false;
    }

    if (!wavBuf || wavLen == 0 || spec.freq <= 0 || spec.channels <= 0) {
        if (wavBuf) {
            SDL_free(wavBuf);
        }
        ENGINE_CORE_ERROR("AudioClip: WAV '{}' decoded empty or invalid spec", filepath);
        return false;
    }

    m_Buffer.resize(wavLen);
    std::memcpy(m_Buffer.data(), wavBuf, wavLen);
    SDL_free(wavBuf);

    m_Spec = spec;
    m_SourceFormat = AudioFormat::WAV;
    ComputeFrameCount();

    ENGINE_CORE_INFO("AudioClip loaded WAV '{}': {} bytes, {} frames, {} Hz, {} ch ({:.2f}s)",
                     filepath, m_Buffer.size(), m_FrameCount, m_Spec.freq, m_Spec.channels, GetDurationSeconds());
    return true;
}

bool AudioClip::DecodeWavMemory(std::span<const uint8_t> data, const std::string& debugName) {
    SDL_IOStream* io = SDL_IOFromConstMem(data.data(), data.size());
    if (!io) {
        ENGINE_CORE_ERROR("AudioClip: SDL_IOFromConstMem failed for '{}': {}", debugName, SDL_GetError());
        return false;
    }

    SDL_AudioSpec spec{};
    Uint8* wavBuf = nullptr;
    Uint32 wavLen = 0;
    const bool ok = SDL_LoadWAV_IO(io, true, &spec, &wavBuf, &wavLen);
    if (!ok || !wavBuf || wavLen == 0 || spec.freq <= 0 || spec.channels <= 0) {
        if (wavBuf) {
            SDL_free(wavBuf);
        }
        ENGINE_CORE_ERROR("AudioClip: Failed to decode WAV from memory '{}': {}", debugName, SDL_GetError());
        return false;
    }

    m_Buffer.resize(wavLen);
    std::memcpy(m_Buffer.data(), wavBuf, wavLen);
    SDL_free(wavBuf);

    m_Spec = spec;
    m_SourceFormat = AudioFormat::WAV;
    ComputeFrameCount();

    ENGINE_CORE_DEBUG("AudioClip decoded WAV from memory '{}': {} frames, {} Hz, {} ch",
                      debugName, m_FrameCount, m_Spec.freq, m_Spec.channels);
    return true;
}

bool AudioClip::DecodeOggFile(const std::string& filepath) {
    int channels = 0;
    int sampleRate = 0;
    short* pcm = nullptr;

    const int frames = stb_vorbis_decode_filename(filepath.c_str(), &channels, &sampleRate, &pcm);
    if (frames <= 0 || !pcm || channels <= 0 || sampleRate <= 0) {
        if (pcm) {
            std::free(pcm);
        }
        ENGINE_CORE_ERROR("AudioClip: Failed to decode OGG Vorbis file '{}' (frames={})", filepath, frames);
        return false;
    }

    const uint64_t totalBytes = static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels) * sizeof(int16_t);
    if (totalBytes > static_cast<uint64_t>(std::numeric_limits<Uint32>::max())) {
        std::free(pcm);
        ENGINE_CORE_ERROR("AudioClip: OGG '{}' exceeds maximum 4GB buffer size", filepath);
        return false;
    }

    m_Buffer.resize(static_cast<size_t>(totalBytes));
    std::memcpy(m_Buffer.data(), pcm, static_cast<size_t>(totalBytes));
    std::free(pcm);

    m_Spec.format = SDL_AUDIO_S16;
    m_Spec.channels = channels;
    m_Spec.freq = sampleRate;
    m_SourceFormat = AudioFormat::OGG;
    m_FrameCount = static_cast<uint64_t>(frames);

    ENGINE_CORE_INFO("AudioClip loaded OGG '{}': {} bytes, {} frames, {} Hz, {} ch ({:.2f}s)",
                     filepath, m_Buffer.size(), m_FrameCount, m_Spec.freq, m_Spec.channels, GetDurationSeconds());
    return true;
}

bool AudioClip::DecodeOggMemory(std::span<const uint8_t> data, const std::string& debugName) {
    if (data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        ENGINE_CORE_ERROR("AudioClip: OGG memory buffer '{}' exceeds int size limit", debugName);
        return false;
    }

    int channels = 0;
    int sampleRate = 0;
    short* pcm = nullptr;

    const int frames = stb_vorbis_decode_memory(
        data.data(),
        static_cast<int>(data.size()),
        &channels,
        &sampleRate,
        &pcm
    );
    if (frames <= 0 || !pcm || channels <= 0 || sampleRate <= 0) {
        if (pcm) {
            std::free(pcm);
        }
        ENGINE_CORE_ERROR("AudioClip: Failed to decode OGG Vorbis from memory '{}' (frames={})", debugName, frames);
        return false;
    }

    const uint64_t totalBytes = static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels) * sizeof(int16_t);
    m_Buffer.resize(static_cast<size_t>(totalBytes));
    std::memcpy(m_Buffer.data(), pcm, static_cast<size_t>(totalBytes));
    std::free(pcm);

    m_Spec.format = SDL_AUDIO_S16;
    m_Spec.channels = channels;
    m_Spec.freq = sampleRate;
    m_SourceFormat = AudioFormat::OGG;
    m_FrameCount = static_cast<uint64_t>(frames);

    ENGINE_CORE_DEBUG("AudioClip decoded OGG from memory '{}': {} frames, {} Hz, {} ch",
                      debugName, m_FrameCount, m_Spec.freq, m_Spec.channels);
    return true;
}

void AudioClip::ComputeFrameCount() noexcept {
    const uint32_t bytesPerSample = SDL_AUDIO_BYTESIZE(m_Spec.format);
    const uint64_t frameBytes = static_cast<uint64_t>(bytesPerSample) * static_cast<uint64_t>(std::max(0, m_Spec.channels));
    if (frameBytes > 0u) {
        m_FrameCount = static_cast<uint64_t>(m_Buffer.size()) / frameBytes;
    } else {
        m_FrameCount = 0u;
    }
}

} // namespace Engine
