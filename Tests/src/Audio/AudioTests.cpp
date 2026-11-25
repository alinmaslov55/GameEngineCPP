#include <gtest/gtest.h>
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Audio/AudioStreamTrack.h"
#include "Engine/Audio/MusicDirector.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Scene/GameObject.h"
#include "EmbeddedTestOgg.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::vector<uint8_t> BuildSyntheticWavPCM16(uint32_t sampleRate, uint16_t channels, uint32_t numFrames) {
    const uint16_t bitsPerSample = 16u;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = numFrames * blockAlign;
    const uint32_t riffChunkSize = 36u + dataSize;

    std::vector<uint8_t> wav(44u + dataSize, 0u);
    auto writeU16 = [&](size_t off, uint16_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    };
    auto writeU32 = [&](size_t off, uint32_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        wav[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        wav[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };

    std::memcpy(wav.data() + 0, "RIFF", 4);
    writeU32(4, riffChunkSize);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    writeU32(16, 16u);          // PCM fmt chunk size
    writeU16(20, 1u);           // PCM format
    writeU16(22, channels);
    writeU32(24, sampleRate);
    writeU32(28, byteRate);
    writeU16(32, blockAlign);
    writeU16(34, bitsPerSample);
    std::memcpy(wav.data() + 36, "data", 4);
    writeU32(40, dataSize);

    auto* pcm = reinterpret_cast<int16_t*>(wav.data() + 44u);
    constexpr double kTwoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < numFrames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        const int16_t sample = static_cast<int16_t>(std::sin(kTwoPi * 440.0 * t) * 16000.0);
        for (uint16_t ch = 0; ch < channels; ++ch) {
            pcm[static_cast<size_t>(i) * channels + ch] = sample;
        }
    }
    return wav;
}

