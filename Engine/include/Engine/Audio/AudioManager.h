#ifndef AUDIO_AUDIOMANAGER_H
#define AUDIO_AUDIOMANAGER_H

#include <SDL3/SDL.h>

namespace Engine {

class AudioStreamTrack;

class AudioManager {
public:
    static void Init();
    static void Shutdown();
    static void Update();

    [[nodiscard]] static bool IsInitialized() noexcept;

    // Internal stream-track registration (automatically called by AudioStreamTrack)
    static void RegisterStreamTrack(AudioStreamTrack* track);
    static void UnregisterStreamTrack(AudioStreamTrack* track);
};

} // namespace Engine

#endif // AUDIO_AUDIOMANAGER_H
