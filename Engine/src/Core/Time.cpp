#include "Engine/Core/Time.h"
#include "Engine/Utils/Logger.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace Engine {

float Time::s_DeltaTime = 0.0f;
float Time::s_UnscaledDeltaTime = 0.0f;
float Time::s_TotalTime = 0.0f;
float Time::s_UnscaledTotalTime = 0.0f;
float Time::s_TimeScale = 1.0f;
float Time::s_FixedDeltaTime = 1.0f / 60.0f;

float Time::s_HitStopTimer = 0.0f;
float Time::s_HitStopTimeScale = 0.02f;
float Time::s_PreHitStopTimeScale = 1.0f;

float Time::s_MockDeltaTime = 0.0f;
bool Time::s_UseMockDeltaTime = false;
uint64_t Time::s_LastFrameTime = 0;

void Time::Init() {
    s_LastFrameTime = SDL_GetPerformanceCounter();
    s_DeltaTime = 0.0f;
    s_UnscaledDeltaTime = 0.0f;
    s_TotalTime = 0.0f;
    s_UnscaledTotalTime = 0.0f;
    s_TimeScale = 1.0f;
    s_FixedDeltaTime = 1.0f / 60.0f;
    s_HitStopTimer = 0.0f;
    s_HitStopTimeScale = 0.02f;
    s_PreHitStopTimeScale = 1.0f;
    s_MockDeltaTime = 0.0f;
    s_UseMockDeltaTime = false;
    ENGINE_CORE_INFO("Time system initialized");
}

void Time::SetTimeScale(float scale) noexcept {
    const float clamped = std::clamp(scale, 0.0f, 4.0f);
    if (clamped != scale) {
        ENGINE_CORE_WARN("Time::SetTimeScale clamped {} -> {}", scale, clamped);
    } else {
        ENGINE_CORE_DEBUG("Time::SetTimeScale set to {:.3f}", clamped);
    }
    s_TimeScale = clamped;
    s_PreHitStopTimeScale = clamped;
}

void Time::SetFixedDeltaTime(float fixedDt) noexcept {
    if (fixedDt > 0.0001f) {
        s_FixedDeltaTime = fixedDt;
        ENGINE_CORE_DEBUG("Time::SetFixedDeltaTime set to {:.5f}s", fixedDt);
    } else {
        ENGINE_CORE_WARN("Time::SetFixedDeltaTime ignored invalid value {}", fixedDt);
    }
}

void Time::TriggerHitStop(float unscaledDurationSeconds, float hitScale) noexcept {
    if (unscaledDurationSeconds <= 0.0f) {
        ENGINE_CORE_WARN("Time::TriggerHitStop ignored non-positive duration {}", unscaledDurationSeconds);
        return;
    }
    if (s_HitStopTimer <= 0.0f) {
        s_PreHitStopTimeScale = s_TimeScale;
    }
    s_HitStopTimer = unscaledDurationSeconds;
    s_HitStopTimeScale = std::clamp(hitScale, 0.0f, 4.0f);
    ENGINE_CORE_DEBUG("Time::TriggerHitStop active for {:.3f}s at scale {:.3f}",
                      s_HitStopTimer, s_HitStopTimeScale);
}

void Time::SetMockDeltaTime(float dt) noexcept {
    s_MockDeltaTime = dt;
    s_UseMockDeltaTime = (dt >= 0.0f);
    ENGINE_CORE_DEBUG("Time::SetMockDeltaTime enabled={} (dt={:.5f}s)", s_UseMockDeltaTime, dt);
}

void Time::Reset() noexcept {
    s_DeltaTime = 0.0f;
    s_UnscaledDeltaTime = 0.0f;
    s_TotalTime = 0.0f;
    s_UnscaledTotalTime = 0.0f;
    s_TimeScale = 1.0f;
    s_FixedDeltaTime = 1.0f / 60.0f;
    s_HitStopTimer = 0.0f;
    s_HitStopTimeScale = 0.02f;
    s_PreHitStopTimeScale = 1.0f;
    s_MockDeltaTime = 0.0f;
    s_UseMockDeltaTime = false;
    s_LastFrameTime = SDL_GetPerformanceCounter();
    ENGINE_CORE_DEBUG("Time system reset");
}

void Time::Update() {
    float rawDelta = 0.0f;
    if (s_UseMockDeltaTime) {
        rawDelta = s_MockDeltaTime;
    } else {
        uint64_t currentFrameTime = SDL_GetPerformanceCounter();
        rawDelta = static_cast<float>(currentFrameTime - s_LastFrameTime) / static_cast<float>(SDL_GetPerformanceFrequency());
        s_LastFrameTime = currentFrameTime;
    }

    // Clamp frame delta to 0.10s to prevent spiral-of-death on extreme hitches
    if (rawDelta > 0.10f) {
        ENGINE_CORE_TRACE("Time::Update clamped rawDelta {:.4f}s -> 0.1000s", rawDelta);
        rawDelta = 0.10f;
    }
    s_UnscaledDeltaTime = rawDelta;
    s_UnscaledTotalTime += rawDelta;

    // Decay hit-stop if active using unscaled delta time
    float effectiveTimeScale = s_TimeScale;
    if (s_HitStopTimer > 0.0f) {
        effectiveTimeScale = s_HitStopTimeScale;
        s_HitStopTimer -= rawDelta;
        if (s_HitStopTimer <= 0.0f) {
            s_HitStopTimer = 0.0f;
            // Restore previous time scale
            s_TimeScale = s_PreHitStopTimeScale;
            ENGINE_CORE_DEBUG("Time: Hit-stop expired; restored timeScale to {:.3f}", s_TimeScale);
        }
    }

    s_DeltaTime = s_UnscaledDeltaTime * effectiveTimeScale;
    s_TotalTime += s_DeltaTime;

    ENGINE_CORE_TRACE("Time::Update - DeltaTime: {:.4f}s, UnscaledDelta: {:.4f}s, Scale: {:.2f}, Total: {:.2f}s",
                      s_DeltaTime, s_UnscaledDeltaTime, effectiveTimeScale, s_TotalTime);
}

} // namespace Engine