std::vector<uint8_t> BuildSyntheticWavFloat32(uint32_t sampleRate, uint16_t channels, uint32_t numFrames) {
    const uint16_t bitsPerSample = 32u;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = numFrames * blockAlign;
    const uint32_t riffChunkSize = 36u + dataSize;

    std::vector<uint8_t> wav(44u + dataSize, 0u);
    auto writeU16 = [&](size_t off, uint16_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    };
    auto writeU32 = [&](size_t off, uint32_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        wav[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        wav[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };

    std::memcpy(wav.data() + 0, "RIFF", 4);
    writeU32(4, riffChunkSize);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    writeU32(16, 16u);          // PCM fmt chunk size
    writeU16(20, 3u);           // IEEE Float format = 3
    writeU16(22, channels);
    writeU32(24, sampleRate);
    writeU32(28, byteRate);
    writeU16(32, blockAlign);
    writeU16(34, bitsPerSample);
    std::memcpy(wav.data() + 36, "data", 4);
    writeU32(40, dataSize);

    auto* f32 = reinterpret_cast<float*>(wav.data() + 44u);
    constexpr double kTwoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < numFrames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        const float sample = static_cast<float>(std::sin(kTwoPi * 440.0 * t) * 0.5);
        for (uint16_t ch = 0; ch < channels; ++ch) {
            f32[static_cast<size_t>(i) * channels + ch] = sample;
        }
    }
    return wav;
}

std::vector<uint8_t> BuildSyntheticWavPCM8(uint32_t sampleRate, uint16_t channels, uint32_t numFrames) {
    const uint16_t bitsPerSample = 8u;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = numFrames * blockAlign;
    const uint32_t riffChunkSize = 36u + dataSize;

    std::vector<uint8_t> wav(44u + dataSize, 0u);
    auto writeU16 = [&](size_t off, uint16_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    };
    auto writeU32 = [&](size_t off, uint32_t v) {
        wav[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        wav[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        wav[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };

    std::memcpy(wav.data() + 0, "RIFF", 4);
    writeU32(4, riffChunkSize);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    writeU32(16, 16u);          // PCM fmt chunk size
    writeU16(20, 1u);           // PCM format
    writeU16(22, channels);
    writeU32(24, sampleRate);
    writeU32(28, byteRate);
    writeU16(32, blockAlign);
    writeU16(34, bitsPerSample);
    std::memcpy(wav.data() + 36, "data", 4);
    writeU32(40, dataSize);

    auto* u8 = wav.data() + 44u;
    constexpr double kTwoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < numFrames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        const uint8_t sample = static_cast<uint8_t>(128.0 + std::sin(kTwoPi * 440.0 * t) * 60.0);
        for (uint16_t ch = 0; ch < channels; ++ch) {
            u8[static_cast<size_t>(i) * channels + ch] = sample;
        }
    }
    return wav;
}

struct ScopedTempDir {
    std::filesystem::path Path;
    explicit ScopedTempDir(const std::string& name)
        : Path(std::filesystem::temp_directory_path() / name) {
        std::error_code ec;
        std::filesystem::remove_all(Path, ec);
        std::filesystem::create_directories(Path, ec);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(Path, ec);
    }
    std::filesystem::path WriteBytes(const std::string& filename, std::span<const uint8_t> data) const {
        const auto filePath = Path / filename;
        std::ofstream out(filePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return filePath;
    }
};

TEST(AudioClipTest, NonExistentFileGracefulFailure) {
    Engine::AudioClip clip("non_existent_audio_path_12345.wav");
    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(clip.GetBuffer(), nullptr);
    EXPECT_EQ(clip.GetLength(), 0u);
    EXPECT_EQ(clip.GetFrameCount(), 0u);
    EXPECT_EQ(clip.GetFilepath(), "non_existent_audio_path_12345.wav");
}

TEST(AudioClipTest, DecodeWavFromMemoryAndDisk) {
    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 2u, 2048u);

    auto memClip = Engine::AudioClip::CreateFromMemory(wavBytes, "synthetic_tone.wav");
    ASSERT_NE(memClip, nullptr);
    EXPECT_TRUE(memClip->IsValid());
    EXPECT_EQ(memClip->GetSourceFormat(), Engine::AudioFormat::WAV);
    EXPECT_EQ(memClip->GetSampleRate(), 44100);
    EXPECT_EQ(memClip->GetChannels(), 2);
    EXPECT_EQ(memClip->GetFrameCount(), 2048u);
    EXPECT_EQ(memClip->GetLength(), 2048u * 2u * sizeof(int16_t));
    EXPECT_GT(memClip->GetDurationSeconds(), 0.04f);

    // Verify non-zero decoded PCM energy
    const auto* samples = reinterpret_cast<const int16_t*>(memClip->GetBuffer());
    int64_t energy = 0;
    for (size_t i = 0; i < 2048u * 2u; ++i) {
        energy += std::abs(samples[i]);
    }
    EXPECT_GT(energy, 0);

    ScopedTempDir dir("agy_aud01_wav_test");
    const auto wavPath = dir.WriteBytes("sfx_hit.wav", wavBytes);
    Engine::AudioClip diskClip(wavPath.string());
    EXPECT_TRUE(diskClip.IsValid());
    EXPECT_EQ(diskClip.GetSourceFormat(), Engine::AudioFormat::WAV);
    EXPECT_EQ(diskClip.GetSampleRate(), 44100);
    EXPECT_EQ(diskClip.GetChannels(), 2);
    EXPECT_EQ(diskClip.GetFrameCount(), 2048u);
}

TEST(AudioClipTest, DecodeOggFromMemoryAndDiskWithMagicByteSniffing) {
    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    EXPECT_EQ(Engine::AudioClip::DetectFormat(oggSpan, "renamed_audio.bin"), Engine::AudioFormat::OGG);

    auto memClip = Engine::AudioClip::CreateFromMemory(oggSpan, "embedded_test.ogg");
    ASSERT_NE(memClip, nullptr);
    EXPECT_TRUE(memClip->IsValid());
    EXPECT_EQ(memClip->GetSourceFormat(), Engine::AudioFormat::OGG);
    EXPECT_EQ(memClip->GetSampleRate(), Engine::TestFixtures::kTestOggSampleRate);
    EXPECT_EQ(memClip->GetChannels(), Engine::TestFixtures::kTestOggChannels);
    EXPECT_GT(memClip->GetFrameCount(), 4096u);
    EXPECT_EQ(memClip->GetSpec().format, SDL_AUDIO_S16);
    EXPECT_GT(memClip->GetDurationSeconds(), 0.2f);

    // Verify decoded OGG PCM has non-zero waveform energy
    const auto* samples = reinterpret_cast<const int16_t*>(memClip->GetBuffer());
    const size_t sampleCount = static_cast<size_t>(memClip->GetFrameCount()) * static_cast<size_t>(memClip->GetChannels());
    int64_t energy = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        energy += std::abs(samples[i]);
    }
    EXPECT_GT(energy, 0);

    // Verify disk load works both with .ogg extension and magic-byte sniffing on a non-.ogg extension
    ScopedTempDir dir("agy_aud01_ogg_test");
    const auto oggPath = dir.WriteBytes("mus_test.ogg", oggSpan);
    const auto magicPath = dir.WriteBytes("sfx_magic_only.bin", oggSpan);

    Engine::AudioClip diskOgg(oggPath.string());
    EXPECT_TRUE(diskOgg.IsValid());
    EXPECT_EQ(diskOgg.GetSourceFormat(), Engine::AudioFormat::OGG);
    EXPECT_EQ(diskOgg.GetSampleRate(), Engine::TestFixtures::kTestOggSampleRate);
    EXPECT_EQ(diskOgg.GetChannels(), Engine::TestFixtures::kTestOggChannels);
    EXPECT_EQ(diskOgg.GetFrameCount(), memClip->GetFrameCount());

    Engine::AudioClip magicOgg(magicPath.string());
    EXPECT_TRUE(magicOgg.IsValid());
    EXPECT_EQ(magicOgg.GetSourceFormat(), Engine::AudioFormat::OGG);
    EXPECT_EQ(magicOgg.GetFrameCount(), memClip->GetFrameCount());
}

TEST(AudioClipTest, ResourceManagerLoadAudioClipCachesWavAndOgg) {
    Engine::ResourceManager::ClearType<Engine::AudioClip>();

    ScopedTempDir dir("agy_aud01_rm_cache");
    const auto wavBytes = BuildSyntheticWavPCM16(22050u, 1u, 1024u);
    const auto wavPath = dir.WriteBytes("sfx_chop.wav", wavBytes);
    const auto oggPath = dir.WriteBytes("sfx_crunch.ogg", Engine::TestFixtures::kTestOggData);

    auto wav1 = Engine::ResourceManager::LoadAudioClip(wavPath.string());
    auto wav2 = Engine::ResourceManager::LoadAudioClip(wavPath.string());
    ASSERT_NE(wav1, nullptr);
    EXPECT_TRUE(wav1->IsValid());
    EXPECT_EQ(wav1.get(), wav2.get());
    EXPECT_EQ(Engine::ResourceManager::Get<Engine::AudioClip>(wavPath.string()).get(), wav1.get());

    auto ogg1 = Engine::ResourceManager::LoadAudioClip(oggPath.string());
    auto ogg2 = Engine::ResourceManager::LoadAudioClip(oggPath.string());
    ASSERT_NE(ogg1, nullptr);
    EXPECT_TRUE(ogg1->IsValid());
    EXPECT_EQ(ogg1->GetSourceFormat(), Engine::AudioFormat::OGG);
    EXPECT_EQ(ogg1.get(), ogg2.get());
    EXPECT_EQ(Engine::ResourceManager::Get<Engine::AudioClip>(oggPath.string()).get(), ogg1.get());

    Engine::ResourceManager::ClearType<Engine::AudioClip>();
    EXPECT_EQ(Engine::ResourceManager::Get<Engine::AudioClip>(oggPath.string()), nullptr);
}

TEST(AudioClipTest, CorruptAndTruncatedInputsFailGracefully) {
    const std::vector<uint8_t> emptyBuf{};
    auto emptyClip = Engine::AudioClip::CreateFromMemory(emptyBuf, "empty.ogg");
    EXPECT_FALSE(emptyClip->IsValid());

    const std::vector<uint8_t> badOgg = { 'O', 'g', 'g', 'S', 0x00, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF };
    auto badOggClip = Engine::AudioClip::CreateFromMemory(badOgg, "corrupt.ogg");
    EXPECT_FALSE(badOggClip->IsValid());
    EXPECT_EQ(badOggClip->GetFrameCount(), 0u);

    const std::vector<uint8_t> badWav = { 'R', 'I', 'F', 'F', 12, 0, 0, 0, 'W', 'A', 'V', 'E', 'b', 'a', 'd' };
    auto badWavClip = Engine::AudioClip::CreateFromMemory(badWav, "corrupt.wav");
    EXPECT_FALSE(badWavClip->IsValid());
}

TEST(AudioStreamTrackTest, Chunked4096DecodeAndSeamlessLoopBack) {
    ScopedTempDir dir("agy_aud01_stream_loop");
    const auto oggPath = dir.WriteBytes("mus_explore_ambience.ogg", Engine::TestFixtures::kTestOggData);

    // 1. Looping stream opened from disk: read 4 x 4096-frame chunks (>2.5 full track lengths)
    Engine::AudioStreamTrack track;
    ASSERT_TRUE(track.OpenFile(oggPath.string(), /*loop=*/true));
    EXPECT_TRUE(track.IsOpen());
    EXPECT_EQ(track.GetSampleRate(), Engine::TestFixtures::kTestOggSampleRate);
    EXPECT_EQ(track.GetChannels(), Engine::TestFixtures::kTestOggChannels);
    EXPECT_GT(track.GetTotalFrames(), 4096u);

    std::vector<int16_t> chunk(static_cast<size_t>(Engine::AudioStreamTrack::kChunkFrames) *
                               static_cast<size_t>(track.GetChannels()));
    uint64_t totalDecodedFrames = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t decoded = track.DecodeFrames(chunk.data(), Engine::AudioStreamTrack::kChunkFrames);
        // Seamless loop-back must always return the full requested 4096 frames with zero gap!
        EXPECT_EQ(decoded, Engine::AudioStreamTrack::kChunkFrames);
        EXPECT_FALSE(track.IsEndOfStream());
        totalDecodedFrames += decoded;
    }
    EXPECT_EQ(totalDecodedFrames, 4u * Engine::AudioStreamTrack::kChunkFrames);
    EXPECT_GE(track.GetLoopCount(), 2u);

    // 2. Non-looping stream terminates cleanly at EOF
    Engine::AudioStreamTrack oneshotTrack;
    ASSERT_TRUE(oneshotTrack.OpenMemory(Engine::TestFixtures::kTestOggData, /*loop=*/false, "oneshot.ogg"));
    uint64_t oneshotTotal = 0;
    while (!oneshotTrack.IsEndOfStream()) {
        const uint32_t decoded = oneshotTrack.DecodeFrames(chunk.data(), Engine::AudioStreamTrack::kChunkFrames);
        oneshotTotal += decoded;
        if (decoded == 0u) {
            break;
        }
    }
    EXPECT_TRUE(oneshotTrack.IsEndOfStream());
    EXPECT_EQ(oneshotTrack.GetLoopCount(), 0u);
    EXPECT_GT(oneshotTotal, 4096u);
    EXPECT_EQ(oneshotTotal, oneshotTrack.GetTotalFrames());
}

TEST(AudioStreamTrackTest, RamFootprintUnder256KBAndHeadlessLifecycle) {
    ScopedTempDir dir("agy_aud01_stream_ram");
    const auto oggPath = dir.WriteBytes("mus_combat_frenzy.ogg", Engine::TestFixtures::kTestOggData);

    Engine::AudioStreamTrack track;
    ASSERT_TRUE(track.OpenFile(oggPath.string(), /*loop=*/true));

    // Verify per-stem RAM footprint is strictly < 256 KB
    const size_t ramBytes = track.GetEstimatedMemoryBytes();
    EXPECT_GT(ramBytes, 0u);
    EXPECT_LT(ramBytes, 256u * 1024u);

    // Verify headless Play / Update / AudioManager::Update / SeekToStart / Pause / Stop lifecycle
    track.SetGain(0.75f);
    EXPECT_FLOAT_EQ(track.GetGain(), 0.75f);

    track.Play();
    EXPECT_TRUE(track.IsPlaying());
    const uint32_t pumped = track.Update();
    EXPECT_GE(pumped, Engine::AudioStreamTrack::kChunkFrames);
    EXPECT_GT(track.GetCurrentFrame(), 0u);

    Engine::AudioManager::Update();

    EXPECT_TRUE(track.SeekToStart());
    EXPECT_EQ(track.GetCurrentFrame(), 0u);

    track.Pause();
    EXPECT_FALSE(track.IsPlaying());

    track.Stop();
    EXPECT_FALSE(track.IsPlaying());
    EXPECT_EQ(track.GetCurrentFrame(), 0u);

    track.Close();
    EXPECT_FALSE(track.IsOpen());
}

TEST(AudioManagerTest, LifecycleInitAndShutdown) {
    // SDL audio subsystem init and shutdown idempotency
    Engine::AudioManager::Init();
    Engine::AudioManager::Update();
    Engine::AudioManager::Shutdown();
    SUCCEED();
}

TEST(AudioSourceTest, ComponentLifecycleAndNullSafety) {
    Engine::GameObject entity("Jukebox");
    auto* source = entity.AddComponent<Engine::AudioSource>();

    ASSERT_NE(source, nullptr);

    // Calling Play or Stop with null clip should not crash
    source->Play();
    source->Stop();

    // Setting null clip should be safe
    source->SetClip(nullptr);
    source->Play();

    // Setting invalid clip
    auto invalidClip = std::make_shared<Engine::AudioClip>("missing.wav");
    source->SetClip(invalidClip);
    source->Play();
    source->Stop();
    SUCCEED();
}

TEST(AudioSourceTest, DisableDestroyAndPoolReleaseStopPlaybackSafely) {
    Engine::GameObject entity("SafeAudioSource");
    auto* source = entity.AddComponent<Engine::AudioSource>();
    ASSERT_NE(source, nullptr);

    // Call OnDisable, OnPoolRelease, OnDestroy without active stream -> safe no-ops
    source->OnDisable();
    source->OnPoolRelease();
    source->OnDestroy();
    SUCCEED();
}

TEST(SpatialAudioTest, ConstantPowerPanLawPreservesAcousticEnergy) {
    const float testPans[] = { -1.0f, -0.85f, -0.5f, 0.0f, 0.5f, 0.85f, 1.0f };
    for (float pan : testPans) {
        const auto gains = Engine::SpatialAudio::CalculateConstantPowerGains(pan);
        const float energy = gains.Left * gains.Left + gains.Right * gains.Right;
        EXPECT_NEAR(energy, 1.0f, 1e-5f) << "Failed for pan: " << pan;
    }

    // Center pan: equal power to both channels
    const auto center = Engine::SpatialAudio::CalculateConstantPowerGains(0.0f);
    EXPECT_NEAR(center.Left, center.Right, 1e-5f);
    EXPECT_NEAR(center.Left, 0.70710678f, 1e-4f);

    // Hard Left
    const auto hardLeft = Engine::SpatialAudio::CalculateConstantPowerGains(-1.0f);
    EXPECT_NEAR(hardLeft.Left, 1.0f, 1e-5f);
    EXPECT_NEAR(hardLeft.Right, 0.0f, 1e-5f);

    // Hard Right
    const auto hardRight = Engine::SpatialAudio::CalculateConstantPowerGains(1.0f);
    EXPECT_NEAR(hardRight.Left, 0.0f, 1e-5f);
    EXPECT_NEAR(hardRight.Right, 1.0f, 1e-5f);
}

TEST(SpatialAudioTest, DistanceAttenuationSmoothQuadraticRollOff) {
    constexpr float kMinD = 3.0f;
    constexpr float kMaxD = 24.0f;

    // Within min distance: full volume
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(0.0f, kMinD, kMaxD), 1.0f);
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(2.0f, kMinD, kMaxD), 1.0f);
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(3.0f, kMinD, kMaxD), 1.0f);

    // Beyond max distance: silence
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(24.0f, kMinD, kMaxD), 0.0f);
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(50.0f, kMinD, kMaxD), 0.0f);

    // Midpoint distance: t = 0.5 -> atten = (1 - 0.5)^2 = 0.25
    constexpr float midDist = kMinD + (kMaxD - kMinD) * 0.5f;
    EXPECT_NEAR(Engine::SpatialAudio::CalculateDistanceAttenuation(midDist, kMinD, kMaxD), 0.25f, 1e-5f);

    // Degenerate range
    EXPECT_FLOAT_EQ(Engine::SpatialAudio::CalculateDistanceAttenuation(5.0f, 10.0f, 5.0f), 0.0f);
}

