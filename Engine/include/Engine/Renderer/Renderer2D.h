#ifndef RENDERER_RENDERER2D_H
#define RENDERER_RENDERER2D_H

#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace Engine {

class Renderer2D {
public:
    static void Init();
    static void Shutdown();

    static void BeginScene(const OrthographicCamera& camera);
    static void BeginScene(const glm::mat4& projectionMatrix);
    static void EndScene();
    static void Flush();

    // Draw a quad using a transform matrix
    static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
    static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor = glm::vec4(1.0f));
    static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));

    // Per-frame batch rendering statistics
    struct Statistics {
        uint32_t DrawCalls = 0;
        uint32_t QuadCount = 0;

        uint32_t GetTotalVertexCount() const { return QuadCount * 4; }
        uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
    };

    static void ResetStats();
    static Statistics GetStats();

private:
    static void StartBatch();
    static void NextBatch();
    static void SubmitQuad(const glm::mat4& transform, uint32_t rendererTextureID, const glm::vec2 texCoords[4], const glm::vec4& tintColor);
};

} // namespace Engine

#endif // RENDERER_RENDERER2D_H
