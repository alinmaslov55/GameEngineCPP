#ifndef RENDERER_RENDERER2D_H
#define RENDERER_RENDERER2D_H

#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Renderer/Shader.h"

#include <glm/glm.hpp>
#include <memory>

namespace Engine {

struct Renderer2DStorage {
    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<Shader> TextureShader;
};

class Renderer2D {
public:
    static void Init();
    static void Shutdown();

    static void BeginScene(const OrthographicCamera& camera);
    static void EndScene();

    // Draw a quad using a transform matrix
    static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor = glm::vec4(1.0f));
};

} // namespace Engine

#endif // RENDERER_RENDERER2D_H

