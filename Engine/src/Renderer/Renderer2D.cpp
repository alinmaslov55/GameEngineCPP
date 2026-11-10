#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace Engine {
namespace {
static const glm::vec2 kDefaultTexCoords[4] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 0.0f, 1.0f }
};
} // namespace

void ComputeBillboardCorners(const glm::vec3& anchorPosition,
                             const glm::vec2& size,
                             const glm::vec3& rightAxis,
                             const glm::vec3& upAxis,
                             float rotationZDegrees,
                             std::span<glm::vec3, 4> outPositions) {
    glm::vec3 r = rightAxis;
    glm::vec3 u = upAxis;
    if (std::abs(rotationZDegrees) > 1e-4f) {
        const float rad = glm::radians(rotationZDegrees);
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        r = rightAxis * c + upAxis * s;
        u = -rightAxis * s + upAxis * c;
    }

    const glm::vec3 center = anchorPosition + upAxis * (0.5f * size.y);
    const glm::vec3 halfR = r * (0.5f * size.x);
    const glm::vec3 halfU = u * (0.5f * size.y);

    outPositions[0] = center - halfR - halfU;
    outPositions[1] = center + halfR - halfU;
    outPositions[2] = center + halfR + halfU;
    outPositions[3] = center - halfR + halfU;
}

struct Renderer2DStorage {
    static constexpr uint32_t MaxQuads = 10000;
    static constexpr uint32_t MaxVertices = MaxQuads * 4;
    static constexpr uint32_t MaxIndices = MaxQuads * 6;
    static constexpr uint32_t RingBufferMultiplier = 3;
    static constexpr uint32_t TotalVertices = MaxVertices * RingBufferMultiplier;
    static constexpr uint32_t MaxTextureSlots = 15; // Slots 0..14 = batched color textures; Slot 15 = ShadowMap
    static constexpr uint32_t ShadowMapTextureSlot = 15;

    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<VertexBuffer> QuadVertexBuffer;
    std::shared_ptr<Shader> TextureShader;
    uint32_t WhiteTexture = 0;

    uint32_t QuadIndexCount = 0;
    std::unique_ptr<QuadVertex[]> QuadVertexBufferBase;
    QuadVertex* QuadVertexBufferPtr = nullptr;
    uint32_t CurrentVertexOffset = 0;

    std::array<uint32_t, MaxTextureSlots> TextureSlots{};
    uint32_t TextureSlotIndex = 1; // Slot 0 = 1x1 WhiteTexture

    glm::vec4 QuadVertexPositions[4]{};
    float DefaultAlphaCutoff = 0.0f;
    uint32_t BoundShadowTextureID = 0;
    std::vector<float> PreassignedTexIndices;

    Renderer2D::Statistics Stats;

    ~Renderer2DStorage() {
        if (WhiteTexture != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &WhiteTexture);
            WhiteTexture = 0;
        }
    }
};

static std::unique_ptr<Renderer2DStorage> s_Data{nullptr};


