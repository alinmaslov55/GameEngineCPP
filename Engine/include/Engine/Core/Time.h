#ifndef CORE_TIME_H
#define CORE_TIME_H

#include <cstdint>

namespace Engine {
class Time {
public:
    static void Init();
    static void Update();

    [[nodiscard]] static float GetDeltaTime() noexcept { return s_DeltaTime; }
    [[nodiscard]] static float GetUnscaledDeltaTime() noexcept { return s_UnscaledDeltaTime; }
    [[nodiscard]] static float GetTotalTime() noexcept { return s_TotalTime; }
    [[nodiscard]] static float GetUnscaledTotalTime() noexcept { return s_UnscaledTotalTime; }

    static void SetTimeScale(float scale) noexcept;
    [[nodiscard]] static float GetTimeScale() noexcept {
        return (s_HitStopTimer > 0.0f) ? s_HitStopTimeScale : s_TimeScale;
    }
    [[nodiscard]] static float GetBaseTimeScale() noexcept { return s_TimeScale; }

    static void SetFixedDeltaTime(float fixedDt) noexcept;
    [[nodiscard]] static float GetFixedDeltaTime() noexcept { return s_FixedDeltaTime; }

    static void TriggerHitStop(float unscaledDurationSeconds, float hitScale = 0.02f) noexcept;
    [[nodiscard]] static bool IsInHitStop() noexcept { return s_HitStopTimer > 0.0f; }

    // Testing helper for deterministic time simulation in test suites
    static void SetMockDeltaTime(float dt) noexcept;
    static void Reset() noexcept;

private:
    static float s_DeltaTime;
    static float s_UnscaledDeltaTime;
    static float s_TotalTime;
    static float s_UnscaledTotalTime;
    static float s_TimeScale;
    static float s_FixedDeltaTime;

    static float s_HitStopTimer;
    static float s_HitStopTimeScale;
    static float s_PreHitStopTimeScale;

    static float s_MockDeltaTime;
    static bool s_UseMockDeltaTime;
    static uint64_t s_LastFrameTime;
};
} // namespace Engine

#endif // CORE_TIME_H
