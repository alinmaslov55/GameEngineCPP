#ifndef AUDIO_AUDIOMANAGER_H
#define AUDIO_AUDIOMANAGER_H
#include <SDL3/SDL.h>
#include "Engine/Utils/Logger.h"

namespace Engine {
class AudioManager {
public:
    static void Init() {
        ENGINE_CORE_INFO("Initializing AudioManager...");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            ENGINE_CORE_ERROR("Failed to initialize SDL Audio: {}", SDL_GetError());
            return;
        }
        ENGINE_CORE_INFO("AudioManager initialized successfully");
    }

    static void Shutdown() {
        ENGINE_CORE_INFO("Shutting down AudioManager...");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        ENGINE_CORE_INFO("AudioManager shut down");
    }
};
} // namespace Engine

#endif // AUDIO_AUDIOMANAGER_H