#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/Buffer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>

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
    std::shared_ptr<Texture> WhiteFallbackDepthTex;

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

    ENGINE_CORE_INFO("Initializing TerrainRenderer (4-Layer Splatmap + 2.5D Shadow & PointLight Shader)...");
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

    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    s_TerrainData->WhiteFallbackDepthTex = std::make_shared<Texture>(
        1u, 1u, whitePixel, /*linearFilter=*/false, /*repeatWrap=*/false
    );

    const std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_LocalPosition;
        layout(location = 1) in vec2 a_SplatUV;

        uniform mat4 u_ViewProjection;
        uniform vec3 u_TerrainOrigin;
        uniform vec2 u_TerrainSizeXZ;

        out vec2 v_SplatUV;
        out vec3 v_WorldPos;

        void main() {
            vec3 worldPos = u_TerrainOrigin + vec3(
                a_LocalPosition.x * u_TerrainSizeXZ.x,
                0.0,
                a_LocalPosition.z * u_TerrainSizeXZ.y
            );
            v_SplatUV = a_SplatUV;
            v_WorldPos = worldPos;
            gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
        }
    )";

    const std::string fragmentSrc = R"(
        #version 410 core
        layout(location = 0) out vec4 o_Color;

        in vec2 v_SplatUV;
        in vec3 v_WorldPos;

        uniform sampler2D u_SplatMap;
        uniform sampler2D u_Layer0;
        uniform sampler2D u_Layer1;
        uniform sampler2D u_Layer2;
        uniform sampler2D u_Layer3;
        uniform sampler2D u_ShadowMap;

        uniform vec4 u_LayerTiling;
        uniform float u_BlendSharpness;
        uniform float u_EdgeDarkening;

        // 2.5D Scene Lighting & Cutout Shadow Uniforms
        uniform int u_LightingEnabled;
        uniform vec3 u_SunDirection;
        uniform vec3 u_SunColor;
        uniform float u_SunIntensity;
        uniform vec3 u_AmbientColor;
        uniform float u_AmbientIntensity;

        uniform int u_ShadowsEnabled;
        uniform mat4 u_LightSpaceMatrix;
        uniform float u_ShadowTexelSize;
        uniform float u_ShadowSoftness;
        uniform float u_ShadowBias;

        uniform int u_PointLightCount;
        uniform vec4 u_PointLightPosRadius[8];
        uniform vec4 u_PointLightColorIntensity[8];
        uniform float u_PointLightFalloff[8];

        // Live 3D Editor Brush Ring Uniforms
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

        float computeShadowVisibility(vec3 worldPos) {
            if (u_ShadowsEnabled == 0) {
                return 1.0;
            }
            vec3 samplePos = worldPos - u_SunDirection * 0.015;
            vec4 lightClip = u_LightSpaceMatrix * vec4(samplePos, 1.0);
            vec3 projCoords = (lightClip.xyz / max(lightClip.w, 1e-5)) * 0.5 + 0.5;

            if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
                projCoords.y < 0.0 || projCoords.y > 1.0) {
                return 1.0;
            }

            const vec2 poissonDisk[9] = vec2[9](
                vec2( 0.000,  0.000),
                vec2(-0.707, -0.707),
                vec2( 0.707, -0.707),
                vec2(-0.707,  0.707),
                vec2( 0.707,  0.707),
                vec2(-1.000,  0.000),
                vec2( 1.000,  0.000),
                vec2( 0.000, -1.000),
                vec2( 0.000,  1.000)
            );

            float radiusUV = u_ShadowTexelSize * max(u_ShadowSoftness, 0.25);
            float currentDepth = projCoords.z - u_ShadowBias;
            float litSum = 0.0;
            for (int i = 0; i < 9; ++i) {
                float closestDepth = texture(u_ShadowMap, projCoords.xy + poissonDisk[i] * radiusUV).r;
                litSum += (currentDepth <= closestDepth) ? 1.0 : 0.0;
            }
            return litSum / 9.0;
        }

        vec3 evaluatePointLights(vec3 worldPos) {
            vec3 accum = vec3(0.0);
            for (int i = 0; i < u_PointLightCount; ++i) {
                vec3 lPos = u_PointLightPosRadius[i].xyz;
                float radius = max(u_PointLightPosRadius[i].w, 0.05);
                float dist = length(lPos - worldPos);
                if (dist < radius) {
                    float x = clamp(1.0 - (dist / radius), 0.0, 1.0);
                    float atten = pow(x * x * (3.0 - 2.0 * x), max(u_PointLightFalloff[i], 0.25));
                    accum += u_PointLightColorIntensity[i].rgb * (u_PointLightColorIntensity[i].a * atten);
                }
            }
            return accum;
        }

        void main() {
            vec2 worldXZ = v_WorldPos.xz;
            vec4 w = texture(u_SplatMap, v_SplatUV);
            float wSum = max(w.r + w.g + w.b + w.a, 1e-4);
            w /= wSum;

            vec3 c0 = texture(u_Layer0, worldXZ * u_LayerTiling.x).rgb;
            vec3 c1 = texture(u_Layer1, worldXZ * u_LayerTiling.y).rgb;
            vec3 c2 = texture(u_Layer2, worldXZ * u_LayerTiling.z).rgb;
            vec3 c3 = texture(u_Layer3, worldXZ * u_LayerTiling.w).rgb;

            vec4 b = w;
            if (u_BlendSharpness > 0.001) {
                float n = organicBreakup(worldXZ);
                vec3 lumWeights = vec3(0.299, 0.587, 0.114);

                float h0 = mix(dot(c0, lumWeights), n, 0.42);
                float h1 = mix(dot(c1, lumWeights), 1.0 - n, 0.42);
                float h2 = mix(dot(c2, lumWeights), n, 0.28);
                float h3 = mix(dot(c3, lumWeights), 1.0 - n, 0.38);

                vec4 modRamp = smoothstep(vec4(0.02), vec4(0.28), w);
                vec4 scores = w + vec4(h0, h1, h2, h3) * (u_BlendSharpness * 0.68) * modRamp;

                float blendDepth = mix(0.90, 0.07, u_BlendSharpness);
                float maxScore = max(max(scores.x, scores.y), max(scores.z, scores.w)) - blendDepth;
                b = max(scores - vec4(maxScore), vec4(0.0));
                b /= max(b.r + b.g + b.b + b.a, 1e-5);
            }

            vec3 finalColor = c0 * b.r + c1 * b.g + c2 * b.b + c3 * b.a;

            float dominant = max(max(b.r, b.g), max(b.b, b.a));
            float transitionMask = smoothstep(0.04, 0.42, 1.0 - dominant);
            finalColor *= (1.0 - u_EdgeDarkening * transitionMask);

            // Apply 2.5D Directional Cutout Shadows + Cool Indigo Shade + Flickering PointLights
            if (u_LightingEnabled == 1) {
                float shadowVis = computeShadowVisibility(v_WorldPos);
                vec3 sunAndAmbient = u_AmbientColor * u_AmbientIntensity +
                                     u_SunColor * (u_SunIntensity * shadowVis);
                vec3 pointGlow = evaluatePointLights(v_WorldPos);
                finalColor *= (sunAndAmbient + pointGlow);
            }

            // Live 3D ground brush cursor ring stays unlit on top for clear visibility at night
            if (u_BrushEnabled == 1) {
                float dist = length(worldXZ - u_BrushCenterXZ);
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
    s_TerrainData->SplatShader->SetInt("u_ShadowMap", 5);
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
    ENGINE_CORE_TRACE("TerrainRenderer::SetBrushPreview: enabled={}, center=({:.2f}, {:.2f}), radius={:.2f}",
                      enabled, worldCenterXZ.x, worldCenterXZ.y, radiusWorld);
}

void TerrainRenderer::ClearBrushPreview() {
    if (s_TerrainData) {
        s_TerrainData->BrushPreviewEnabled = false;
    }
}

void TerrainRenderer::RenderTerrain(TerrainComponent& terrain,
                                    const glm::vec3& origin,
                                    const glm::vec3& scale,
                                    const PerspectiveCamera& camera,
                                    const SceneLightingData* lighting) {
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

    auto& shader = *s_TerrainData->SplatShader;
    shader.Bind();
    shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    shader.SetFloat3("u_TerrainOrigin", origin);
    shader.SetFloat2("u_TerrainSizeXZ", { totalWidth, totalDepth });
    shader.SetFloat4("u_LayerTiling", layerTiling);
    shader.SetFloat("u_BlendSharpness", terrain.GetBlendSharpness());
    shader.SetFloat("u_EdgeDarkening", terrain.GetEdgeDarkening());

    // Bind shadow map or safe white fallback to slot 5
    glActiveTexture(GL_TEXTURE5);
    if (lighting && lighting->LightingActive) {
        const uint32_t shadowTex = (lighting->ShadowsEnabled && lighting->ShadowMapTextureID != 0)
            ? lighting->ShadowMapTextureID
            : s_TerrainData->WhiteFallbackDepthTex->GetRendererID();
        glBindTexture(GL_TEXTURE_2D, shadowTex);

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

        const uint32_t count = std::min(lighting->PointLightCount, SceneLightingData::MaxPointLights);
        shader.SetInt("u_PointLightCount", static_cast<int>(count));
        for (uint32_t i = 0; i < count; ++i) {
            const std::string idxStr = std::to_string(i);
            shader.SetFloat4("u_PointLightPosRadius[" + idxStr + "]", lighting->PointLights[i].PositionAndRadius);
            shader.SetFloat4("u_PointLightColorIntensity[" + idxStr + "]", lighting->PointLights[i].ColorAndIntensity);
            shader.SetFloat("u_PointLightFalloff[" + idxStr + "]", lighting->PointLights[i].Falloff);
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, s_TerrainData->WhiteFallbackDepthTex->GetRendererID());
        shader.SetInt("u_LightingEnabled", 0);
        shader.SetInt("u_ShadowsEnabled", 0);
        shader.SetInt("u_PointLightCount", 0);
    }

    shader.SetInt("u_BrushEnabled", s_TerrainData->BrushPreviewEnabled ? 1 : 0);
    if (s_TerrainData->BrushPreviewEnabled) {
        shader.SetFloat2("u_BrushCenterXZ", s_TerrainData->BrushCenterXZ);
        shader.SetFloat("u_BrushRadius", s_TerrainData->BrushRadiusWorld);
        shader.SetFloat("u_BrushFalloff", s_TerrainData->BrushFalloff);
    }

    s_TerrainData->GroundVAO->Bind();
    Renderer::DrawIndexed(s_TerrainData->GroundVAO, 6);
    shader.Unbind();
}

} // namespace Engine