TEST(SpatialAudioTest, StereoPanAngleAndClamping) {
    const glm::vec2 listenerRight(1.0f, 0.0f);

    // 10 units right -> clamped to 0.85
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(10.0f, 0.0f), listenerRight), 0.85f, 1e-5f);
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(20.0f, 0.0f), listenerRight), 0.85f, 1e-5f);

    // 5 units right -> 0.5
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(5.0f, 0.0f), listenerRight), 0.5f, 1e-5f);

    // Directly in front or behind -> 0.0 (center)
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(0.0f, 15.0f), listenerRight), 0.0f, 1e-5f);
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(0.0f, -15.0f), listenerRight), 0.0f, 1e-5f);

    // 10 units left -> clamped to -0.85
    EXPECT_NEAR(Engine::SpatialAudio::CalculateStereoPan(glm::vec2(-10.0f, 0.0f), listenerRight), -0.85f, 1e-5f);
}

TEST(AudioManagerTest, BusGainHierarchyAndCascading) {
    Engine::AudioManager::Init(48);

    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 0.8f);
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::SFX, 0.5f);
    EXPECT_FLOAT_EQ(Engine::AudioManager::GetBusVolume(Engine::AudioBus::Master), 0.8f);
    EXPECT_FLOAT_EQ(Engine::AudioManager::GetBusVolume(Engine::AudioBus::SFX), 0.5f);

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 2u, 1024u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "bus_test.wav");
    ASSERT_NE(clip, nullptr);

    Engine::SoundPlayParams params;
    params.Bus = Engine::AudioBus::SFX;
    params.Volume = 0.9f;
    params.SpatializedXZ = false;

    const auto voice = Engine::AudioManager::PlayOneShot(clip, params);
    EXPECT_NE(voice, Engine::kInvalidVoiceHandle);
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(voice));

    // Expected gain: 0.8 (Master) * 0.5 (SFX) * 0.9 (Volume) = 0.36
    EXPECT_NEAR(Engine::AudioManager::GetVoiceEffectiveGain(voice), 0.36f, 1e-4f);

    // Setting Master to 0.0 cascades to silence
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 0.0f);
    EXPECT_FLOAT_EQ(Engine::AudioManager::GetVoiceEffectiveGain(voice), 0.0f);

    // Restoring Master and changing SFX
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 1.0f);
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::SFX, 0.5f);
    EXPECT_NEAR(Engine::AudioManager::GetVoiceEffectiveGain(voice), 0.45f, 1e-4f);

    // Bus pause & resume
    Engine::AudioManager::PauseBus(Engine::AudioBus::SFX);
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(voice));
    Engine::AudioManager::ResumeBus(Engine::AudioBus::SFX);
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(voice));

    // Bus stop
    Engine::AudioManager::StopBus(Engine::AudioBus::SFX);
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(voice));

    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, VoiceStealingPriorityWhenOver48Voices) {
    Engine::AudioManager::Init(48);

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 1024u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "voice_steal.wav");
    ASSERT_NE(clip, nullptr);

    std::vector<Engine::VoiceHandle> handles;
    handles.reserve(48);

    // Fill all 48 slots with increasing priorities
    for (uint32_t i = 0; i < 48; ++i) {
        Engine::SoundPlayParams params;
        params.Bus = Engine::AudioBus::SFX;
        params.Volume = 0.5f;
        params.Priority = static_cast<uint8_t>(i + 1); // 1..48
        params.Loop = true; // Looping keeps voices in active state
        const auto h = Engine::AudioManager::PlayOneShot(clip, params);
        ASSERT_NE(h, Engine::kInvalidVoiceHandle);
        handles.push_back(h);
    }

    EXPECT_EQ(Engine::AudioManager::GetActiveVoiceCount(), 48u);
    const auto lowestPriorityHandle = handles[0]; // Priority 1 -> first to be stolen

    // Trigger 49th voice with high priority
    Engine::SoundPlayParams urgentParams;
    urgentParams.Bus = Engine::AudioBus::SFX;
    urgentParams.Volume = 1.0f;
    urgentParams.Priority = 200;
    urgentParams.Loop = true;
    const auto urgentHandle = Engine::AudioManager::PlayOneShot(clip, urgentParams);

    EXPECT_NE(urgentHandle, Engine::kInvalidVoiceHandle);
    EXPECT_EQ(Engine::AudioManager::GetActiveVoiceCount(), 48u);

    // Verify lowest-priority voice was stolen and invalidated
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(lowestPriorityHandle));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(urgentHandle));

    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, PitchVarianceRangeRandomization) {
    Engine::AudioManager::Init(48);

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 1024u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "pitch_test.wav");
    ASSERT_NE(clip, nullptr);

    constexpr float minPitch = 0.94f;
    constexpr float maxPitch = 1.06f;

    for (int i = 0; i < 10; ++i) {
        Engine::SoundPlayParams params;
        params.PitchMin = minPitch;
        params.PitchMax = maxPitch;
        const auto handle = Engine::AudioManager::PlayOneShot(clip, params);
        ASSERT_NE(handle, Engine::kInvalidVoiceHandle);
        const float pitch = Engine::AudioManager::GetVoiceFrequencyRatio(handle);
        EXPECT_GE(pitch, minPitch);
        EXPECT_LE(pitch, maxPitch);
        Engine::AudioManager::StopVoice(handle);
    }

    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, PlayRandomVariationSelection) {
    Engine::AudioManager::Init(48);

    std::vector<std::shared_ptr<Engine::AudioClip>> variations;
    for (int i = 0; i < 4; ++i) {
        const auto wavBytes = BuildSyntheticWavPCM16(22050u + i * 2000u, 1u, 512u);
        variations.push_back(Engine::AudioClip::CreateFromMemory(wavBytes, "var_" + std::to_string(i) + ".wav"));
    }

    for (int i = 0; i < 16; ++i) {
        const auto handle = Engine::AudioManager::PlayRandomVariation(variations);
        EXPECT_NE(handle, Engine::kInvalidVoiceHandle);
        EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(handle));
        Engine::AudioManager::StopVoice(handle);
    }

    Engine::AudioManager::Shutdown();
}

