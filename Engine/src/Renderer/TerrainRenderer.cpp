#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/Buffer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {

namespace {

struct TerrainVertex {
    glm::vec3 LocalPosition;
    glm::vec2 SplatUV;
};

struct TerrainRendererStorage {
    std::shared_ptr<VertexArray> GroundVAO;
    std::shared_ptr<VertexBuffer> GroundVBO;
    std::shared_ptr<IndexBuffer> GroundIB;
    std::unordered_map<uint32_t, std::shared_ptr<VertexArray>> StitchedVAOs;
    std::shared_ptr<Shader> SplatShader;
    std::array<std::shared_ptr<Texture>, TerrainComponent::MaxLayers> FallbackLayerTextures;
    std::shared_ptr<Texture> FallbackSplatTex;
    std::shared_ptr<Texture> WhiteFallbackDepthTex;

    bool BrushPreviewEnabled = false;
    glm::vec2 BrushCenterXZ = { 0.0f, 0.0f };
    float BrushRadiusWorld = 1.25f;
    float BrushFalloff = 0.65f;
};

std::unique_ptr<TerrainRendererStorage> s_TerrainData = nullptr;

std::shared_ptr<VertexArray> GetOrCreateStitchedVAO(uint32_t splatResolution) {
    const uint32_t safeRes = std::clamp(splatResolution, 5u, 513u);
    auto it = s_TerrainData->StitchedVAOs.find(safeRes);
    if (it != s_TerrainData->StitchedVAOs.end()) {
        return it->second;
    }

    // Map quad edges [0, 1] directly to texel centers [0.5 / R .. (R - 0.5) / R]
    // so adjacent chunks sharing border texels interpolate with bit-exact C0 continuity under GL_LINEAR.
    const float invRes = 1.0f / static_cast<float>(safeRes);
    const float minUV = 0.5f * invRes;
    const float maxUV = (static_cast<float>(safeRes) - 0.5f) * invRes;
    const TerrainVertex vertices[4] = {
        { { 0.0f, 0.0f, 0.0f }, { minUV, minUV } },
        { { 1.0f, 0.0f, 0.0f }, { maxUV, minUV } },
        { { 1.0f, 0.0f, 1.0f }, { maxUV, maxUV } },
        { { 0.0f, 0.0f, 1.0f }, { minUV, maxUV } }
    };

    auto vao = std::make_shared<VertexArray>();
    auto vbo = std::make_shared<VertexBuffer>(static_cast<uint32_t>(sizeof(vertices)));
    vbo->SetData(vertices, static_cast<uint32_t>(sizeof(vertices)));
    vbo->SetLayout({
        { ShaderDataType::Float3, "a_LocalPosition" },
        { ShaderDataType::Float2, "a_SplatUV"       }
    });
    vao->AddVertexBuffer(vbo);
    vao->SetIndexBuffer(s_TerrainData->GroundIB);
    s_TerrainData->StitchedVAOs.emplace(safeRes, vao);
    return vao;
}

void BindSplatShaderAndState(const std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers,
                             float blendSharpness,
                             float edgeDarkening,
                             const PerspectiveCamera& camera,
                             const SceneLightingData* lighting) {
    glm::vec4 layerTiling(0.45f);
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const auto& layer = layers[i];
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
    shader.SetFloat4("u_LayerTiling", layerTiling);
    shader.SetFloat("u_BlendSharpness", blendSharpness);
    shader.SetFloat("u_EdgeDarkening", edgeDarkening);

    glActiveTexture(GL_TEXTURE5);
    if (lighting && lighting->LightingActive) {
        const uint32_t shadowTex = (lighting->ShadowsEnabled && lighting->ShadowMapTextureID != 0)
            ? lighting->ShadowMapTextureID
            : s_TerrainData->WhiteFallbackDepthTex->GetRendererID();
        glBindTexture(GL_TEXTURE_2D, shadowTex);

        glActiveTexture(GL_TEXTURE6);
        const uint32_t torchShadowTex = (lighting->TorchShadowsEnabled && lighting->TorchShadowMapTextureID != 0)
            ? lighting->TorchShadowMapTextureID
            : s_TerrainData->WhiteFallbackDepthTex->GetRendererID();
        glBindTexture(GL_TEXTURE_2D, torchShadowTex);

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

        Renderer::UploadPointLights(lighting);
    } else {
        glBindTexture(GL_TEXTURE_2D, s_TerrainData->WhiteFallbackDepthTex->GetRendererID());
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, s_TerrainData->WhiteFallbackDepthTex->GetRendererID());
        shader.SetInt("u_LightingEnabled", 0);
        shader.SetInt("u_ShadowsEnabled", 0);
        shader.SetInt("u_TorchShadowsEnabled", 0);
    }

    shader.SetInt("u_BrushEnabled", s_TerrainData->BrushPreviewEnabled ? 1 : 0);
    if (s_TerrainData->BrushPreviewEnabled) {
        shader.SetFloat2("u_BrushCenterXZ", s_TerrainData->BrushCenterXZ);
        shader.SetFloat("u_BrushRadius", s_TerrainData->BrushRadiusWorld);
        shader.SetFloat("u_BrushFalloff", s_TerrainData->BrushFalloff);
    }
}

} // namespace

