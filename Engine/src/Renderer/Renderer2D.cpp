#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/RHI/VertexArray.h"
#include "Engine/Renderer/RHI/Shader.h"
#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/TorchShadowMap.h"
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

struct Renderer2DStorage {
    static constexpr uint32_t MaxQuads = 10000;
    static constexpr uint32_t MaxVertices = MaxQuads * 4;
    static constexpr uint32_t MaxIndices = MaxQuads * 6;
    static constexpr uint32_t RingBufferMultiplier = 3;
    static constexpr uint32_t TotalVertices = MaxVertices * RingBufferMultiplier;
    static constexpr uint32_t MaxTextureSlots = 13; // Slots 0..12 = batched color textures; 13 = ShadowMap; 14 = TorchShadowMap; 15 = DissolveNoise
    static constexpr uint32_t ShadowMapTextureSlot = 13;
    static constexpr uint32_t TorchShadowMapTextureSlot = 14;
    static constexpr uint32_t DissolveNoiseTextureSlot = 15;

    // Line batch constants
    static constexpr uint32_t MaxLines = 10000;
    static constexpr uint32_t MaxLineVertices = MaxLines * 2;

    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<VertexBuffer> QuadVertexBuffer;
    std::shared_ptr<Shader> TextureShader;
    uint32_t WhiteTexture = 0;
    uint32_t DissolveNoiseTexture = 0;

    uint32_t QuadIndexCount = 0;
    std::unique_ptr<QuadVertex[]> QuadVertexBufferBase;
    QuadVertex* QuadVertexBufferPtr = nullptr;
    uint32_t CurrentVertexOffset = 0;

    // Line batch resources
    std::shared_ptr<VertexArray> LineVertexArray;
    std::shared_ptr<VertexBuffer> LineVertexBuffer;
    std::shared_ptr<Shader> LineShader;
    std::unique_ptr<LineVertex[]> LineVertexBufferBase;
    LineVertex* LineVertexBufferPtr = nullptr;
    float LineWidth = 1.5f;

    std::array<uint32_t, MaxTextureSlots> TextureSlots{};
    uint32_t TextureSlotIndex = 1; // Slot 0 = 1x1 WhiteTexture

    glm::vec4 QuadVertexPositions[4]{};
    float DefaultAlphaCutoff = 0.0f;
    uint32_t BoundShadowTextureID = 0;
    uint32_t BoundTorchShadowTextureID = 0;
    std::vector<float> PreassignedTexIndices;

    glm::mat4 CurrentViewProjection{ 1.0f };

    Renderer2D::Statistics Stats;

    ~Renderer2DStorage() {
        if (WhiteTexture != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &WhiteTexture);
            WhiteTexture = 0;
        }
        if (DissolveNoiseTexture != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &DissolveNoiseTexture);
            DissolveNoiseTexture = 0;
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
        { ShaderDataType::Float3, "a_WindParams"   },
        { ShaderDataType::Float4, "a_CutoutFX"     }
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