void Renderer2D::Init() {
    ENGINE_CORE_INFO("Initializing Renderer2D (MaxQuads: {}, TotalRingVertices: {}, MaxTextureSlots: {} + ShadowSlot {})...",
                     Renderer2DStorage::MaxQuads,
                     Renderer2DStorage::TotalVertices,
                     Renderer2DStorage::MaxTextureSlots,
                     Renderer2DStorage::ShadowMapTextureSlot);
    s_Data = std::make_unique<Renderer2DStorage>();

    s_Data->QuadVertexArray = std::make_shared<VertexArray>();

    s_Data->QuadVertexBuffer = std::make_shared<VertexBuffer>(
        Renderer2DStorage::TotalVertices * static_cast<uint32_t>(sizeof(QuadVertex))
    );
    s_Data->QuadVertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position"     },
        { ShaderDataType::Float4, "a_Color"        },
        { ShaderDataType::Float2, "a_TexCoord"     },
        { ShaderDataType::Float,  "a_TexIndex"     },
        { ShaderDataType::Float,  "a_AlphaCutoff"  },
        { ShaderDataType::Float,  "a_Emissive"     },
        { ShaderDataType::Float,  "a_ShadowOffset" },
        { ShaderDataType::Float3, "a_WindParams"   }
    });
    s_Data->QuadVertexArray->AddVertexBuffer(s_Data->QuadVertexBuffer);

    s_Data->QuadVertexBufferBase = std::make_unique<QuadVertex[]>(Renderer2DStorage::MaxVertices);
    s_Data->PreassignedTexIndices.resize(Renderer2DStorage::MaxQuads);

    std::vector<uint32_t> quadIndices(Renderer2DStorage::MaxIndices);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < Renderer2DStorage::MaxIndices; i += 6) {
        quadIndices[i + 0] = offset + 0;
        quadIndices[i + 1] = offset + 1;
        quadIndices[i + 2] = offset + 2;

        quadIndices[i + 3] = offset + 2;
        quadIndices[i + 4] = offset + 3;
        quadIndices[i + 5] = offset + 0;

        offset += 4;
    }

    auto quadIB = std::make_shared<IndexBuffer>(quadIndices);
    s_Data->QuadVertexArray->SetIndexBuffer(quadIB);

    // Create 1x1 white texture for solid color rendering in slot 0 & fallback shadow slot 15
    glGenTextures(1, &s_Data->WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uint32_t whiteData = 0xffffffff;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteData);
    glBindTexture(GL_TEXTURE_2D, 0);

    int32_t samplers[Renderer2DStorage::MaxTextureSlots];
    for (uint32_t i = 0; i < Renderer2DStorage::MaxTextureSlots; i++) {
        samplers[i] = static_cast<int32_t>(i);
    }

    std::string shaderPath = ResourceManager::ResolvePath("assets/shaders/Batch2D.glsl");
    s_Data->TextureShader = std::make_shared<Shader>(shaderPath);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetIntArray("u_Textures", samplers, Renderer2DStorage::MaxTextureSlots);
    s_Data->TextureShader->SetInt("u_ShadowMap", static_cast<int>(Renderer2DStorage::ShadowMapTextureSlot));
    s_Data->TextureShader->BindUniformBlock("PointLightBlock", SceneLightingData::PointLightUBOBinding);

    s_Data->TextureSlots[0] = s_Data->WhiteTexture;
    s_Data->BoundShadowTextureID = s_Data->WhiteTexture;

    s_Data->QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

    ENGINE_CORE_INFO("Renderer2D initialized successfully");
}

void Renderer2D::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Renderer2D...");
    s_Data.reset();
    ENGINE_CORE_INFO("Renderer2D shut down");
}

void Renderer2D::ApplyLightingUniforms(const SceneLightingData* lighting) {
    auto& shader = *s_Data->TextureShader;
    shader.Bind();

    if (lighting && lighting->LightingActive) {
        s_Data->BoundShadowTextureID = (lighting->ShadowsEnabled && lighting->ShadowMapTextureID != 0)
            ? lighting->ShadowMapTextureID
            : s_Data->WhiteTexture;

        shader.SetInt("u_LightingEnabled", 1);
        shader.SetFloat3("u_SunDirection", lighting->SunDirection);
        shader.SetFloat3("u_SunColor", lighting->SunColor);
        shader.SetFloat("u_SunIntensity", lighting->SunIntensity);
        shader.SetFloat3("u_AmbientColor", lighting->AmbientColor);
        shader.SetFloat("u_AmbientIntensity", lighting->AmbientIntensity);

        shader.SetInt("u_ShadowsEnabled", (lighting->ShadowsEnabled && lighting->ShadowMapTextureID != 0) ? 1 : 0);
        shader.SetMat4("u_LightSpaceMatrix", lighting->LightSpaceMatrix);
        shader.SetFloat("u_ShadowTexelSize", lighting->ShadowMapTexelSize);
        shader.SetFloat("u_ShadowSoftness", lighting->ShadowSoftness);
        shader.SetFloat("u_ShadowBias", lighting->ShadowBias);

        Renderer::UploadPointLights(lighting);
    } else {
        // Bind WhiteTexture to slot 15 immediately so ShadowMap is never still bound during Pass 0 FBO writes
        s_Data->BoundShadowTextureID = s_Data->WhiteTexture;
        glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::ShadowMapTextureSlot);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);

        shader.SetInt("u_LightingEnabled", 0);
        shader.SetInt("u_ShadowsEnabled", 0);
    }
}

