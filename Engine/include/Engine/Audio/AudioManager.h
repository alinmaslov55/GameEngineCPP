#ifndef AUDIO_AUDIOMANAGER_H
#define AUDIO_AUDIOMANAGER_H
#include <SDL3/SDL.h>
#include "Engine/Utils/Logger.h"

namespace Engine {
class AudioManager {
public:
    static void Init() {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            ENGINE_CORE_ERROR("Failed to initialize SDL Audio: {}", SDL_GetError());
        }
    }

    static void Shutdown() {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};
} // namespace Engine

#endif // AUDIO_AUDIOMANAGER_H