    // Create 1x1 white texture for solid color rendering in slot 0 & fallback shadow slot 13/14
    if (glad_glGenTextures != nullptr) {
        glGenTextures(1, &s_Data->WhiteTexture);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uint32_t whiteData = 0xffffffff;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteData);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Create 256x256 seamless procedural R8 noise texture for ash dissolve in slot 15
        glGenTextures(1, &s_Data->DissolveNoiseTexture);
        glBindTexture(GL_TEXTURE_2D, s_Data->DissolveNoiseTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        std::vector<uint8_t> noiseData(256 * 256);
        auto hash2D = [](int x, int y) -> float {
            int n = x + y * 57;
            n = (n << 13) ^ n;
            return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f) * 0.5f + 0.5f;
        };
        auto smoothNoise = [&](float x, float y, int period) -> float {
            int x0 = static_cast<int>(std::floor(x));
            int y0 = static_cast<int>(std::floor(y));
            float tx = x - static_cast<float>(x0);
            float ty = y - static_cast<float>(y0);
            tx = tx * tx * (3.0f - 2.0f * tx);
            ty = ty * ty * (3.0f - 2.0f * ty);
            int xi0 = ((x0 % period) + period) % period;
            int yi0 = ((y0 % period) + period) % period;
            int xi1 = (xi0 + 1) % period;
            int yi1 = (yi0 + 1) % period;
            float v00 = hash2D(xi0, yi0);
            float v10 = hash2D(xi1, yi0);
            float v01 = hash2D(xi0, yi1);
            float v11 = hash2D(xi1, yi1);
            float top = v00 * (1.0f - tx) + v10 * tx;
            float bottom = v01 * (1.0f - tx) + v11 * tx;
            return top * (1.0f - ty) + bottom * ty;
        };

        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                float v = 0.0f;
                v += smoothNoise(static_cast<float>(x) * (8.0f / 256.0f),  static_cast<float>(y) * (8.0f / 256.0f),  8)  * 0.50f;
                v += smoothNoise(static_cast<float>(x) * (16.0f / 256.0f), static_cast<float>(y) * (16.0f / 256.0f), 16) * 0.25f;
                v += smoothNoise(static_cast<float>(x) * (32.0f / 256.0f), static_cast<float>(y) * (32.0f / 256.0f), 32) * 0.125f;
                v += smoothNoise(static_cast<float>(x) * (64.0f / 256.0f), static_cast<float>(y) * (64.0f / 256.0f), 64) * 0.125f;
                noiseData[y * 256 + x] = static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE, noiseData.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    int32_t samplers[Renderer2DStorage::MaxTextureSlots];
    for (uint32_t i = 0; i < Renderer2DStorage::MaxTextureSlots; i++) {
        samplers[i] = static_cast<int32_t>(i);
    }

    std::string shaderPath = ResourceManager::ResolvePath("assets/shaders/Batch2D.glsl");
    s_Data->TextureShader = std::make_shared<Shader>(shaderPath);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetIntArray("u_Textures", samplers, Renderer2DStorage::MaxTextureSlots);
    s_Data->TextureShader->SetInt("u_ShadowMap", static_cast<int>(Renderer2DStorage::ShadowMapTextureSlot));
    s_Data->TextureShader->SetInt("u_TorchShadowMap", static_cast<int>(Renderer2DStorage::TorchShadowMapTextureSlot));
    s_Data->TextureShader->SetInt("u_DissolveNoise", static_cast<int>(Renderer2DStorage::DissolveNoiseTextureSlot));
    s_Data->TextureShader->BindUniformBlock("PointLightBlock", SceneLightingData::PointLightUBOBinding);

    // Initialize 3D / 2.5D Line Batching (EDT-02)
    s_Data->LineVertexArray = std::make_shared<VertexArray>();
    s_Data->LineVertexBuffer = std::make_shared<VertexBuffer>(
        Renderer2DStorage::MaxLineVertices * static_cast<uint32_t>(sizeof(LineVertex))
    );
    s_Data->LineVertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_Position" },
        { ShaderDataType::Float4, "a_Color"    }
    });
    s_Data->LineVertexArray->AddVertexBuffer(s_Data->LineVertexBuffer);
    s_Data->LineVertexBufferBase = std::make_unique<LineVertex[]>(Renderer2DStorage::MaxLineVertices);
    s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase.get();

    static const char* kLineVertexShader = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

uniform mat4 u_ViewProjection;

layout(location = 0) out vec4 v_Color;

void main() {
    v_Color = a_Color;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

    static const char* kLineFragmentShader = R"(#version 410 core
layout(location = 0) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

void main() {
    o_Color = v_Color;
}
)";
    s_Data->LineShader = std::make_shared<Shader>("LineShader", kLineVertexShader, kLineFragmentShader);

    s_Data->TextureSlots[0] = s_Data->WhiteTexture;
    s_Data->BoundShadowTextureID = s_Data->WhiteTexture;
    s_Data->BoundTorchShadowTextureID = s_Data->WhiteTexture;

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

bool Renderer2D::IsInitialized() noexcept {
    return s_Data != nullptr;
}