void Renderer2D::ApplyWindUniforms(const SceneWindData* wind) {
    auto& shader = *s_Data->TextureShader;
    shader.Bind();

    if (wind) {
        shader.SetInt("u_WindActive", wind->WindActive ? 1 : 0);
        shader.SetFloat("u_WindTime", wind->TimeSeconds);
        shader.SetFloat("u_WindStrength", wind->Strength);
        shader.SetFloat("u_WindSpeed", wind->Speed);
        shader.SetFloat("u_WindGustiness", std::clamp(wind->Gustiness, 0.0f, 1.0f));
        shader.SetFloat("u_WindGustFreq", wind->GustFrequency);
        shader.SetFloat3("u_SwayRightAxis", wind->SwayRightAxis);
    } else {
        shader.SetInt("u_WindActive", 0);
        shader.SetFloat3("u_SwayRightAxis", { 1.0f, 0.0f, 0.0f });
    }
}

void Renderer2D::BeginScene(const PerspectiveCamera& camera, bool depthTest, float defaultAlphaCutoff) {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (PerspectiveCamera, Unlit)");
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    StartBatch();
}

void Renderer2D::BeginScene(const PerspectiveCamera& camera,
                            const SceneLightingData& lighting,
                            bool depthTest,
                            float defaultAlphaCutoff) {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (PerspectiveCamera, 2.5D Lit)");
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(&lighting);
    ApplyWindUniforms(nullptr);
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    StartBatch();
}

void Renderer2D::BeginShadowPass(const glm::mat4& lightSpaceMatrix, float defaultAlphaCutoff) {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::BeginShadowPass");
    Renderer::SetDepthTest(true);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr); // Unbinds ShadowMap from slot 15 to prevent FBO feedback loop
    ApplyWindUniforms(nullptr);
    s_Data->TextureShader->SetMat4("u_ViewProjection", lightSpaceMatrix);
    StartBatch();
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (OrthographicCamera)");
    Renderer::SetDepthTest(false);
    s_Data->DefaultAlphaCutoff = 0.0f;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    StartBatch();
}

void Renderer2D::BeginScene(const glm::mat4& projectionMatrix, bool depthTest, float defaultAlphaCutoff) {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (Projection Matrix)");
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->TextureShader->SetMat4("u_ViewProjection", projectionMatrix);
    StartBatch();
}

void Renderer2D::SetDefaultAlphaCutoff(float alphaCutoff) {
    if (s_Data) {
        s_Data->DefaultAlphaCutoff = alphaCutoff;
    }
}

void Renderer2D::SetWindData(const SceneWindData& wind) {
    if (!s_Data) {
        return;
    }
    ENGINE_CORE_TRACE("Renderer2D::SetWindData: WindActive={}, Strength={:.2f}, Speed={:.2f}",
                      wind.WindActive, wind.Strength, wind.Speed);
    if (s_Data->QuadIndexCount > 0) {
        NextBatch();
    }
    ApplyWindUniforms(&wind);
}

void Renderer2D::EndScene() {
    if (!s_Data) return;
    ENGINE_CORE_TRACE("Renderer2D::EndScene");
    Flush();
}

void Renderer2D::StartBatch() {
    if (!s_Data) return;
    s_Data->QuadIndexCount = 0;
    s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase.get();
    s_Data->TextureSlotIndex = 1;
}

void Renderer2D::NextBatch() {
    if (!s_Data) return;
    Flush();
    StartBatch();
}