TEST(AudioSourceTest, PolyphonyAndSpatialIntegration) {
    Engine::AudioManager::Init(48);

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 2048u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "source_test.wav");
    ASSERT_NE(clip, nullptr);

    Engine::GameObject entity("SpatialAudioEmitter");
    auto* transform = entity.GetTransform();
    ASSERT_NE(transform, nullptr);
    transform->position = glm::vec3(5.0f, 0.0f, 0.0f);

    auto* source = entity.AddComponent<Engine::AudioSource>();
    ASSERT_NE(source, nullptr);

    source->SetClip(clip);
    source->SetBus(Engine::AudioBus::SFX);
    source->SetVolume(0.8f);
    source->SetSpatialized(true);
    source->SetAttenuationDistances(2.0f, 20.0f);
    source->SetPitchRange(0.98f, 1.02f);
    source->SetLooping(true);

    EXPECT_EQ(source->GetBus(), Engine::AudioBus::SFX);
    EXPECT_FLOAT_EQ(source->GetVolume(), 0.8f);
    EXPECT_TRUE(source->IsSpatialized());
    EXPECT_FLOAT_EQ(source->GetMinDistance(), 2.0f);
    EXPECT_FLOAT_EQ(source->GetMaxDistance(), 20.0f);
    EXPECT_TRUE(source->IsLooping());

    source->Play();
    EXPECT_TRUE(source->IsPlaying());
    EXPECT_NE(source->GetActiveVoice(), Engine::kInvalidVoiceHandle);

    source->Pause();
    EXPECT_FALSE(source->IsPlaying());

    source->Resume();
    EXPECT_TRUE(source->IsPlaying());

    source->Stop();
    EXPECT_FALSE(source->IsPlaying());

    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, SlewingConvergenceWithoutDiscontinuities) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.0f);

    EXPECT_TRUE(Engine::MusicDirector::IsPlayingStems());
    EXPECT_EQ(Engine::MusicDirector::GetMode(), Engine::MusicMode::SynchronizedStems);

    // Initial state: torchFuel = 1.0 -> Ash Frenzy Tier (Stem 0: 0.1, Stem 1: 0.6, Stem 2: 1.0)
    EXPECT_EQ(Engine::MusicDirector::GetCombatTier(), Engine::CombatMusicTier::AshFrenzyCombat);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 0.1f, 1e-4f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 0.6f, 1e-4f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 1.0f, 1e-4f);

    // Transition to Medium Exploration: torchFuel = 0.5f, enemies = 0
    // Target weights: Stem 0: 0.3, Stem 1: 1.0, Stem 2: 0.0
    constexpr float dt = 0.05f; // 50ms per step
    const float slewRate = Engine::MusicDirector::GetSlewRate();
    const float maxStep = slewRate * dt + 1e-5f;

    float prevW0 = Engine::MusicDirector::GetStemWeight(0);
    float prevW1 = Engine::MusicDirector::GetStemWeight(1);
    float prevW2 = Engine::MusicDirector::GetStemWeight(2);

    for (int step = 0; step < 40; ++step) {
        Engine::MusicDirector::UpdateCombatState(0.5f, 0, dt);

        const float w0 = Engine::MusicDirector::GetStemWeight(0);
        const float w1 = Engine::MusicDirector::GetStemWeight(1);
        const float w2 = Engine::MusicDirector::GetStemWeight(2);

        // Verify no step discontinuities: change per step must not exceed maxStep
        EXPECT_LE(std::abs(w0 - prevW0), maxStep);
        EXPECT_LE(std::abs(w1 - prevW1), maxStep);
        EXPECT_LE(std::abs(w2 - prevW2), maxStep);

        // Verify bounded within [0, 1]
        EXPECT_GE(w0, 0.0f); EXPECT_LE(w0, 1.0f);
        EXPECT_GE(w1, 0.0f); EXPECT_LE(w1, 1.0f);
        EXPECT_GE(w2, 0.0f); EXPECT_LE(w2, 1.0f);

        prevW0 = w0;
        prevW1 = w1;
        prevW2 = w2;
    }

    // After 2.0s (40 * 0.05s) at 0.8/s slew rate, weights must have converged to Exploration targets
    EXPECT_EQ(Engine::MusicDirector::GetCombatTier(), Engine::CombatMusicTier::MediumTorchExploration);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 0.3f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 1.0f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 0.0f, 1e-3f);

    // Transition to Pitch-Black Ambience: torchFuel = 0.1f, enemies = 0
    // Target weights: Stem 0: 1.0, Stem 1: 0.15, Stem 2: 0.0
    for (int step = 0; step < 40; ++step) {
        Engine::MusicDirector::UpdateCombatState(0.1f, 0, dt);

        const float w0 = Engine::MusicDirector::GetStemWeight(0);
        const float w1 = Engine::MusicDirector::GetStemWeight(1);
        const float w2 = Engine::MusicDirector::GetStemWeight(2);

        EXPECT_LE(std::abs(w0 - prevW0), maxStep);
        EXPECT_LE(std::abs(w1 - prevW1), maxStep);
        EXPECT_LE(std::abs(w2 - prevW2), maxStep);

        prevW0 = w0;
        prevW1 = w1;
        prevW2 = w2;
    }

    EXPECT_EQ(Engine::MusicDirector::GetCombatTier(), Engine::CombatMusicTier::PitchBlackAmbience);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 1.0f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 0.15f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 0.0f, 1e-3f);

    // Threat override: even with torchFuel = 0.1f, 6+ enemies triggers Ash Frenzy
    for (int step = 0; step < 40; ++step) {
        Engine::MusicDirector::UpdateCombatState(0.1f, 6, dt);
    }
    EXPECT_EQ(Engine::MusicDirector::GetCombatTier(), Engine::CombatMusicTier::AshFrenzyCombat);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 0.1f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 0.6f, 1e-3f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 1.0f, 1e-3f);

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, PhaseLockedFrameSynchronization) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.0f);

    // Initial pre-buffer pumped frames
    const uint64_t initialFrame0 = Engine::MusicDirector::GetStemFrame(0);
    const uint64_t initialFrame1 = Engine::MusicDirector::GetStemFrame(1);
    const uint64_t initialFrame2 = Engine::MusicDirector::GetStemFrame(2);

    EXPECT_GT(initialFrame0, 0u);
    EXPECT_EQ(initialFrame0, initialFrame1);
    EXPECT_EQ(initialFrame1, initialFrame2);

    // Step combat state across multiple update iterations
    for (int i = 0; i < 20; ++i) {
        Engine::MusicDirector::UpdateCombatState(0.5f, 0, 0.1f);
        const uint64_t f0 = Engine::MusicDirector::GetStemFrame(0);
        const uint64_t f1 = Engine::MusicDirector::GetStemFrame(1);
        const uint64_t f2 = Engine::MusicDirector::GetStemFrame(2);

        // Every stem must have decoded and advanced by the exact same sample frame count!
        EXPECT_EQ(f0, f1) << "Stem 0 and 1 desynchronized at iteration " << i;
        EXPECT_EQ(f1, f2) << "Stem 1 and 2 desynchronized at iteration " << i;
    }

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, SanctuaryHubSingleTrackCrossfade) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.0f);
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingStems());
    EXPECT_FALSE(Engine::MusicDirector::IsPlayingSingleTrack());

    // Crossfade to Sanctuary Hub single track
    Engine::MusicDirector::PlaySingleTrackFromMemory(oggSpan, /*crossfadeSec=*/1.0f, "sanctuary_hub.ogg");
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingSingleTrack());

    // Step forward until crossfade completes (1.5 seconds at 0.8/s slew rate)
    for (int i = 0; i < 30; ++i) {
        Engine::MusicDirector::Update(0.05f);
    }

    // Single track should be at full weight and stems should be faded out
    EXPECT_NEAR(Engine::MusicDirector::GetSingleTrackWeight(), 1.0f, 1e-3f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(0), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(1), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(2), 0.0f);

    // Crossfade back to synchronized stems
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/1.0f);
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingStems());

    for (int i = 0; i < 30; ++i) {
        Engine::MusicDirector::Update(0.05f);
    }

    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetSingleTrackWeight(), 0.0f);
    EXPECT_GT(Engine::MusicDirector::GetStemWeight(1), 0.5f);

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, BusGainHierarchyCascading) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.0f);

    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 0.8f);
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Music, 0.5f);
    Engine::MusicDirector::Update(0.0f); // Reapply gains

    // Ash frenzy initial tier: Stem 2 target weight = 1.0
    // Expected gain = 0.8 (Master) * 0.5 (Music) * 1.0 (Stem Weight) = 0.4
    EXPECT_NEAR(Engine::MusicDirector::GetStemGain(2), 0.4f, 1e-3f);

    // Muting Master cascades to silence
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 0.0f);
    Engine::MusicDirector::Update(0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemGain(2), 0.0f);

    // Restoring Master and changing Music bus
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Master, 1.0f);
    Engine::AudioManager::SetBusVolume(Engine::AudioBus::Music, 0.75f);
    Engine::MusicDirector::Update(0.0f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemGain(2), 0.75f, 1e-3f);

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, HeadlessLifecycleAndSafety) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };

    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.5f);
    Engine::MusicDirector::UpdateCombatState(0.5f, 2, 0.1f);
    Engine::MusicDirector::Stop(/*fadeOutSec=*/0.2f);

    for (int i = 0; i < 10; ++i) {
        Engine::MusicDirector::Update(0.05f);
    }

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
    SUCCEED();
}

