#ifndef RENDERER_RENDERER_H
#define RENDERER_RENDERER_H

#include "Engine/Renderer/VertexArray.h"

#include <glm/glm.hpp>

namespace Engine {

class Renderer {
public:
    static void Init();
    static void SetClearColor(const glm::vec4& color);
    static void Clear();
    static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
};

} // namespace Engine

#endif // RENDERER_RENDERER_H