void Renderer2D::Flush() {
    if (!s_Data || s_Data->QuadIndexCount == 0) {
        return;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(
        s_Data->QuadVertexBufferPtr - s_Data->QuadVertexBufferBase.get()
    );
    const uint32_t dataSize = vertexCount * static_cast<uint32_t>(sizeof(QuadVertex));

    const bool supportsBaseVertex = (glad_glDrawElementsBaseVertex != nullptr);
    if (!supportsBaseVertex) {
        s_Data->CurrentVertexOffset = 0;
    } else if (s_Data->CurrentVertexOffset + vertexCount > Renderer2DStorage::TotalVertices) {
        // Ring buffer wrap: orphan GPU buffer storage to prevent pipeline sync stalls
        s_Data->QuadVertexBuffer->Orphan();
        s_Data->CurrentVertexOffset = 0;
    }

    const uint32_t byteOffset = s_Data->CurrentVertexOffset * static_cast<uint32_t>(sizeof(QuadVertex));
    s_Data->QuadVertexBuffer->SetSubData(s_Data->QuadVertexBufferBase.get(), dataSize, byteOffset);

    for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, s_Data->TextureSlots[i]);
    }

    glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::ShadowMapTextureSlot);
    glBindTexture(GL_TEXTURE_2D, s_Data->BoundShadowTextureID);

    s_Data->TextureShader->Bind();
    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray, s_Data->QuadIndexCount, s_Data->CurrentVertexOffset);
    s_Data->Stats.DrawCalls++;
    s_Data->Stats.RingBufferVBOOffset = byteOffset + dataSize;

    if (supportsBaseVertex) {
        s_Data->CurrentVertexOffset += vertexCount;
    }
}

void Renderer2D::SubmitVertices(
    const glm::vec3 positions[4],
    uint32_t rendererTextureID,
    const glm::vec2 texCoords[4],
    const glm::vec4& tintColor,
    float alphaCutoff,
    float emissive,
    float shadowReceiverOffset,
    const glm::vec3 windParams[4])
{
    if (!s_Data) return;
    if (s_Data->QuadIndexCount >= Renderer2DStorage::MaxIndices) {
        NextBatch();
    }

    float textureIndex = 0.0f;
    if (rendererTextureID != 0) {
        for (uint32_t i = 1; i < s_Data->TextureSlotIndex; i++) {
            if (s_Data->TextureSlots[i] == rendererTextureID) {
                textureIndex = static_cast<float>(i);
                break;
            }
        }

        if (textureIndex == 0.0f) {
            if (s_Data->TextureSlotIndex >= Renderer2DStorage::MaxTextureSlots) {
                NextBatch();
            }
            textureIndex = static_cast<float>(s_Data->TextureSlotIndex);
            s_Data->TextureSlots[s_Data->TextureSlotIndex] = rendererTextureID;
            s_Data->TextureSlotIndex++;
        }
    }

    const float resolvedCutoff = (alphaCutoff < 0.0f) ? s_Data->DefaultAlphaCutoff : alphaCutoff;

    for (size_t i = 0; i < 4; i++) {
        s_Data->QuadVertexBufferPtr->Position     = positions[i];
        s_Data->QuadVertexBufferPtr->Color        = tintColor;
        s_Data->QuadVertexBufferPtr->TexCoord     = texCoords[i];
        s_Data->QuadVertexBufferPtr->TexIndex     = textureIndex;
        s_Data->QuadVertexBufferPtr->AlphaCutoff  = resolvedCutoff;
        s_Data->QuadVertexBufferPtr->Emissive     = emissive;
        s_Data->QuadVertexBufferPtr->ShadowOffset = shadowReceiverOffset;
        s_Data->QuadVertexBufferPtr->WindParams   = windParams[i];
        s_Data->QuadVertexBufferPtr++;
    }

    s_Data->QuadIndexCount += 6;
    s_Data->Stats.QuadCount++;
}