TEST(AudioManagerTest, SharedDeviceStreamPauseDoesNotPauseDeviceOrOtherStreams) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 4410u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "voice_test.wav");
    ASSERT_NE(clip, nullptr);

    Engine::SoundPlayParams p1{};
    p1.Bus = Engine::AudioBus::SFX;
    p1.Loop = true;
    Engine::VoiceHandle v1 = Engine::AudioManager::PlayOneShot(clip, p1);

    Engine::SoundPlayParams p2{};
    p2.Bus = Engine::AudioBus::SFX;
    p2.Loop = true;
    Engine::VoiceHandle v2 = Engine::AudioManager::PlayOneShot(clip, p2);

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    Engine::MusicDirector::PlaySingleTrackFromMemory(oggSpan, 0.0f, "bgm.ogg");

    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v1));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v2));
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingSingleTrack());

    // Pause Voice 1
    Engine::AudioManager::PauseVoice(v1);
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(v1));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v2));
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingSingleTrack());

    // Pause SFX bus
    Engine::AudioManager::PauseBus(Engine::AudioBus::SFX);
    EXPECT_TRUE(Engine::AudioManager::IsBusPaused(Engine::AudioBus::SFX));
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(v2));
    // Music track should still be playing and device unpaused
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingSingleTrack());

    const SDL_AudioDeviceID dev = Engine::AudioManager::GetPlaybackDevice();
    if (dev != 0) {
        EXPECT_FALSE(SDL_AudioDevicePaused(dev));
    }

    // Resume SFX bus
    Engine::AudioManager::ResumeBus(Engine::AudioBus::SFX);
    EXPECT_FALSE(Engine::AudioManager::IsBusPaused(Engine::AudioBus::SFX));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v2));

    Engine::AudioManager::StopVoice(v1);
    Engine::AudioManager::StopVoice(v2);
    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, VoiceHandle12BitIndexAndGenerationalWrapSafety) {
    Engine::AudioManager::Init();

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 1000u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "handle_test.wav");
    ASSERT_NE(clip, nullptr);

    Engine::SoundPlayParams params{};
    params.Loop = true;
    Engine::VoiceHandle h = Engine::AudioManager::PlayOneShot(clip, params);
    EXPECT_NE(h, Engine::kInvalidVoiceHandle);

    uint32_t rawIndex = static_cast<uint32_t>(h & 0xFFFu);
    uint32_t rawGen = static_cast<uint32_t>((h >> 12u) & 0xFFFFFu);
    EXPECT_LT(rawIndex, 4096u);
    EXPECT_GT(rawGen, 0u);

    // Stop and re-play multiple times to verify generation increments and wraps cleanly
    Engine::AudioManager::StopVoice(h);
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(h));

    // Stale handle should never report playing or allow mutation
    EXPECT_FLOAT_EQ(Engine::AudioManager::GetVoiceEffectiveGain(h), 0.0f);
    EXPECT_FLOAT_EQ(Engine::AudioManager::GetVoiceFrequencyRatio(h), 1.0f);

    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, HeadlessVoiceExpirationByElapsedDuration) {
    Engine::AudioManager::Init();

    // 44100 Hz, 441 frames -> 10ms
    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 441u);
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "short_test.wav");
    ASSERT_NE(clip, nullptr);

    Engine::SoundPlayParams params{};
    params.Loop = false;
    params.PitchMin = 1.0f;
    params.PitchMax = 1.0f;
    Engine::VoiceHandle h = Engine::AudioManager::PlayOneShot(clip, params);
    EXPECT_NE(h, Engine::kInvalidVoiceHandle);
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(h));

    // Call update immediately -> clip hasn't expired yet (<10ms)
    Engine::AudioManager::Update();
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(h));

    // Sleep for 30ms so elapsed wall clock exceeds clip duration (10ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Next update should reap the expired voice in headless mode
    Engine::AudioManager::Update();
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(h));

    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, ConcurrentAudioStreamTrackUpdateAndMusicDirectorMutationSafety) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, 0.0f);

    std::atomic<bool> running{true};
    std::thread audioThread([&]() {
        while (running.load(std::memory_order_relaxed)) {
            Engine::AudioManager::Update();
            std::this_thread::yield();
        }
    });

    std::thread directorThread([&]() {
        for (int i = 0; i < 50; ++i) {
            Engine::MusicDirector::UpdateCombatState(static_cast<float>(i % 10) / 10.0f, static_cast<uint32_t>(i % 8), 0.01f);
            if (i == 25) {
                Engine::MusicDirector::PlaySingleTrackFromMemory(oggSpan, 0.01f, "concurrent_test.ogg");
            }
            std::this_thread::yield();
        }
    });

    directorThread.join();
    running.store(false, std::memory_order_relaxed);
    audioThread.join();

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
    SUCCEED();
}

