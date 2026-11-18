#include <gtest/gtest.h>
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Audio/AudioStreamTrack.h"
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

} // namespace