void Renderer2D::SubmitQuad(
    const glm::mat4& transform,
    uint32_t rendererTextureID,
    const glm::vec2 texCoords[4],
    const glm::vec4& tintColor,
    float alphaCutoff,
    float emissive,
    float shadowReceiverOffset)
{
    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };
    glm::vec3 worldPositions[4];
    for (size_t i = 0; i < 4; ++i) {
        worldPositions[i] = glm::vec3(transform * s_Data->QuadVertexPositions[i]);
    }
    SubmitVertices(worldPositions, rendererTextureID, texCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, kZeroWindParams);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, float alphaCutoff, float emissive, float shadowReceiverOffset) {
    SubmitQuad(transform, 0, kDefaultTexCoords, color, alphaCutoff, emissive, shadowReceiverOffset);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor, float alphaCutoff, float emissive, float shadowReceiverOffset) {
    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitQuad(transform, texID, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<SubTexture2D>& subTexture, const glm::vec4& tintColor, float alphaCutoff, float emissive, float shadowReceiverOffset) {
    if (subTexture && subTexture->GetTexture() && subTexture->GetTexture()->GetRendererID() != 0) {
        const glm::vec2 min = subTexture->GetMin();
        const glm::vec2 max = subTexture->GetMax();
        const glm::vec2 subTexCoords[4] = {
            { min.x, min.y },
            { max.x, min.y },
            { max.x, max.y },
            { min.x, max.y }
        };
        SubmitQuad(transform, subTexture->GetTexture()->GetRendererID(), subTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset);
    } else {
        DrawQuad(transform, tintColor, alphaCutoff, emissive, shadowReceiverOffset);
    }
}

void Renderer2D::DrawGroundTile(const glm::vec3& center,
                               const glm::vec2& sizeXZ,
                               const std::shared_ptr<SubTexture2D>& subTexture,
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset) {
    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };

    const float hx = 0.5f * sizeXZ.x;
    const float hz = 0.5f * sizeXZ.y;
    const float y = center.y;

    const glm::vec3 positions[4] = {
        { center.x - hx, y, center.z + hz },
        { center.x + hx, y, center.z + hz },
        { center.x + hx, y, center.z - hz },
        { center.x - hx, y, center.z - hz }
    };

    if (subTexture && subTexture->GetTexture() && subTexture->GetTexture()->GetRendererID() != 0) {
        const glm::vec2 min = subTexture->GetMin();
        const glm::vec2 max = subTexture->GetMax();
        const glm::vec2 subTexCoords[4] = {
            { min.x, min.y },
            { max.x, min.y },
            { max.x, max.y },
            { min.x, max.y }
        };
        SubmitVertices(positions, subTexture->GetTexture()->GetRendererID(), subTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, kZeroWindParams);
    } else {
        SubmitVertices(positions, 0, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, kZeroWindParams);
    }
}

void Renderer2D::DrawGroundTile(const glm::vec3& center,
                               const glm::vec2& sizeXZ,
                               const std::shared_ptr<Texture>& texture,
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset) {
    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };

    const float hx = 0.5f * sizeXZ.x;
    const float hz = 0.5f * sizeXZ.y;
    const float y = center.y;

    const glm::vec3 positions[4] = {
        { center.x - hx, y, center.z + hz },
        { center.x + hx, y, center.z + hz },
        { center.x + hx, y, center.z - hz },
        { center.x - hx, y, center.z - hz }
    };

    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitVertices(positions, texID, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, kZeroWindParams);
}

