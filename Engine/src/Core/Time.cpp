#include "Engine/Core/Time.h"
#include <SDL3/SDL.h>

namespace Engine {
float Time::s_DeltaTime = 0.0f;
float Time::s_TotalTime = 0.0f;
uint64_t Time::s_LastFrameTime = 0;

void Time::Init() {
    s_LastFrameTime = SDL_GetPerformanceCounter();
}

void Time::Update() {
    uint64_t currentFrameTime = SDL_GetPerformanceCounter();

    s_DeltaTime = static_cast<float>(currentFrameTime - s_LastFrameTime) / static_cast<float>(SDL_GetPerformanceFrequency());

    if (s_DeltaTime > 0.1f) {
        s_DeltaTime = 0.1f;
    }

    s_LastFrameTime = currentFrameTime;
    s_TotalTime += s_DeltaTime;
}
}
