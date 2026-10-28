#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <array>
#include <cmath>
#include <cstring>

namespace Engine {

struct QuadVertex {
    glm::vec3 Position;
    glm::vec4 Color;
    glm::vec2 TexCoord;
    float TexIndex;
    float AlphaCutoff;
};

struct Renderer2DStorage {
    static constexpr uint32_t MaxQuads = 10000;
    static constexpr uint32_t MaxVertices = MaxQuads * 4;
    static constexpr uint32_t MaxIndices = MaxQuads * 6;
    static constexpr uint32_t MaxTextureSlots = 16;

    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<VertexBuffer> QuadVertexBuffer;
    std::shared_ptr<Shader> TextureShader;
    uint32_t WhiteTexture = 0;

    uint32_t QuadIndexCount = 0;
    QuadVertex* QuadVertexBufferBase = nullptr;
    QuadVertex* QuadVertexBufferPtr = nullptr;

    std::array<uint32_t, MaxTextureSlots> TextureSlots{};
    uint32_t TextureSlotIndex = 1; // Slot 0 = 1x1 WhiteTexture

    glm::vec4 QuadVertexPositions[4]{};
    float DefaultAlphaCutoff = 0.0f;

    Renderer2D::Statistics Stats;
};

static Renderer2DStorage* s_Data{nullptr};

namespace {
constexpr glm::vec2 kDefaultTexCoords[4] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 0.0f, 1.0f }
};

void ComputeBillboardCorners(const glm::vec3& anchorPosition,
                             const glm::vec2& size,
                             const glm::vec3& rightAxis,
                             const glm::vec3& upAxis,
                             float rotationZDegrees,
                             glm::vec3 outPositions[4]) {
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
} // namespace

void Renderer2D::Init() {
    ENGINE_CORE_INFO("Initializing Renderer2D (MaxQuads: {}, MaxTextureSlots: {})...",
                     Renderer2DStorage::MaxQuads, Renderer2DStorage::MaxTextureSlots);
    s_Data = new Renderer2DStorage();

    s_Data->QuadVertexArray = std::make_shared<VertexArray>();

    s_Data->QuadVertexBuffer = std::make_shared<VertexBuffer>(
        Renderer2DStorage::MaxVertices * static_cast<uint32_t>(sizeof(QuadVertex))
    );
    s_Data->QuadVertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position"    },
        { ShaderDataType::Float4, "a_Color"       },
        { ShaderDataType::Float2, "a_TexCoord"    },
        { ShaderDataType::Float,  "a_TexIndex"    },
        { ShaderDataType::Float,  "a_AlphaCutoff" }
    });
    s_Data->QuadVertexArray->AddVertexBuffer(s_Data->QuadVertexBuffer);

    s_Data->QuadVertexBufferBase = new QuadVertex[Renderer2DStorage::MaxVertices];

    uint32_t* quadIndices = new uint32_t[Renderer2DStorage::MaxIndices];
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

    auto quadIB = std::make_shared<IndexBuffer>(quadIndices, Renderer2DStorage::MaxIndices);
    s_Data->QuadVertexArray->SetIndexBuffer(quadIB);
    delete[] quadIndices;

    // Create 1x1 white texture for solid color rendering in slot 0
    glGenTextures(1, &s_Data->WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uint32_t whiteData = 0xffffffff;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteData);
    glBindTexture(GL_TEXTURE_2D, 0);

    int32_t samplers[Renderer2DStorage::MaxTextureSlots];
    for (uint32_t i = 0; i < Renderer2DStorage::MaxTextureSlots; i++) {
        samplers[i] = static_cast<int32_t>(i);
    }

    std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;
        layout(location = 2) in vec2 a_TexCoord;
        layout(location = 3) in float a_TexIndex;
        layout(location = 4) in float a_AlphaCutoff;

        uniform mat4 u_ViewProjection;

        out vec4 v_Color;
        out vec2 v_TexCoord;
        flat out int v_TexIndex;
        out float v_AlphaCutoff;

        void main() {
            v_Color = a_Color;
            v_TexCoord = a_TexCoord;
            v_TexIndex = int(a_TexIndex + 0.5);
            v_AlphaCutoff = a_AlphaCutoff;
            gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
        }
    )";

    std::string fragmentSrc = R"(
        #version 410 core
        in vec4 v_Color;
        in vec2 v_TexCoord;
        flat in int v_TexIndex;
        in float v_AlphaCutoff;

        out vec4 color;

        uniform sampler2D u_Textures[16];

        void main() {
            vec4 texSample = vec4(1.0);
            switch (v_TexIndex) {
                case  0: texSample = texture(u_Textures[ 0], v_TexCoord); break;
                case  1: texSample = texture(u_Textures[ 1], v_TexCoord); break;
                case  2: texSample = texture(u_Textures[ 2], v_TexCoord); break;
                case  3: texSample = texture(u_Textures[ 3], v_TexCoord); break;
                case  4: texSample = texture(u_Textures[ 4], v_TexCoord); break;
                case  5: texSample = texture(u_Textures[ 5], v_TexCoord); break;
                case  6: texSample = texture(u_Textures[ 6], v_TexCoord); break;
                case  7: texSample = texture(u_Textures[ 7], v_TexCoord); break;
                case  8: texSample = texture(u_Textures[ 8], v_TexCoord); break;
                case  9: texSample = texture(u_Textures[ 9], v_TexCoord); break;
                case 10: texSample = texture(u_Textures[10], v_TexCoord); break;
                case 11: texSample = texture(u_Textures[11], v_TexCoord); break;
                case 12: texSample = texture(u_Textures[12], v_TexCoord); break;
                case 13: texSample = texture(u_Textures[13], v_TexCoord); break;
                case 14: texSample = texture(u_Textures[14], v_TexCoord); break;
                case 15: texSample = texture(u_Textures[15], v_TexCoord); break;
            }
            vec4 texColor = v_Color * texSample;
            if (v_AlphaCutoff > 0.0 && texColor.a < v_AlphaCutoff) {
                discard;
            }
            color = texColor;
        }
    )";

    s_Data->TextureShader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetIntArray("u_Textures", samplers, Renderer2DStorage::MaxTextureSlots);

    s_Data->TextureSlots[0] = s_Data->WhiteTexture;

    s_Data->QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
    s_Data->QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

    ENGINE_CORE_INFO("Renderer2D initialized successfully");
}

