#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/Buffer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <array>
#include <cmath>
#include <memory>

namespace Engine {

namespace {

struct TerrainVertex {
    glm::vec3 LocalPosition;
    glm::vec2 SplatUV;
};

struct TerrainRendererStorage {
    std::shared_ptr<VertexArray> GroundVAO;
    std::shared_ptr<VertexBuffer> GroundVBO;
    std::shared_ptr<Shader> SplatShader;
    std::array<std::shared_ptr<Texture>, TerrainComponent::MaxLayers> FallbackLayerTextures;

    bool BrushPreviewEnabled = false;
    glm::vec2 BrushCenterXZ = { 0.0f, 0.0f };
    float BrushRadiusWorld = 1.25f;
    float BrushFalloff = 0.65f;
};

TerrainRendererStorage* s_TerrainData = nullptr;

} // namespace

void TerrainRenderer::Init() {
    if (s_TerrainData) {
        return;
    }

    ENGINE_CORE_INFO("Initializing TerrainRenderer (4-Layer Height-Modulated Splatmap Shader)...");
    s_TerrainData = new TerrainRendererStorage();

    // Unit [0, 1] x [0, 1] XZ quad with upward +Y normal
    const TerrainVertex vertices[4] = {
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } }
    };
    uint32_t indices[6] = { 0, 3, 2, 2, 1, 0 };

    s_TerrainData->GroundVAO = std::make_shared<VertexArray>();
    s_TerrainData->GroundVBO = std::make_shared<VertexBuffer>(static_cast<uint32_t>(sizeof(vertices)));
    s_TerrainData->GroundVBO->SetData(vertices, static_cast<uint32_t>(sizeof(vertices)));
    s_TerrainData->GroundVBO->SetLayout({
        { ShaderDataType::Float3, "a_LocalPosition" },
        { ShaderDataType::Float2, "a_SplatUV"       }
    });
    s_TerrainData->GroundVAO->AddVertexBuffer(s_TerrainData->GroundVBO);

    auto groundIB = std::make_shared<IndexBuffer>(indices, 6);
    s_TerrainData->GroundVAO->SetIndexBuffer(groundIB);

    // Default 1x1 fallback palette matching Cult of the Lamb ground hues if a layer texture is unassigned:
    // Layer 0: Lush Meadow Grass, Layer 1: Warm Dirt Path, Layer 2: Slate Cobble, Layer 3: Rich Tilled Soil
    const uint8_t fallbackColors[4][4] = {
        {  62,  98,  66, 255 },
        { 126,  92,  68, 255 },
        { 108, 114, 122, 255 },
        {  72,  48,  38, 255 }
    };
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        s_TerrainData->FallbackLayerTextures[i] = std::make_shared<Texture>(
            1u, 1u, fallbackColors[i], /*linearFilter=*/true, /*repeatWrap=*/true
        );
    }

    const std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_LocalPosition;
        layout(location = 1) in vec2 a_SplatUV;

        uniform mat4 u_ViewProjection;
        uniform vec3 u_TerrainOrigin;
        uniform vec2 u_TerrainSizeXZ;

        out vec2 v_SplatUV;
        out vec2 v_WorldXZ;

        void main() {
            vec3 worldPos = u_TerrainOrigin + vec3(
                a_LocalPosition.x * u_TerrainSizeXZ.x,
                0.0,
                a_LocalPosition.z * u_TerrainSizeXZ.y
            );
            v_SplatUV = a_SplatUV;
            v_WorldXZ = worldPos.xz;
            gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
        }
    )";

    const std::string fragmentSrc = R"(
        #version 410 core
        layout(location = 0) out vec4 o_Color;

        in vec2 v_SplatUV;
        in vec2 v_WorldXZ;

        uniform sampler2D u_SplatMap;
        uniform sampler2D u_Layer0;
        uniform sampler2D u_Layer1;
        uniform sampler2D u_Layer2;
        uniform sampler2D u_Layer3;

        uniform vec4 u_LayerTiling;
        uniform float u_BlendSharpness;
        uniform float u_EdgeDarkening;

        uniform int u_BrushEnabled;
        uniform vec2 u_BrushCenterXZ;
        uniform float u_BrushRadius;
        uniform float u_BrushFalloff;

        float hash21(vec2 p) {
            p = fract(p * vec2(127.1, 311.7));
            p += dot(p, p + 45.32);
            return fract(p.x * p.y);
        }

        float valueNoise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(
                mix(hash21(i + vec2(0.0, 0.0)), hash21(i + vec2(1.0, 0.0)), u.x),
                mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x),
                u.y
            );
        }

        float organicBreakup(vec2 xz) {
            return 0.65 * valueNoise(xz * 3.1) + 0.35 * valueNoise(xz * 7.3 + vec2(11.7, 5.3));
        }

        void main() {
            vec4 w = texture(u_SplatMap, v_SplatUV);
            float wSum = max(w.r + w.g + w.b + w.a, 1e-4);
            w /= wSum;

            vec3 c0 = texture(u_Layer0, v_WorldXZ * u_LayerTiling.x).rgb;
            vec3 c1 = texture(u_Layer1, v_WorldXZ * u_LayerTiling.y).rgb;
            vec3 c2 = texture(u_Layer2, v_WorldXZ * u_LayerTiling.z).rgb;
            vec3 c3 = texture(u_Layer3, v_WorldXZ * u_LayerTiling.w).rgb;

            vec4 b = w;
            if (u_BlendSharpness > 0.001) {
                float n = organicBreakup(v_WorldXZ);
                vec3 lumWeights = vec3(0.299, 0.587, 0.114);

                float h0 = mix(dot(c0, lumWeights), n, 0.42);
                float h1 = mix(dot(c1, lumWeights), 1.0 - n, 0.42);
                float h2 = mix(dot(c2, lumWeights), n, 0.28);
                float h3 = mix(dot(c3, lumWeights), 1.0 - n, 0.38);

                // Smoothly ramp height modulation so C1 continuity holds where any weight -> 0
                vec4 modRamp = smoothstep(vec4(0.02), vec4(0.28), w);
                vec4 scores = w + vec4(h0, h1, h2, h3) * (u_BlendSharpness * 0.68) * modRamp;

                float blendDepth = mix(0.90, 0.07, u_BlendSharpness);
                float maxScore = max(max(scores.x, scores.y), max(scores.z, scores.w)) - blendDepth;
                b = max(scores - vec4(maxScore), vec4(0.0));
                b /= max(b.r + b.g + b.b + b.a, 1e-5);
            }

            vec3 finalColor = c0 * b.r + c1 * b.g + c2 * b.b + c3 * b.a;

            // Subtle storybook ink-border darkening where two materials interlock
            float dominant = max(max(b.r, b.g), max(b.b, b.a));
            float transitionMask = smoothstep(0.04, 0.42, 1.0 - dominant);
            finalColor *= (1.0 - u_EdgeDarkening * transitionMask);

            // Live 3D ground brush cursor ring in Edit Mode
            if (u_BrushEnabled == 1) {
                float dist = length(v_WorldXZ - u_BrushCenterXZ);
                float aaWidth = max(fwidth(dist) * 1.6, 0.02);

                float outerRing = 1.0 - smoothstep(0.0, aaWidth, abs(dist - u_BrushRadius));
                float innerRadius = u_BrushRadius * clamp(1.0 - u_BrushFalloff, 0.05, 0.98);
                float innerRing = (1.0 - smoothstep(0.0, aaWidth, abs(dist - innerRadius))) * 0.42;
                float insideTint = (1.0 - smoothstep(u_BrushRadius - aaWidth, u_BrushRadius + aaWidth, dist)) * 0.07;

                vec3 ringColor = vec3(0.98, 0.86, 0.36);
                float overlayAlpha = clamp(outerRing * 0.85 + innerRing + insideTint, 0.0, 0.9);
                finalColor = mix(finalColor, ringColor, overlayAlpha);
            }

            o_Color = vec4(finalColor, 1.0);
        }
    )";

    s_TerrainData->SplatShader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
    s_TerrainData->SplatShader->Bind();
    s_TerrainData->SplatShader->SetInt("u_SplatMap", 0);
    s_TerrainData->SplatShader->SetInt("u_Layer0", 1);
    s_TerrainData->SplatShader->SetInt("u_Layer1", 2);
    s_TerrainData->SplatShader->SetInt("u_Layer2", 3);
    s_TerrainData->SplatShader->SetInt("u_Layer3", 4);
    s_TerrainData->SplatShader->Unbind();

    ENGINE_CORE_INFO("TerrainRenderer initialized successfully");
}