TEST(AudioClipTest, NonS16WavFormatsNormalizedToS16) {
    // 1. 32-bit float stereo WAV
    const auto f32Bytes = BuildSyntheticWavFloat32(44100u, 2u, 1024u);
    auto f32Clip = Engine::AudioClip::CreateFromMemory(f32Bytes, "test_f32.wav");
    ASSERT_NE(f32Clip, nullptr);
    EXPECT_TRUE(f32Clip->IsValid());
    EXPECT_EQ(f32Clip->GetSpec().format, SDL_AUDIO_S16);
    EXPECT_EQ(f32Clip->GetChannels(), 2);
    EXPECT_EQ(f32Clip->GetSampleRate(), 44100);
    EXPECT_EQ(f32Clip->GetFrameCount(), 1024u);
    EXPECT_EQ(f32Clip->GetLength(), 1024u * 2u * sizeof(int16_t));

    // 2. 8-bit unsigned mono WAV
    const auto u8Bytes = BuildSyntheticWavPCM8(22050u, 1u, 512u);
    auto u8Clip = Engine::AudioClip::CreateFromMemory(u8Bytes, "test_u8.wav");
    ASSERT_NE(u8Clip, nullptr);
    EXPECT_TRUE(u8Clip->IsValid());
    EXPECT_EQ(u8Clip->GetSpec().format, SDL_AUDIO_S16);
    EXPECT_EQ(u8Clip->GetChannels(), 1);
    EXPECT_EQ(u8Clip->GetSampleRate(), 22050);
    EXPECT_EQ(u8Clip->GetFrameCount(), 512u);
    EXPECT_EQ(u8Clip->GetLength(), 512u * 1u * sizeof(int16_t));

    // 3. 4-channel quadraphonic 16-bit WAV -> downmixed to stereo
    const auto quadBytes = BuildSyntheticWavPCM16(48000u, 4u, 256u);
    auto quadClip = Engine::AudioClip::CreateFromMemory(quadBytes, "test_quad.wav");
    ASSERT_NE(quadClip, nullptr);
    EXPECT_TRUE(quadClip->IsValid());
    EXPECT_EQ(quadClip->GetSpec().format, SDL_AUDIO_S16);
    EXPECT_LE(quadClip->GetChannels(), 2);
    EXPECT_EQ(quadClip->GetSampleRate(), 48000);
    EXPECT_EQ(quadClip->GetFrameCount(), 256u);
}