void Renderer2D::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Renderer2D...");
    if (s_Data) {
        if (s_Data->WhiteTexture) {
            glDeleteTextures(1, &s_Data->WhiteTexture);
        }
        delete[] s_Data->QuadVertexBufferBase;
        delete s_Data;
        s_Data = nullptr;
    }
    ENGINE_CORE_INFO("Renderer2D shut down");
}

void Renderer2D::BeginScene(const PerspectiveCamera& camera, bool depthTest, float defaultAlphaCutoff) {
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (PerspectiveCamera)");
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    StartBatch();
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (OrthographicCamera)");
    Renderer::SetDepthTest(false);
    s_Data->DefaultAlphaCutoff = 0.0f;
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    StartBatch();
}

void Renderer2D::BeginScene(const glm::mat4& projectionMatrix, bool depthTest, float defaultAlphaCutoff) {
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (Projection Matrix)");
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", projectionMatrix);
    StartBatch();
}

void Renderer2D::SetDefaultAlphaCutoff(float alphaCutoff) {
    if (s_Data) {
        s_Data->DefaultAlphaCutoff = alphaCutoff;
    }
}

void Renderer2D::EndScene() {
    ENGINE_CORE_TRACE("Renderer2D::EndScene");
    Flush();
}

void Renderer2D::StartBatch() {
    s_Data->QuadIndexCount = 0;
    s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase;
    s_Data->TextureSlotIndex = 1;
}

void Renderer2D::NextBatch() {
    Flush();
    StartBatch();
}

void Renderer2D::Flush() {
    if (s_Data->QuadIndexCount == 0) {
        return;
    }

    uint32_t dataSize = static_cast<uint32_t>(
        reinterpret_cast<const uint8_t*>(s_Data->QuadVertexBufferPtr) -
        reinterpret_cast<const uint8_t*>(s_Data->QuadVertexBufferBase)
    );
    s_Data->QuadVertexBuffer->SetData(s_Data->QuadVertexBufferBase, dataSize);

    // Bind all active textures for this batch
    for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, s_Data->TextureSlots[i]);
    }

    s_Data->TextureShader->Bind();
    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray, s_Data->QuadIndexCount);
    s_Data->Stats.DrawCalls++;
}