void TerrainRenderer::Shutdown() {
    if (!s_TerrainData) {
        return;
    }
    ENGINE_CORE_INFO("Shutting down TerrainRenderer...");
    delete s_TerrainData;
    s_TerrainData = nullptr;
}

void TerrainRenderer::SetBrushPreview(bool enabled,
                                      const glm::vec2& worldCenterXZ,
                                      float radiusWorld,
                                      float falloff) {
    if (!s_TerrainData) {
        return;
    }
    s_TerrainData->BrushPreviewEnabled = enabled;
    s_TerrainData->BrushCenterXZ = worldCenterXZ;
    s_TerrainData->BrushRadiusWorld = std::max(0.05f, radiusWorld);
    s_TerrainData->BrushFalloff = std::clamp(falloff, 0.0f, 1.0f);
}

void TerrainRenderer::ClearBrushPreview() {
    if (s_TerrainData) {
        s_TerrainData->BrushPreviewEnabled = false;
    }
}

void TerrainRenderer::RenderTerrain(TerrainComponent& terrain,
                                    const glm::vec3& origin,
                                    const glm::vec3& scale,
                                    const PerspectiveCamera& camera) {
    if (!s_TerrainData || !s_TerrainData->SplatShader) {
        return;
    }

    const auto& splatTex = terrain.SyncGpuSplatTexture();
    if (!splatTex || splatTex->GetRendererID() == 0) {
        return;
    }

    const float totalWidth = std::max(0.01f, terrain.GetWidth() * std::abs(scale.x));
    const float totalDepth = std::max(0.01f, terrain.GetDepth() * std::abs(scale.z));

    splatTex->Bind(0);

    glm::vec4 layerTiling(0.45f);
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const auto& layer = terrain.GetLayer(i);
        layerTiling[static_cast<glm::length_t>(i)] = layer.TilingScale;
        if (layer.LayerTexture && layer.LayerTexture->GetRendererID() != 0) {
            if (!layer.LayerTexture->IsWrapRepeat()) {
                layer.LayerTexture->SetWrapRepeat(true);
            }
            layer.LayerTexture->Bind(i + 1u);
        } else {
            s_TerrainData->FallbackLayerTextures[i]->Bind(i + 1u);
        }
    }

    s_TerrainData->SplatShader->Bind();
    s_TerrainData->SplatShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    s_TerrainData->SplatShader->SetFloat3("u_TerrainOrigin", origin);
    s_TerrainData->SplatShader->SetFloat2("u_TerrainSizeXZ", { totalWidth, totalDepth });
    s_TerrainData->SplatShader->SetFloat4("u_LayerTiling", layerTiling);
    s_TerrainData->SplatShader->SetFloat("u_BlendSharpness", terrain.GetBlendSharpness());
    s_TerrainData->SplatShader->SetFloat("u_EdgeDarkening", terrain.GetEdgeDarkening());

    s_TerrainData->SplatShader->SetInt("u_BrushEnabled", s_TerrainData->BrushPreviewEnabled ? 1 : 0);
    if (s_TerrainData->BrushPreviewEnabled) {
        s_TerrainData->SplatShader->SetFloat2("u_BrushCenterXZ", s_TerrainData->BrushCenterXZ);
        s_TerrainData->SplatShader->SetFloat("u_BrushRadius", s_TerrainData->BrushRadiusWorld);
        s_TerrainData->SplatShader->SetFloat("u_BrushFalloff", s_TerrainData->BrushFalloff);
    }

    s_TerrainData->GroundVAO->Bind();
    Renderer::DrawIndexed(s_TerrainData->GroundVAO, 6);
    s_TerrainData->SplatShader->Unbind();
}

} // namespace Engine
