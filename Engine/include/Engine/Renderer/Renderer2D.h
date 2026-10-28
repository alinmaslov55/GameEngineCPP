#ifndef RENDERER_RENDERER2D_H
#define RENDERER_RENDERER2D_H

#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SceneLighting.h"
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

    static void BeginScene(const PerspectiveCamera& camera, bool depthTest = true, float defaultAlphaCutoff = 0.0f);
    static void BeginScene(const PerspectiveCamera& camera, const SceneLightingData& lighting, bool depthTest = true, float defaultAlphaCutoff = 0.0f);
    static void BeginShadowPass(const glm::mat4& lightSpaceMatrix, float defaultAlphaCutoff = 0.15f);
    static void BeginScene(const OrthographicCamera& camera);
    static void BeginScene(const glm::mat4& projectionMatrix, bool depthTest = false, float defaultAlphaCutoff = 0.0f);
    static void EndScene();
    static void Flush();

    static void SetDefaultAlphaCutoff(float alphaCutoff);

    // Draw a center-pivoted quad using a 4x4 transform matrix (ground tiles, decals, 2D UI)
    // Note: emissive < 0.0f renders the quad unlit; emissive >= 0.0f adds self-glow on top of scene lighting
    static void DrawQuad(const glm::mat4& transform, const glm::vec4& color, float alphaCutoff = -1.0f, float emissive = 0.0f, float shadowReceiverOffset = 0.02f);
    static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor = glm::vec4(1.0f), float alphaCutoff = -1.0f, float emissive = 0.0f, float shadowReceiverOffset = 0.02f);
    static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f), float alphaCutoff = -1.0f, float emissive = 0.0f, float shadowReceiverOffset = 0.02f);

    // Draw a bottom-center anchored 3D upright billboard ("cardboard standee")
    static void DrawBillboard(const glm::vec3& anchorPosition,
                              const glm::vec2& size,
                              const glm::vec3& rightAxis,
                              const glm::vec3& upAxis,
                              float rotationZDegrees,
                              const glm::vec4& color,
                              float alphaCutoff = -1.0f,
                              float emissive = 0.0f,
                              float shadowReceiverOffset = 0.48f);

    static void DrawBillboard(const glm::vec3& anchorPosition,
                              const glm::vec2& size,
                              const glm::vec3& rightAxis,
                              const glm::vec3& upAxis,
                              float rotationZDegrees,
                              const std::shared_ptr<Texture>& texture,
                              const glm::vec4& tintColor = glm::vec4(1.0f),
                              float alphaCutoff = -1.0f,
                              float emissive = 0.0f,
                              float shadowReceiverOffset = 0.48f);

    static void DrawBillboard(const glm::vec3& anchorPosition,
                              const glm::vec2& size,
                              const glm::vec3& rightAxis,
                              const glm::vec3& upAxis,
                              float rotationZDegrees,
                              const std::shared_ptr<SubTexture2D>& subTexture,
                              const glm::vec4& tintColor = glm::vec4(1.0f),
                              float alphaCutoff = -1.0f,
                              float emissive = 0.0f,
                              float shadowReceiverOffset = 0.48f);

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
    static void ApplyLightingUniforms(const SceneLightingData* lighting);
    static void SubmitVertices(const glm::vec3 positions[4],
                               uint32_t rendererTextureID,
                               const glm::vec2 texCoords[4],
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset);
    static void SubmitQuad(const glm::mat4& transform,
                           uint32_t rendererTextureID,
                           const glm::vec2 texCoords[4],
                           const glm::vec4& tintColor,
                           float alphaCutoff,
                           float emissive,
                           float shadowReceiverOffset);
};

} // namespace Engine

#endif // RENDERER_RENDERER2D_H