void Renderer2D::SubmitVertices(
    const glm::vec3 positions[4],
    uint32_t rendererTextureID,
    const glm::vec2 texCoords[4],
    const glm::vec4& tintColor,
    float alphaCutoff)
{
    if (s_Data->QuadIndexCount >= Renderer2DStorage::MaxIndices) {
        NextBatch();
    }

    float textureIndex = 0.0f; // Default to Slot 0 (1x1 WhiteTexture)
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
        s_Data->QuadVertexBufferPtr->Position    = positions[i];
        s_Data->QuadVertexBufferPtr->Color       = tintColor;
        s_Data->QuadVertexBufferPtr->TexCoord    = texCoords[i];
        s_Data->QuadVertexBufferPtr->TexIndex    = textureIndex;
        s_Data->QuadVertexBufferPtr->AlphaCutoff = resolvedCutoff;
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
    float alphaCutoff)
{
    glm::vec3 worldPositions[4];
    for (size_t i = 0; i < 4; ++i) {
        worldPositions[i] = glm::vec3(transform * s_Data->QuadVertexPositions[i]);
    }
    SubmitVertices(worldPositions, rendererTextureID, texCoords, tintColor, alphaCutoff);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, float alphaCutoff) {
    SubmitQuad(transform, 0, kDefaultTexCoords, color, alphaCutoff);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor, float alphaCutoff) {
    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitQuad(transform, texID, kDefaultTexCoords, tintColor, alphaCutoff);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<SubTexture2D>& subTexture, const glm::vec4& tintColor, float alphaCutoff) {
    if (subTexture && subTexture->GetTexture() && subTexture->GetTexture()->GetRendererID() != 0) {
        const glm::vec2 min = subTexture->GetMin();
        const glm::vec2 max = subTexture->GetMax();
        const glm::vec2 subTexCoords[4] = {
            { min.x, min.y },
            { max.x, min.y },
            { max.x, max.y },
            { min.x, max.y }
        };
        SubmitQuad(transform, subTexture->GetTexture()->GetRendererID(), subTexCoords, tintColor, alphaCutoff);
    } else {
        DrawQuad(transform, tintColor, alphaCutoff);
    }
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const glm::vec4& color,
                               float alphaCutoff) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    SubmitVertices(corners, 0, kDefaultTexCoords, color, alphaCutoff);
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const std::shared_ptr<Texture>& texture,
                               const glm::vec4& tintColor,
                               float alphaCutoff) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitVertices(corners, texID, kDefaultTexCoords, tintColor, alphaCutoff);
}

void Renderer2D::DrawBillboard(const glm::vec3& anchorPosition,
                               const glm::vec2& size,
                               const glm::vec3& rightAxis,
                               const glm::vec3& upAxis,
                               float rotationZDegrees,
                               const std::shared_ptr<SubTexture2D>& subTexture,
                               const glm::vec4& tintColor,
                               float alphaCutoff) {
    glm::vec3 corners[4];
    ComputeBillboardCorners(anchorPosition, size, rightAxis, upAxis, rotationZDegrees, corners);
    if (subTexture && subTexture->GetTexture() && subTexture->GetTexture()->GetRendererID() != 0) {
        const glm::vec2 min = subTexture->GetMin();
        const glm::vec2 max = subTexture->GetMax();
        const glm::vec2 subTexCoords[4] = {
            { min.x, min.y },
            { max.x, min.y },
            { max.x, max.y },
            { min.x, max.y }
        };
        SubmitVertices(corners, subTexture->GetTexture()->GetRendererID(), subTexCoords, tintColor, alphaCutoff);
    } else {
        SubmitVertices(corners, 0, kDefaultTexCoords, tintColor, alphaCutoff);
    }
}

void Renderer2D::ResetStats() {
    if (s_Data) {
        std::memset(&s_Data->Stats, 0, sizeof(Statistics));
    }
}

Renderer2D::Statistics Renderer2D::GetStats() {
    return s_Data ? s_Data->Stats : Statistics{};
}

} // namespace Engine