void Renderer2D::ApplyLightingUniforms(const SceneLightingData* lighting) {
    auto& shader = *s_Data->TextureShader;
    shader.Bind();

    if (lighting && lighting->LightingActive) {
        s_Data->BoundShadowTextureID = (lighting->ShadowsEnabled && lighting->ShadowMapTextureID != 0)
            ? lighting->ShadowMapTextureID
            : s_Data->WhiteTexture;
        s_Data->BoundTorchShadowTextureID = (lighting->TorchShadowsEnabled && lighting->TorchShadowMapTextureID != 0)
            ? lighting->TorchShadowMapTextureID
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

        const bool torchActive = (lighting->TorchShadowsEnabled && lighting->TorchShadowMapTextureID != 0);
        shader.SetInt("u_TorchShadowsEnabled", torchActive ? 1 : 0);
        if (torchActive) {
            shader.SetFloat4("u_TorchShadowPosRadius", lighting->TorchShadowPosRadius);
            shader.SetFloat2("u_TorchShadowTexelSize", lighting->TorchShadowTexelSize);
            shader.SetFloat("u_TorchShadowSoftness", lighting->TorchShadowSoftness);
            shader.SetFloat("u_TorchShadowBias", lighting->TorchShadowBias);
            static constexpr const char* kTorchMatNames[4] = {
                "u_TorchLightMatrices[0]", "u_TorchLightMatrices[1]",
                "u_TorchLightMatrices[2]", "u_TorchLightMatrices[3]"
            };
            for (int q = 0; q < 4; ++q) {
                shader.SetMat4(kTorchMatNames[q], lighting->TorchLightMatrices[static_cast<size_t>(q)]);
            }
        }
    } else {
        // Bind WhiteTexture to slots 13 & 14 immediately so ShadowMap / TorchShadowMap are never still bound during FBO writes
        s_Data->BoundShadowTextureID = s_Data->WhiteTexture;
        s_Data->BoundTorchShadowTextureID = s_Data->WhiteTexture;
        glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::ShadowMapTextureSlot);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
        glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::TorchShadowMapTextureSlot);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);

        shader.SetInt("u_LightingEnabled", 0);
        shader.SetInt("u_ShadowsEnabled", 0);
        shader.SetInt("u_TorchShadowsEnabled", 0);
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
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->CurrentViewProjection = camera.GetViewProjectionMatrix();
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);
    StartBatch();
}

void Renderer2D::BeginScene(const PerspectiveCamera& camera,
                            const SceneLightingData& lighting,
                            bool depthTest,
                            float defaultAlphaCutoff) {
    if (!s_Data) return;
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(&lighting);
    ApplyWindUniforms(nullptr);
    s_Data->CurrentViewProjection = camera.GetViewProjectionMatrix();
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);
    StartBatch();
}

void Renderer2D::BeginShadowPass(const glm::mat4& lightSpaceMatrix, float defaultAlphaCutoff) {
    if (!s_Data) return;
    Renderer::SetDepthTest(true);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr); // Unbinds ShadowMap from slot 15 to prevent FBO feedback loop
    ApplyWindUniforms(nullptr);
    s_Data->CurrentViewProjection = lightSpaceMatrix;
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);
    StartBatch();
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    if (!s_Data) return;
    Renderer::SetDepthTest(false);
    s_Data->DefaultAlphaCutoff = 0.0f;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->CurrentViewProjection = camera.GetViewProjectionMatrix();
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);
    StartBatch();
}

void Renderer2D::BeginScene(const glm::mat4& projectionMatrix, bool depthTest, float defaultAlphaCutoff) {
    if (!s_Data) return;
    Renderer::SetDepthTest(depthTest);
    s_Data->DefaultAlphaCutoff = defaultAlphaCutoff;
    ApplyLightingUniforms(nullptr);
    ApplyWindUniforms(nullptr);
    s_Data->CurrentViewProjection = projectionMatrix;
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);
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
    if (s_Data->QuadIndexCount > 0) {
        NextBatch();
    }
    ApplyWindUniforms(&wind);
}

void Renderer2D::EndScene() {
    if (!s_Data) return;
    Flush();
}

void Renderer2D::StartBatch() {
    if (!s_Data) return;
    s_Data->QuadIndexCount = 0;
    s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase.get();
    s_Data->TextureSlotIndex = 1;

    s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase.get();
}

void Renderer2D::NextBatch() {
    if (!s_Data) return;
    Flush();
    StartBatch();
}

