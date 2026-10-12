#ifndef CORE_TIME_H
#define CORE_TIME_H

#include <cstdint>

namespace Engine {
class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime() { return s_DeltaTime; }
    static float GetTotalTime() { return s_TotalTime; }

private:
    static float s_DeltaTime;
    static float s_TotalTime;
    static uint64_t s_LastFrameTime;
};
} // namespace Engine

#endif // CORE_TIME_H
