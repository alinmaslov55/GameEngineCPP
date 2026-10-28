#ifndef RENDERER_POSTPROCESSOR_H
#define RENDERER_POSTPROCESSOR_H

#include "Engine/Scene/PostProcessComponent.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

class PostProcessor {
public:
    static void Init();
    static void Shutdown();

    static bool IsValid();

    // Saves the currently bound LDR target FBO & viewport, resizes the internal GL_RGBA16F
    // HDR framebuffer + 5-mip Bloom pyramid if dimensions changed, and binds + clears the HDR target
    static void BeginHDRScene(uint32_t fallbackWidth,
                              uint32_t fallbackHeight,
                              const glm::vec3& clearColor);

    // Evaluates the 5-mip Karis soft-knee downsample + 9-tap tent upsample Bloom pyramid and
    // composites Exposure, White Balance, Tone Mapping, Split-Toning Color Grading, and Vignette
    // back onto the saved LDR target FBO (Editor Framebuffer or Standalone OS Window FBO 0)
    static void EndHDRAndComposite(const PostProcessComponent& settings);
};

} // namespace Engine

#endif // RENDERER_POSTPROCESSOR_H