void Renderer2D::FlushQuads() {
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
    glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::TorchShadowMapTextureSlot);
    glBindTexture(GL_TEXTURE_2D, s_Data->BoundTorchShadowTextureID);
    glActiveTexture(GL_TEXTURE0 + Renderer2DStorage::DissolveNoiseTextureSlot);
    glBindTexture(GL_TEXTURE_2D, s_Data->DissolveNoiseTexture);

    s_Data->TextureShader->Bind();
    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray, s_Data->QuadIndexCount, s_Data->CurrentVertexOffset);
    s_Data->Stats.DrawCalls++;
    s_Data->Stats.RingBufferVBOOffset = byteOffset + dataSize;

    if (supportsBaseVertex) {
        s_Data->CurrentVertexOffset += vertexCount;
    }

    s_Data->QuadIndexCount = 0;
    s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase.get();
    s_Data->TextureSlotIndex = 1;
}

void Renderer2D::FlushLines() {
    if (!s_Data) return;

    const uint32_t lineVertexCount = static_cast<uint32_t>(
        s_Data->LineVertexBufferPtr - s_Data->LineVertexBufferBase.get()
    );
    if (lineVertexCount == 0) return;

    if (!s_Data->LineShader || !s_Data->LineShader->IsValid()) {
        ENGINE_CORE_WARN("Renderer2D::FlushLines: LineShader is null or invalid; skipping draw");
        s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase.get();
        return;
    }

    const uint32_t dataSize = lineVertexCount * static_cast<uint32_t>(sizeof(LineVertex));
    s_Data->LineVertexBuffer->SetSubData(s_Data->LineVertexBufferBase.get(), dataSize, 0);

    s_Data->LineShader->Bind();
    s_Data->LineShader->SetMat4("u_ViewProjection", s_Data->CurrentViewProjection);

    if (glad_glLineWidth != nullptr) {
        float lineWidth = s_Data->LineWidth;
        if (glad_glGetFloatv != nullptr) {
            GLfloat range[2] = { 1.0f, 1.0f };
            glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
            lineWidth = std::clamp(lineWidth, range[0], range[1]);
        }
        glLineWidth(lineWidth);
    }

    if (s_Data->LineVertexArray) {
        s_Data->LineVertexArray->Bind();
        if (glad_glDrawArrays != nullptr) {
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertexCount));
        }
    }

    s_Data->Stats.DrawCalls++;
    s_Data->Stats.LineCount += (lineVertexCount / 2);

    s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase.get();
}

void Renderer2D::Flush() {
    if (!s_Data) return;
    if (s_Data->QuadIndexCount > 0) {
        FlushQuads();
    }
    const uint32_t lineVertexCount = static_cast<uint32_t>(
        s_Data->LineVertexBufferPtr - s_Data->LineVertexBufferBase.get()
    );
    if (lineVertexCount > 0) {
        FlushLines();
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
        s_Data->QuadVertexBufferPtr->CutoutFX     = glm::vec4(0.0f, 0.0f, 0.0f, 0.08f);
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
    if (!s_Data) return;

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

void Renderer2D::DrawQuad(
    const glm::vec3 positions[4],
    const glm::vec2 texCoords[4],
    const std::shared_ptr<Texture>& texture,
    const glm::vec4& tintColor)
{
    if (!s_Data) return;

    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };
    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitVertices(positions, texID, texCoords, tintColor, -1.0f, 0.0f, 0.02f, kZeroWindParams);
}

void Renderer2D::DrawQuad(
    const glm::vec3 positions[4],
    const glm::vec2 texCoords[4],
    const glm::vec4& tintColor)
{
    DrawQuad(positions, texCoords, std::shared_ptr<Texture>(nullptr), tintColor);
}

void Renderer2D::DrawGroundTile(const glm::vec3& center,
                               const glm::vec2& sizeXZ,
                               const std::shared_ptr<SubTexture2D>& subTexture,
                               const glm::vec4& tintColor,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset,
                               float rotationYRadians) {
    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };

    glm::vec3 positions[4];
    BuildGroundQuadCorners(center, sizeXZ, rotationYRadians, positions);

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
                               float shadowReceiverOffset,
                               float rotationYRadians) {
    static const glm::vec3 kZeroWindParams[4] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };

    glm::vec3 positions[4];
    BuildGroundQuadCorners(center, sizeXZ, rotationYRadians, positions);

    uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0;
    SubmitVertices(positions, texID, kDefaultTexCoords, tintColor, alphaCutoff, emissive, shadowReceiverOffset, kZeroWindParams);
}

