#include <gtest/gtest.h>
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Scene/GameObject.h"

namespace {

TEST(AudioClipTest, NonExistentFileGracefulFailure) {
    Engine::AudioClip clip("non_existent_audio_path_12345.wav");
    EXPECT_FALSE(clip.IsValid());
    EXPECT_EQ(clip.GetBuffer(), nullptr);
    EXPECT_EQ(clip.GetLength(), 0u);
    EXPECT_EQ(clip.GetFilepath(), "non_existent_audio_path_12345.wav");
}

TEST(AudioManagerTest, LifecycleInitAndShutdown) {
    // SDL audio subsystem init and shutdown idempotency
    Engine::AudioManager::Init();
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

} // namespace

