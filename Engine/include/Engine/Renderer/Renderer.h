#ifndef RENDERER_RENDERER_H
#define RENDERER_RENDERER_H

#include "Engine/Renderer/VertexArray.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

class Renderer {
public:
    static void Init();
    static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    static void SetDepthTest(bool enabled);
    static void SetDepthMask(bool writeEnabled);
    static void SetPolygonOffset(bool enabled, float factor = 1.0f, float units = 1.0f);
    static void SetClearColor(const glm::vec4& color);
    static void Clear();
    static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0);
};

} // namespace Engine

#endif // RENDERER_RENDERER_H