void Renderer2D::DrawGroundTile(const glm::vec3& center,
                               const glm::vec2& sizeXZ,
                               const glm::vec4& color,
                               float alphaCutoff,
                               float emissive,
                               float shadowReceiverOffset,
                               float rotationYRadians) {
    DrawGroundTile(center, sizeXZ, std::shared_ptr<Texture>(nullptr), color, alphaCutoff, emissive, shadowReceiverOffset, rotationYRadians);
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

namespace {

void SubmitStandeesInternal(
    Renderer2DStorage& data,
    std::span<const StandeeRenderCommand> commands,
    const glm::vec3& camRight,
    const glm::vec3& camUp,
    const glm::vec3& camRightXZ,
    bool isShadowPass,
    bool isTorchShadowPass,
    bool isSilhouettePass,
    const glm::vec3& torchWorldPos,
    std::span<const glm::vec2> defaultTexCoords)
{
    if (commands.empty()) {
        return;
    }

    size_t cmdIndex = 0;
    const size_t totalCmds = commands.size();

    while (cmdIndex < totalCmds) {
        const size_t batchStart = cmdIndex;
        size_t batchEnd = cmdIndex;

        while (batchEnd < totalCmds) {
            if ((data.QuadIndexCount / 6) >= Renderer2DStorage::MaxQuads) {
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
                for (uint32_t s = 1; s < data.TextureSlotIndex; ++s) {
                    if (data.TextureSlots[s] == texID) {
                        texSlot = static_cast<float>(s);
                        break;
                    }
                }
                if (texSlot == 0.0f) {
                    if (data.TextureSlotIndex >= Renderer2DStorage::MaxTextureSlots) {
                        break;
                    }
                    texSlot = static_cast<float>(data.TextureSlotIndex);
                    data.TextureSlots[data.TextureSlotIndex++] = texID;
                }
            }

            data.PreassignedTexIndices[batchEnd - batchStart] = texSlot;
            ++batchEnd;

            if ((data.QuadIndexCount / 6) + (batchEnd - batchStart) >= Renderer2DStorage::MaxQuads) {
                break;
            }
        }

        const uint32_t batchCount = static_cast<uint32_t>(batchEnd - batchStart);
        if (batchCount == 0) {
            Renderer2D::NextBatch();
            continue;
        }

        QuadVertex* batchBase = data.QuadVertexBufferPtr;
        data.QuadVertexBufferPtr += batchCount * 4;
        data.QuadIndexCount += batchCount * 6;
        data.Stats.QuadCount += batchCount;

        const float defaultAlphaCutoff = data.DefaultAlphaCutoff;
        const glm::vec4* quadLocalPositions = data.QuadVertexPositions;
        const float* texSlots = data.PreassignedTexIndices.data();

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
                if (isTorchShadowPass) {
                    rAxis = TorchShadowMap::ComputeRadialBillboardRightXZ(cmd.worldPosition, torchWorldPos);
                    uAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                } else if (isShadowPass) {
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

            glm::vec2 resolvedUV[4];
            if (sprite && sprite->GetSubTexture()) {
                const auto& subTexture = sprite->GetSubTexture();
                const glm::vec2 min = subTexture->GetMin();
                const glm::vec2 max = subTexture->GetMax();
                resolvedUV[0] = { min.x, min.y };
                resolvedUV[1] = { max.x, min.y };
                resolvedUV[2] = { max.x, max.y };
                resolvedUV[3] = { min.x, max.y };
            } else {
                resolvedUV[0] = defaultTexCoords[0];
                resolvedUV[1] = defaultTexCoords[1];
                resolvedUV[2] = defaultTexCoords[2];
                resolvedUV[3] = defaultTexCoords[3];
            }

            if (sprite && sprite->GetFlipX()) {
                std::swap(resolvedUV[0].x, resolvedUV[1].x);
                std::swap(resolvedUV[3].x, resolvedUV[2].x);
            }
            if (sprite && sprite->GetFlipY()) {
                std::swap(resolvedUV[0].y, resolvedUV[3].y);
                std::swap(resolvedUV[1].y, resolvedUV[2].y);
            }
            const glm::vec2* uv = resolvedUV;

            const float proxBend = (isShadowPass || isTorchShadowPass) ? cmd.shadowProximityBend : cmd.camProximityBend;
            const glm::vec3 wind[4] = {
                { 0.0f,           cmd.spatialPhase, 0.0f     },
                { 0.0f,           cmd.spatialPhase, 0.0f     },
                { cmd.windWeight, cmd.spatialPhase, proxBend },
                { cmd.windWeight, cmd.spatialPhase, proxBend }
            };

            const bool shadowMode = isShadowPass || isTorchShadowPass;
            const float cutoff = (cmd.alphaCutoff < 0.0f) ? defaultAlphaCutoff : cmd.alphaCutoff;
            const float resolvedCutoff = shadowMode ? std::max(0.05f, cutoff) : cutoff;
            const float emissive = isSilhouettePass ? -2.0f : (shadowMode ? -1.0f : cmd.emissive);
            const float shadowOffset = (shadowMode || isSilhouettePass) ? 0.0f : cmd.receiverOffset;
            const glm::vec4 tint = isSilhouettePass ? cmd.silhouetteColor : (shadowMode ? glm::vec4(1.0f) : cmd.color);
            const glm::vec4 cutoutFX = (shadowMode || isSilhouettePass)
                ? glm::vec4(cmd.cutoutFX.x, 0.0f, 0.0f, cmd.cutoutFX.w)
                : cmd.cutoutFX;
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
                v[c].CutoutFX     = cutoutFX;
            }
        });

        cmdIndex = batchEnd;

        if (cmdIndex < totalCmds) {
            Renderer2D::NextBatch();
        }
    }
}

} // namespace