void TerrainRenderer::Init() {
    if (s_TerrainData) {
        return;
    }

    ENGINE_CORE_INFO("Initializing TerrainRenderer (4-Layer Splatmap + 2.5D Shadow & PointLight Shader)...");
    s_TerrainData = std::make_unique<TerrainRendererStorage>();

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

    s_TerrainData->GroundIB = std::make_shared<IndexBuffer>(indices, 6);
    s_TerrainData->GroundVAO->SetIndexBuffer(s_TerrainData->GroundIB);

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

    const uint8_t defaultSplatPixel[4] = { 255, 0, 0, 0 };
    s_TerrainData->FallbackSplatTex = std::make_shared<Texture>(
        1u, 1u, defaultSplatPixel, /*linearFilter=*/false, /*repeatWrap=*/false
    );

    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    s_TerrainData->WhiteFallbackDepthTex = std::make_shared<Texture>(
        1u, 1u, whitePixel, /*linearFilter=*/false, /*repeatWrap=*/false
    );

    std::string shaderPath = ResourceManager::ResolvePath("assets/shaders/TerrainSplatmap.glsl");
    s_TerrainData->SplatShader = std::make_shared<Shader>(shaderPath);
    s_TerrainData->SplatShader->Bind();
    s_TerrainData->SplatShader->SetInt("u_SplatMap", 0);
    s_TerrainData->SplatShader->SetInt("u_Layer0", 1);
    s_TerrainData->SplatShader->SetInt("u_Layer1", 2);
    s_TerrainData->SplatShader->SetInt("u_Layer2", 3);
    s_TerrainData->SplatShader->SetInt("u_Layer3", 4);
    s_TerrainData->SplatShader->SetInt("u_ShadowMap", 5);
    s_TerrainData->SplatShader->SetInt("u_TorchShadowMap", 6);
    s_TerrainData->SplatShader->BindUniformBlock("PointLightBlock", SceneLightingData::PointLightUBOBinding);
    s_TerrainData->SplatShader->Unbind();

    ENGINE_CORE_INFO("TerrainRenderer initialized successfully");
}

void TerrainRenderer::Shutdown() {
    if (!s_TerrainData) {
        return;
    }
    ENGINE_CORE_INFO("Shutting down TerrainRenderer...");
    s_TerrainData.reset();
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

    BindSplatShaderAndState(terrain.GetLayers(), terrain.GetBlendSharpness(), terrain.GetEdgeDarkening(), camera, lighting);
    splatTex->Bind(0);

    auto& shader = *s_TerrainData->SplatShader;
    shader.SetFloat3("u_TerrainOrigin", origin);
    shader.SetFloat2("u_TerrainSizeXZ", { totalWidth, totalDepth });

    s_TerrainData->GroundVAO->Bind();
    Renderer::DrawIndexed(s_TerrainData->GroundVAO, 6);
    shader.Unbind();
}

void TerrainRenderer::RenderChunkManager(TerrainChunkManager& chunkManager,
                                         float groundY,
                                         const PerspectiveCamera& camera,
                                         const SceneLightingData* lighting) {
    if (!s_TerrainData || !s_TerrainData->SplatShader) {
        return;
    }

    auto& chunks = chunkManager.GetChunks();
    if (chunks.empty()) {
        return;
    }

    BindSplatShaderAndState(chunkManager.GetLayers(),
                            chunkManager.GetBlendSharpness(),
                            chunkManager.GetEdgeDarkening(),
                            camera,
                            lighting);

    auto& shader = *s_TerrainData->SplatShader;
    const Frustum& frustum = camera.GetFrustum();
    uint32_t boundResolution = 0;
    std::shared_ptr<VertexArray> activeVao;

    for (auto& [coord, chunk] : chunks) {
        (void)coord;
        if (!chunk.IsResident()) {
            continue;
        }

        const float chunkSize = std::max(1.0f, chunk.ChunkSizeMeters);
        const glm::vec3 origin = chunk.GetWorldOrigin(groundY);
        const glm::vec3 center = { origin.x + 0.5f * chunkSize, groundY, origin.z + 0.5f * chunkSize };
        const float cullRadius = 0.75f * chunkSize;
        if (!frustum.IntersectsSphere(center, cullRadius)) {
            continue;
        }

        if (chunk.HasGpuTexture() && chunk.GpuDirty) {
            // Sync incremental paint sub-rect immediately for already-resident textures
            chunk.SyncGpuSplatTexture();
        }

        const auto& splatTex = chunk.HasGpuTexture()
            ? chunk.GpuSplatTexture
            : s_TerrainData->FallbackSplatTex;
        if (!splatTex || splatTex->GetRendererID() == 0) {
            continue;
        }

        if (!activeVao || boundResolution != chunk.SplatResolution) {
            boundResolution = chunk.SplatResolution;
            activeVao = GetOrCreateStitchedVAO(boundResolution);
            activeVao->Bind();
        }

        splatTex->Bind(0);
        shader.SetFloat3("u_TerrainOrigin", origin);
        shader.SetFloat2("u_TerrainSizeXZ", { chunkSize, chunkSize });
        Renderer::DrawIndexed(activeVao, 6);
    }

    shader.Unbind();
}

} // namespace Engine