void Renderer2D::DrawGroundTile(const glm::vec3& center,
                               const glm::vec2& sizeXZ,
                               const glm::vec4& color,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset) {
    DrawGroundTile(center, sizeXZ, std::shared_ptr<Texture>(nullptr), color, alphaCutoff, emissive, shadowReceiverOffset);
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const glm::vec4& color,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset,
                               float windSwayWeight,
                               float windSpatialPhase,
                               float proximityBend) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    const glm::vec3 windParams[4] = {
        { 0.0f,           windSpatialPhase, 0.0f          },
        { 0.0f,           windSpatialPhase, 0.0f          },
        { windSwayWeight, windSpatialPhase, proximityBend },
        { windSwayWeight, windSpatialPhase, proximityBend }
    };
    SubmitVertices(corners, 0, kDefaultTexCoords, color, alphaCutoff, emissive, shadowReceiverOffset, windParams);
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const std::shared_ptr<Texture>& texture,
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset,
                               float windSwayWeight,
                               float windSpatialPhase,
                               float proximityBend) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    const glm::vec3 windParams[4] = {
        { 0.0f,           windSpatialPhase, 0.0f          },
        { 0.0f,           windSpatialPhase, 0.0f          },
        { windSwayWeight, windSpatialPhase, proximityBend },
        { windSwayWeight, windSpatialPhase, proximityBend }
    };
    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitVertices(corners, texID, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, windParams);
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const std::shared_ptr<SubTexture2D>& subTexture,
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset,
                               float windSwayWeight,
                               float windSpatialPhase,
                               float proximityBend) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    const glm::vec3 windParams[4] = {
        { 0.0f,           windSpatialPhase, 0.0f          },
        { 0.0f,           windSpatialPhase, 0.0f          },
        { windSwayWeight, windSpatialPhase, proximityBend },
        { windSwayWeight, windSpatialPhase, proximityBend }
    };
    if (subTexture && subTexture->GetTexture() && subTexture->GetTexture()->GetRendererID() != 0) {
        const glm::vec2 min = subTexture->GetMin();
        const glm::vec2 max = subTexture->GetMax();
        const glm::vec2 subTexCoords[4] = {
            { min.x, min.y },
            { max.x, min.y },
            { max.x, max.y },
            { min.x, max.y }
        };
        SubmitVertices(corners, subTexture->GetTexture()->GetRendererID(), subTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, windParams);
    } else {
        SubmitVertices(corners, 0, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, windParams);
    }
}