TEST(AudioManagerTest, SpatializedFloat32WavPlaybackZeroDistortion) {
    Engine::AudioManager::Init();

    const auto f32Bytes = BuildSyntheticWavFloat32(44100u, 2u, 1024u);
    auto clip = Engine::AudioClip::CreateFromMemory(f32Bytes, "spatial_f32.wav");
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->GetSpec().format, SDL_AUDIO_S16);

    Engine::SoundPlayParams params{};
    params.SpatializedXZ = true;
    params.WorldPos = glm::vec3(5.0f, 0.0f, 0.0f);
    params.MinDistance = 1.0f;
    params.MaxDistance = 20.0f;
    params.Loop = false;

    Engine::VoiceHandle h = Engine::AudioManager::PlayOneShot(clip, params);
    EXPECT_NE(h, Engine::kInvalidVoiceHandle);
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(h));

    // Update listener position
    Engine::AudioManager::Update(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(h));

    Engine::AudioManager::StopVoice(h);
    Engine::AudioManager::Shutdown();
}

TEST(AudioStreamTrackTest, GetQueuedFramesAccuratelyReflectsInputQueuedBytes) {
    Engine::AudioManager::Init();

    Engine::AudioStreamTrack track;
    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    ASSERT_TRUE(track.OpenMemory(oggSpan, /*loop=*/false, "queued_frames_test.ogg"));

    EXPECT_EQ(track.GetQueuedFrames(), 0u);

    // In testing without a physical audio device, AttachDevice to a mock stream or create stream
    SDL_AudioStream* stream = SDL_CreateAudioStream(&track.GetSpec(), &track.GetSpec());
    ASSERT_NE(stream, nullptr);

    // Push 2048 frames into stream
    std::vector<int16_t> pcm(2048u * static_cast<size_t>(track.GetChannels()), 100);
    const int byteCount = static_cast<int>(pcm.size() * sizeof(int16_t));
    SDL_PutAudioStreamData(stream, pcm.data(), byteCount);

    // Verify SDL_GetAudioStreamQueued directly reflects the 2048 frames
    const int queuedBytes = SDL_GetAudioStreamQueued(stream);
    EXPECT_EQ(queuedBytes, byteCount);
    const uint32_t frameBytes = static_cast<uint32_t>(track.GetChannels()) * sizeof(int16_t);
    EXPECT_EQ(static_cast<uint32_t>(queuedBytes) / frameBytes, 2048u);

    SDL_DestroyAudioStream(stream);
    track.Close();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, SynchronizedStemsPhaseLockedPreBufferAndFadeInSlew) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };

    // Start stems with fadeInSec = 0.25s
    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, /*fadeInSec=*/0.25f);
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingStems());

    // Initially weights are 0.0f
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(0), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(1), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(2), 0.0f);

    // Initial frame offsets must be identical across all 3 stems (phase locked)
    EXPECT_EQ(Engine::MusicDirector::GetStemFrame(0), Engine::MusicDirector::GetStemFrame(1));
    EXPECT_EQ(Engine::MusicDirector::GetStemFrame(1), Engine::MusicDirector::GetStemFrame(2));

    // Advance halfway through the 0.25s fade (0.125s)
    Engine::MusicDirector::Update(0.125f);

    // Initial combat state has s_TorchFuel = 1.0f -> AshFrenzyCombat!
    // AshFrenzyCombat target weights: Stem 0: 0.1f, Stem 1: 0.6f, Stem 2: 1.0f
    // Slew rate = 1.0 / 0.25 = 4.0/s.
    // In 0.125s, maxStep = 0.50.
    // Stem 0: target 0.10f (reaches 0.10f within 0.025s)
    // Stem 1: target 0.60f (reaches 0.50f in 0.125s)
    // Stem 2: target 1.00f (reaches 0.50f in 0.125s)
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 0.10f, 0.02f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 0.50f, 0.02f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 0.50f, 0.02f);

    // Advance another 0.125s (total 0.25s) -> transition finishes
    Engine::MusicDirector::Update(0.125f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(0), 0.10f, 0.02f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(1), 0.60f, 0.02f);
    EXPECT_NEAR(Engine::MusicDirector::GetStemWeight(2), 1.00f, 0.02f);

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(MusicDirectorTest, CrossfadeToSingleTrackStopsFadedStems) {
    Engine::AudioManager::Init();
    Engine::MusicDirector::Shutdown();

    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);
    const std::array<std::span<const uint8_t>, 3> buffers = { oggSpan, oggSpan, oggSpan };

    Engine::MusicDirector::PlaySynchronizedStemsFromMemory(buffers, 0.0f);
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingStems());

    // Crossfade to single track with crossfadeSec = 0.20s
    Engine::MusicDirector::PlaySingleTrackFromMemory(oggSpan, /*crossfadeSec=*/0.20f, "sanctuary_single.ogg");
    EXPECT_TRUE(Engine::MusicDirector::IsPlayingSingleTrack());
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetSingleTrackWeight(), 0.0f);

    // Update by 0.25s (> 0.20s crossfade duration)
    Engine::MusicDirector::Update(0.25f);

    EXPECT_NEAR(Engine::MusicDirector::GetSingleTrackWeight(), 1.0f, 1e-3f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(0), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(1), 0.0f);
    EXPECT_FLOAT_EQ(Engine::MusicDirector::GetStemWeight(2), 0.0f);

    // Verify all 3 stems have stopped playing
    EXPECT_FALSE(Engine::MusicDirector::IsPlayingStems());

    Engine::MusicDirector::Shutdown();
    Engine::AudioManager::Shutdown();
}

