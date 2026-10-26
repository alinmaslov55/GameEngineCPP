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
    static void SetClearColor(const glm::vec4& color);
    static void Clear();
    static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
};

} // namespace Engine

#endif // RENDERER_RENDERER_H