void Renderer2D::DrawStandeesParallel(
    std::span<const StandeeRenderCommand> commands,
    const glm::vec3& camRight,
    const glm::vec3& camUp,
    const glm::vec3& camRightXZ,
    bool isShadowPass)
{
    if (commands.empty() || !s_Data) {
        return;
    }

    size_t cmdIndex = 0;
    const size_t totalCmds = commands.size();

    while (cmdIndex < totalCmds) {
        const size_t batchStart = cmdIndex;
        size_t batchEnd = cmdIndex;

        while (batchEnd < totalCmds) {
            if ((s_Data->QuadIndexCount / 6) >= Renderer2DStorage::MaxQuads) {
                break;
            }

            const auto& cmd = commands[batchEnd];
            const auto* sprite = cmd.sprite;
            uint32_t texID = 0;
            if (sprite) {
                if (const auto& subTexture = sprite->GetSubTexture()) {
                    if (subTexture->GetTexture()) {
                        texID = subTexture->GetTexture()->GetRendererID();
                    }
                } else if (const auto& texture = sprite->GetTexture()) {
                    texID = texture->GetRendererID();
                }
            }

            float texSlot = 0.0f;
            if (texID != 0) {
                for (uint32_t s = 1; s < s_Data->TextureSlotIndex; ++s) {
                    if (s_Data->TextureSlots[s] == texID) {
                        texSlot = static_cast<float>(s);
                        break;
                    }
                }
                if (texSlot == 0.0f) {
                    if (s_Data->TextureSlotIndex >= Renderer2DStorage::MaxTextureSlots) {
                        break;
                    }
                    texSlot = static_cast<float>(s_Data->TextureSlotIndex);
                    s_Data->TextureSlots[s_Data->TextureSlotIndex++] = texID;
                }
            }

            s_Data->PreassignedTexIndices[batchEnd - batchStart] = texSlot;
            ++batchEnd;

            if ((s_Data->QuadIndexCount / 6) + (batchEnd - batchStart) >= Renderer2DStorage::MaxQuads) {
                break;
            }
        }

        const uint32_t batchCount = static_cast<uint32_t>(batchEnd - batchStart);
        if (batchCount == 0) {
            NextBatch();
            continue;
        }

        QuadVertex* batchBase = s_Data->QuadVertexBufferPtr;
        s_Data->QuadVertexBufferPtr += batchCount * 4;
        s_Data->QuadIndexCount += batchCount * 6;
        s_Data->Stats.QuadCount += batchCount;

        const float defaultAlphaCutoff = s_Data->DefaultAlphaCutoff;
        const glm::vec4* quadLocalPositions = s_Data->QuadVertexPositions;
        const float* texSlots = s_Data->PreassignedTexIndices.data();

        JobSystem::ParallelFor(static_cast<size_t>(batchCount), [=](size_t i) {
            const auto& cmd = commands[batchStart + i];
            QuadVertex* v = &batchBase[i * 4];
            const auto* sprite = cmd.sprite;
            const auto billboardMode = sprite ? sprite->GetBillboardMode() : SpriteRenderer::BillboardMode::ViewPlane;

            glm::vec3 corners[4];
            if (billboardMode == SpriteRenderer::BillboardMode::ViewPlane ||
                billboardMode == SpriteRenderer::BillboardMode::CylindricalY) {
                glm::vec3 rAxis = camRight;
                glm::vec3 uAxis = camUp;
                if (isShadowPass) {
                    rAxis = camRight;
                    uAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                } else if (billboardMode == SpriteRenderer::BillboardMode::CylindricalY) {
                    rAxis = camRightXZ;
                    uAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                ComputeBillboardCorners(cmd.worldPosition, cmd.size, rAxis, uAxis, cmd.rotationZ, corners);
            } else {
                const glm::vec3 scaled3D = { cmd.size.x, cmd.size.y, cmd.scaleZ };
                const glm::vec3 centerPos = cmd.worldPosition + glm::vec3(0.0f, 0.5f * cmd.size.y, 0.0f);
                const glm::vec3 worldRot = cmd.worldRotation;
                const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), centerPos) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(cmd.rotationZ), glm::vec3(0.0f, 0.0f, 1.0f)) *
                                              glm::scale(glm::mat4(1.0f), scaled3D);
                for (int c = 0; c < 4; ++c) {
                    corners[c] = glm::vec3(modelMatrix * quadLocalPositions[c]);
                }
            }

            const glm::vec2* uv = kDefaultTexCoords;
            glm::vec2 subUV[4];
            if (sprite) {
                if (const auto& subTexture = sprite->GetSubTexture()) {
                    const glm::vec2 min = subTexture->GetMin();
                    const glm::vec2 max = subTexture->GetMax();
                    subUV[0] = { min.x, min.y };
                    subUV[1] = { max.x, min.y };
                    subUV[2] = { max.x, max.y };
                    subUV[3] = { min.x, max.y };
                    uv = subUV;
                }
            }

            const float proxBend = isShadowPass ? cmd.shadowProximityBend : cmd.camProximityBend;
            const glm::vec3 wind[4] = {
                { 0.0f,           cmd.spatialPhase, 0.0f     },
                { 0.0f,           cmd.spatialPhase, 0.0f     },
                { cmd.windWeight, cmd.spatialPhase, proxBend },
                { cmd.windWeight, cmd.spatialPhase, proxBend }
            };

            const float cutoff = (cmd.alphaCutoff < 0.0f) ? defaultAlphaCutoff : cmd.alphaCutoff;
            const float resolvedCutoff = isShadowPass ? std::max(0.05f, cutoff) : cutoff;
            const float emissive = isShadowPass ? -1.0f : cmd.emissive;
            const float shadowOffset = isShadowPass ? 0.0f : cmd.receiverOffset;
            const glm::vec4 tint = isShadowPass ? glm::vec4(1.0f) : cmd.color;
            const float texIdx = texSlots[i];
            const bool isBillboard = (billboardMode == SpriteRenderer::BillboardMode::ViewPlane ||
                                      billboardMode == SpriteRenderer::BillboardMode::CylindricalY);

            for (int c = 0; c < 4; ++c) {
                v[c].Position     = corners[c];
                v[c].Color        = tint;
                v[c].TexCoord     = uv[c];
                v[c].TexIndex     = texIdx;
                v[c].AlphaCutoff  = resolvedCutoff;
                v[c].Emissive     = emissive;
                v[c].ShadowOffset = shadowOffset;
                v[c].WindParams   = isBillboard ? wind[c] : glm::vec3(0.0f);
            }
        });

        cmdIndex = batchEnd;

        if (cmdIndex < totalCmds) {
            NextBatch();
        }
    }
}