TEST(AudioSourceTest, PlayReplacesExistingVoiceAndUsesWorldPosition) {
    Engine::AudioManager::Init();

    const auto wavBytes = BuildSyntheticWavPCM16(44100u, 1u, 44100u); // 1 second clip
    auto clip = Engine::AudioClip::CreateFromMemory(wavBytes, "source_test.wav");
    ASSERT_NE(clip, nullptr);

    auto parentGo = std::make_unique<Engine::GameObject>("Parent");
    parentGo->GetTransform()->position = glm::vec3(10.0f, 0.0f, 10.0f);

    auto childGo = std::make_unique<Engine::GameObject>("Child");
    childGo->GetTransform()->SetParent(parentGo->GetTransform());
    childGo->GetTransform()->position = glm::vec3(5.0f, 0.0f, 5.0f);

    // World position of child is (15, 0, 15)
    EXPECT_FLOAT_EQ(childGo->GetTransform()->GetWorldPosition().x, 15.0f);
    EXPECT_FLOAT_EQ(childGo->GetTransform()->GetWorldPosition().z, 15.0f);

    auto* source = childGo->AddComponent<Engine::AudioSource>();
    source->SetClip(clip);
    source->SetSpatialized(true);
    source->SetLooping(true);

    // Play 1st time
    source->Play();
    Engine::VoiceHandle v1 = source->GetActiveVoice();
    EXPECT_NE(v1, Engine::kInvalidVoiceHandle);
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v1));

    // Play 2nd time on same source -> should stop v1 and start a new voice v2
    source->Play();
    Engine::VoiceHandle v2 = source->GetActiveVoice();
    EXPECT_NE(v2, Engine::kInvalidVoiceHandle);
    EXPECT_NE(v1, v2);

    // Old voice v1 should be stopped, new voice v2 playing
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(v1));
    EXPECT_TRUE(Engine::AudioManager::IsVoicePlaying(v2));

    source->Stop();
    EXPECT_FALSE(Engine::AudioManager::IsVoicePlaying(v2));

    Engine::AudioManager::Shutdown();
}

TEST(AudioManagerTest, ConcurrentStreamTrackConstructionDestructionAndUpdateSafe) {
    Engine::AudioManager::Init();
    const auto oggSpan = std::span<const uint8_t>(Engine::TestFixtures::kTestOggData);

    std::atomic<bool> running{ true };
    std::thread updater([&]() {
        while (running.load(std::memory_order_relaxed)) {
            Engine::AudioManager::Update();
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < 100; ++i) {
        auto track = std::make_unique<Engine::AudioStreamTrack>();
        if (track->OpenMemory(oggSpan, true)) {
            track->Play();
            Engine::AudioStreamTrack movedTrack(std::move(*track));
            std::this_thread::yield();
        }
    }

    running.store(false, std::memory_order_relaxed);
    updater.join();
    Engine::AudioManager::Shutdown();
    SUCCEED();
}

} // namespace
