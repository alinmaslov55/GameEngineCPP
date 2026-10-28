#ifndef RENDERER_SHADOWMAP_H
#define RENDERER_SHADOWMAP_H

#include <cstdint>

namespace Engine {

class ShadowMap {
public:
    static void Init(uint32_t resolution = 2048);
    static void Shutdown();

    static bool IsValid();
    static void BeginCapture();
    static void EndCapture();

    static uint32_t GetDepthTextureID();
    static uint32_t GetResolution();
};

} // namespace Engine

#endif // RENDERER_SHADOWMAP_H