void Renderer2D::DrawParticlesParallel(
    const ParticleSystem::Particle* particles,
    uint32_t count,
    const glm::vec3& camRight,
    const glm::vec3& camUp,
    float emissive)
{
    if (!particles || count == 0 || !s_Data) {
        return;
    }

    size_t offset = 0;
    while (offset < count) {
        const uint32_t currentQuads = s_Data->QuadIndexCount / 6;
        if (currentQuads >= Renderer2DStorage::MaxQuads) {
            NextBatch();
            continue;
        }

        const uint32_t remainingInBatch = Renderer2DStorage::MaxQuads - currentQuads;
        const uint32_t batchCount = std::min<uint32_t>(static_cast<uint32_t>(count - offset), remainingInBatch);

        QuadVertex* batchBase = s_Data->QuadVertexBufferPtr;
        s_Data->QuadVertexBufferPtr += batchCount * 4;
        s_Data->QuadIndexCount += batchCount * 6;
        s_Data->Stats.QuadCount += batchCount;

        const ParticleSystem::Particle* batchParticles = &particles[offset];

        JobSystem::ParallelFor(static_cast<size_t>(batchCount), [=](size_t i) {
            const auto& p = batchParticles[i];
            QuadVertex* v = &batchBase[i * 4];

            const float lifeRatio = (p.lifeTime > 1e-5f) ? (p.lifeRemaining / p.lifeTime) : 0.0f;
            const float size = glm::mix(p.sizeEnd, p.sizeBegin, lifeRatio);
            const glm::vec4 color = glm::mix(p.colorEnd, p.colorBegin, lifeRatio);

            const glm::vec3 anchor = p.position - camUp * (0.5f * size);
            glm::vec3 corners[4];
            ComputeBillboardCorners(anchor, { size, size }, camRight, camUp, glm::degrees(p.rotation), corners);

            for (int c = 0; c < 4; ++c) {
                v[c].Position     = corners[c];
                v[c].Color        = color;
                v[c].TexCoord     = kDefaultTexCoords[c];
                v[c].TexIndex     = 0.0f;
                v[c].AlphaCutoff  = 0.0f;
                v[c].Emissive     = emissive;
                v[c].ShadowOffset = 0.25f;
                v[c].WindParams   = glm::vec3(0.0f);
            }
        });

        offset += batchCount;

        if (offset < count) {
            NextBatch();
        }
    }
}

void Renderer2D::ResetStats() {
    if (s_Data) {
        std::memset(&s_Data->Stats, 0, sizeof(Statistics));
        // Wrap ring buffer at frame boundary if next batch cannot fit entirely
        if (s_Data->CurrentVertexOffset + Renderer2DStorage::MaxVertices > Renderer2DStorage::TotalVertices) {
            s_Data->QuadVertexBuffer->Orphan();
            s_Data->CurrentVertexOffset = 0;
        }
        s_Data->Stats.RingBufferVBOOffset = s_Data->CurrentVertexOffset * static_cast<uint32_t>(sizeof(QuadVertex));
    }
}

Renderer2D::Statistics Renderer2D::GetStats() {
    return s_Data ? s_Data->Stats : Statistics{};
}

} // namespace Engine