void Renderer2D::DrawStandeesParallel(
    std::span<const StandeeRenderCommand> commands,
    const glm::vec3& camRight,
    const glm::vec3& camUp,
    const glm::vec3& camRightXZ,
    bool isShadowPass,
    bool isTorchShadowPass,
    bool isSilhouettePass)
{
    if (!s_Data) {
        return;
    }
    SubmitStandeesInternal(*s_Data, commands, camRight, camUp, camRightXZ,
                           isShadowPass, isTorchShadowPass, isSilhouettePass, glm::vec3(0.0f), kDefaultTexCoords);
}

void Renderer2D::DrawStandeesTorchShadowParallel(
    std::span<const StandeeRenderCommand> commands,
    const glm::vec3& torchWorldPos)
{
    if (!s_Data) {
        return;
    }
    const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    const glm::vec3 fallbackRight{ 1.0f, 0.0f, 0.0f };
    SubmitStandeesInternal(*s_Data, commands, fallbackRight, worldUp, fallbackRight,
                           /*isShadowPass=*/true, /*isTorchShadowPass=*/true, /*isSilhouettePass=*/false, torchWorldPos, kDefaultTexCoords);
}

void Renderer2D::DrawParticlesParallel(
    const ParticleSystem::Particle* particles,
    uint32_t count,
    const glm::vec3& camRight,
    const glm::vec3& camUp,
    float emissive,
    const Texture* texture,
    ParticleOrientation orientation,
    const FlipbookConfig& flipbook)
{
    if (!particles || count == 0 || !s_Data) {
        return;
    }

    const uint32_t texID = (texture && texture->GetRendererID() != 0) ? texture->GetRendererID() : 0u;
    const float shadowOffset = (orientation == ParticleOrientation::FlatGroundXZ) ? 0.02f : 0.25f;

    size_t offset = 0;
    while (offset < count) {
        const uint32_t currentQuads = s_Data->QuadIndexCount / 6;
        if (currentQuads >= Renderer2DStorage::MaxQuads) {
            NextBatch();
            continue;
        }

        // Resolve shared particle texture slot on the main thread before parallel worker dispatch
        float texSlot = 0.0f;
        if (texID != 0u) {
            for (uint32_t s = 1; s < s_Data->TextureSlotIndex; ++s) {
                if (s_Data->TextureSlots[s] == texID) {
                    texSlot = static_cast<float>(s);
                    break;
                }
            }
            if (texSlot == 0.0f) {
                if (s_Data->TextureSlotIndex >= Renderer2DStorage::MaxTextureSlots) {
                    NextBatch();
                    continue;
                }
                texSlot = static_cast<float>(s_Data->TextureSlotIndex);
                s_Data->TextureSlots[s_Data->TextureSlotIndex++] = texID;
            }
        }

        const uint32_t remainingInBatch = Renderer2DStorage::MaxQuads - currentQuads;
        const uint32_t batchCount = std::min<uint32_t>(static_cast<uint32_t>(count - offset), remainingInBatch);

        QuadVertex* batchBase = s_Data->QuadVertexBufferPtr;
        s_Data->QuadVertexBufferPtr += batchCount * 4;
        s_Data->QuadIndexCount += batchCount * 6;
        s_Data->Stats.QuadCount += batchCount;

        const ParticleSystem::Particle* batchParticles = &particles[offset];
        const FlipbookConfig flipbookCopy = flipbook;

        JobSystem::ParallelFor(static_cast<size_t>(batchCount), [=](size_t i) {
            const auto& p = batchParticles[i];
            QuadVertex* v = &batchBase[i * 4];

            const float lifeRatio = (p.lifeTime > 1e-5f) ? (p.lifeRemaining / p.lifeTime) : 0.0f;
            const float size = glm::mix(p.sizeEnd, p.sizeBegin, lifeRatio);
            const glm::vec4 color = glm::mix(p.colorEnd, p.colorBegin, lifeRatio);

            glm::vec3 corners[4];
            ComputeParticleCorners(p.position, p.velocity, size, p.rotation, orientation, camRight, camUp, corners);

            glm::vec2 uvs[4];
            if (flipbookCopy.Enabled) {
                const uint32_t frameIdx = ParticleSystem::ComputeFlipbookFrameIndex(p, flipbookCopy);
                ParticleSystem::ComputeFlipbookUVs(frameIdx, flipbookCopy, uvs);
            } else {
                uvs[0] = kDefaultTexCoords[0];
                uvs[1] = kDefaultTexCoords[1];
                uvs[2] = kDefaultTexCoords[2];
                uvs[3] = kDefaultTexCoords[3];
            }

            for (int c = 0; c < 4; ++c) {
                v[c].Position     = corners[c];
                v[c].Color        = color;
                v[c].TexCoord     = uvs[c];
                v[c].TexIndex     = texSlot;
                v[c].AlphaCutoff  = 0.0f;
                v[c].Emissive     = emissive;
                v[c].ShadowOffset = shadowOffset;
                v[c].WindParams   = glm::vec3(0.0f);
                v[c].CutoutFX     = glm::vec4(0.0f, 0.0f, 0.0f, 0.08f);
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

void Renderer2D::DrawDebugLine3D(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color) {
    if (!s_Data || !s_Data->LineVertexBufferBase) return;

    const uint32_t currentLineVertices = static_cast<uint32_t>(
        s_Data->LineVertexBufferPtr - s_Data->LineVertexBufferBase.get()
    );
    if (currentLineVertices + 2 > Renderer2DStorage::MaxLineVertices) {
        FlushLines();
    }

    s_Data->LineVertexBufferPtr->Position = p0;
    s_Data->LineVertexBufferPtr->Color = color;
    s_Data->LineVertexBufferPtr++;

    s_Data->LineVertexBufferPtr->Position = p1;
    s_Data->LineVertexBufferPtr->Color = color;
    s_Data->LineVertexBufferPtr++;
}

void Renderer2D::DrawDebugWireCircleXZ(const glm::vec3& center, float radius, const glm::vec4& color, uint32_t segments) {
    if (!s_Data || radius <= 0.0f) return;

    const uint32_t segs = std::clamp(segments, 3u, 128u);
    const float step = (2.0f * 3.14159265358979323846f) / static_cast<float>(segs);

    for (uint32_t i = 0; i < segs; ++i) {
        const float theta0 = static_cast<float>(i) * step;
        const float theta1 = static_cast<float>(i + 1) * step;

        const glm::vec3 p0 = center + glm::vec3(std::cos(theta0) * radius, 0.0f, std::sin(theta0) * radius);
        const glm::vec3 p1 = center + glm::vec3(std::cos(theta1) * radius, 0.0f, std::sin(theta1) * radius);

        DrawDebugLine3D(p0, p1, color);
    }
}

void Renderer2D::DrawDebugWireBoxXZ(const glm::vec3& center, const glm::vec2& sizeXZ, float yawRad, const glm::vec4& color) {
    if (!s_Data || sizeXZ.x <= 0.0f || sizeXZ.y <= 0.0f) return;

    const float hx = 0.5f * sizeXZ.x;
    const float hz = 0.5f * sizeXZ.y;

    glm::vec2 corners[4] = {
        { -hx, -hz },
        {  hx, -hz },
        {  hx,  hz },
        { -hx,  hz }
    };

    if (std::abs(yawRad) > 1e-5f) {
        const float cosY = std::cos(yawRad);
        const float sinY = std::sin(yawRad);
        for (auto& c : corners) {
            const float rx = c.x * cosY + c.y * sinY;
            const float rz = -c.x * sinY + c.y * cosY;
            c.x = rx;
            c.y = rz;
        }
    }

    const glm::vec3 p0 = { center.x + corners[0].x, center.y, center.z + corners[0].y };
    const glm::vec3 p1 = { center.x + corners[1].x, center.y, center.z + corners[1].y };
    const glm::vec3 p2 = { center.x + corners[2].x, center.y, center.z + corners[2].y };
    const glm::vec3 p3 = { center.x + corners[3].x, center.y, center.z + corners[3].y };

    DrawDebugLine3D(p0, p1, color);
    DrawDebugLine3D(p1, p2, color);
    DrawDebugLine3D(p2, p3, color);
    DrawDebugLine3D(p3, p0, color);
}

void Renderer2D::DrawDebugWireArcXZ(const glm::vec3& origin, const glm::vec2& forwardXZ, float radius, float halfAngleDeg, const glm::vec4& color) {
    if (!s_Data || radius <= 0.0f) return;

    const float clampedHalfDeg = std::clamp(halfAngleDeg, 0.1f, 180.0f);
    if (clampedHalfDeg >= 179.9f) {
        DrawDebugWireCircleXZ(origin, radius, color, 32);
        return;
    }

    glm::vec2 fwd = forwardXZ;
    const float lenSq = glm::dot(fwd, fwd);
    if (lenSq < 1e-8f) {
        fwd = { 0.0f, 1.0f };
    } else {
        fwd = glm::normalize(fwd);
    }

    const float baseAngle = std::atan2(fwd.y, fwd.x);
    const float halfRad = glm::radians(clampedHalfDeg);
    const float angleMin = baseAngle - halfRad;
    const float angleMax = baseAngle + halfRad;

    const glm::vec3 pLeft = origin + glm::vec3(std::cos(angleMin) * radius, 0.0f, std::sin(angleMin) * radius);
    const glm::vec3 pRight = origin + glm::vec3(std::cos(angleMax) * radius, 0.0f, std::sin(angleMax) * radius);

    // Left and right boundary spokes
    DrawDebugLine3D(origin, pLeft, color);
    DrawDebugLine3D(origin, pRight, color);

    // Curved perimeter arc
    const uint32_t arcSegments = std::clamp(static_cast<uint32_t>(std::ceil(clampedHalfDeg / 5.0f)), 8u, 48u);
    const float arcStep = (2.0f * halfRad) / static_cast<float>(arcSegments);

    for (uint32_t i = 0; i < arcSegments; ++i) {
        const float a0 = angleMin + static_cast<float>(i) * arcStep;
        const float a1 = angleMin + static_cast<float>(i + 1) * arcStep;

        const glm::vec3 arcP0 = origin + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
        const glm::vec3 arcP1 = origin + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);

        DrawDebugLine3D(arcP0, arcP1, color);
    }
}

void Renderer2D::SetLineWidth(float width) {
    if (s_Data) {
        s_Data->LineWidth = std::clamp(width, 0.5f, 16.0f);
    }
}

float Renderer2D::GetLineWidth() noexcept {
    return s_Data ? s_Data->LineWidth : 1.5f;
}

} // namespace Engine
