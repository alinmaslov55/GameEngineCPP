#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/RHI/Buffer.h"
#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Renderer/RHI/Shader.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Renderer/RHI/VertexArray.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

namespace {

struct TerrainVertex {
    glm::vec3 LocalPosition;
    glm::vec2 SplatUV;
};

static const std::array<glm::u8vec4, TerrainComponent::MaxLayers> kFallbackLayerColors = {{
    glm::u8vec4( 62,  98,  66, 255), // 0: Grass
    glm::u8vec4(112, 104,  98, 255), // 1: Ash
    glm::u8vec4( 34,  28,  42, 255), // 2: Tar
    glm::u8vec4(186, 184, 174, 255), // 3: Chalk
    glm::u8vec4(108, 114, 122, 255), // 4: Cobblestone
    glm::u8vec4( 46,  42,  40, 255), // 5: Scorched Ash
    glm::u8vec4( 72,  48,  38, 255), // 6: Spare 1
    glm::u8vec4( 90,  86,  78, 255)  // 7: Spare 2
}};

struct TerrainLayerSetEntry {
    uint64_t SetHash = 0;
    std::shared_ptr<Texture2DArray> AlbedoHeightArray;
    std::shared_ptr<Texture2DArray> NormalRoughnessArray;
    std::array<uint64_t, TerrainComponent::MaxLayers> LayerHashes{};
    uint64_t LastUsedFrame = 0;
};

struct TerrainRendererStorage {
    std::shared_ptr<VertexArray> GroundVAO;
    std::shared_ptr<VertexBuffer> GroundVBO;
    std::shared_ptr<IndexBuffer> GroundIB;
    std::unordered_map<uint32_t, std::shared_ptr<VertexArray>> StitchedVAOs;
    std::shared_ptr<Shader> SplatShader;

    // Cache of up to 4 distinct layer sets to prevent multi-terrain cache thrashing
    std::unordered_map<uint64_t, std::unique_ptr<TerrainLayerSetEntry>> LayerSetCache;
    TerrainLayerSetEntry* ActiveLayerSet = nullptr;
    uint64_t FrameCounter = 0;

    std::shared_ptr<Texture> FallbackSplatTex0;
    std::shared_ptr<Texture> FallbackSplatTex1;
    std::shared_ptr<Texture> WhiteFallbackDepthTex;

    bool BrushPreviewEnabled = false;
    glm::vec2 BrushCenterXZ = { 0.0f, 0.0f };
    float BrushRadiusWorld = 1.25f;
    float BrushFalloff = 0.65f;
};

std::unique_ptr<TerrainRendererStorage> s_TerrainData = nullptr;

inline uint64_t Fnv1a64(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) noexcept {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(ptr[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline uint64_t Fnv1a64Str(const std::string& str, uint64_t hash = 14695981039346656037ull) noexcept {
    return Fnv1a64(str.data(), str.size(), hash);
}

uint64_t ComputeLayerSignatureHash(const TerrainLayer& layer, uint32_t layerIndex) noexcept {
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1a64(&layerIndex, sizeof(layerIndex), hash);

    const std::string& diffusePath = layer.LayerTexture ? layer.LayerTexture->GetFilepath() : "";
    const uint32_t texId = layer.LayerTexture ? layer.LayerTexture->GetRendererID() : 0u;
    const uint8_t loaded = (layer.LayerTexture && layer.LayerTexture->IsLoaded()) ? 1u : 0u;
    const int32_t glossQ = static_cast<int32_t>(std::lround(std::clamp(layer.Glossiness, 0.0f, 1.0f) * 1000.0f));

    hash = Fnv1a64(&texId, sizeof(texId), hash);
    hash = Fnv1a64(&loaded, sizeof(loaded), hash);
    hash = Fnv1a64Str(diffusePath, hash);
    hash = Fnv1a64Str(layer.HeightMapPath, hash);
    hash = Fnv1a64Str(layer.NormalMapPath, hash);
    hash = Fnv1a64(&glossQ, sizeof(glossQ), hash);

    return hash;
}

uint64_t ComputeLayerSetHash(const std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers) noexcept {
    uint64_t setHash = 14695981039346656037ull;
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        uint64_t layerHash = ComputeLayerSignatureHash(layers[i], i);
        setHash = Fnv1a64(&layerHash, sizeof(layerHash), setHash);
    }
    return setHash;
}

void SyncLayerTextureArrays(const std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers) {
    if (!s_TerrainData) {
        return;
    }

    const uint64_t setHash = ComputeLayerSetHash(layers);
    if (s_TerrainData->ActiveLayerSet && s_TerrainData->ActiveLayerSet->SetHash == setHash) {
        return;
    }

    auto it = s_TerrainData->LayerSetCache.find(setHash);
    if (it != s_TerrainData->LayerSetCache.end()) {
        s_TerrainData->ActiveLayerSet = it->second.get();
        s_TerrainData->ActiveLayerSet->LastUsedFrame = ++s_TerrainData->FrameCounter;
        return;
    }

    // Evict oldest entry if cache is at capacity (bounded at 4 sets)
    static constexpr size_t kMaxCachedLayerSets = 4;
    if (s_TerrainData->LayerSetCache.size() >= kMaxCachedLayerSets) {
        uint64_t oldestFrame = std::numeric_limits<uint64_t>::max();
        uint64_t oldestKey = 0;
        bool foundOldest = false;
        for (const auto& [key, entryPtr] : s_TerrainData->LayerSetCache) {
            if (entryPtr.get() == s_TerrainData->ActiveLayerSet) {
                continue;
            }
            if (entryPtr->LastUsedFrame < oldestFrame) {
                oldestFrame = entryPtr->LastUsedFrame;
                oldestKey = key;
                foundOldest = true;
            }
        }
        if (foundOldest) {
            s_TerrainData->LayerSetCache.erase(oldestKey);
        } else {
            s_TerrainData->LayerSetCache.erase(s_TerrainData->LayerSetCache.begin());
        }
    }

    auto newEntry = std::make_unique<TerrainLayerSetEntry>();
    newEntry->SetHash = setHash;
    newEntry->LastUsedFrame = ++s_TerrainData->FrameCounter;
    newEntry->AlbedoHeightArray = std::make_shared<Texture2DArray>(
        Texture2DArray::DefaultWidth,
        Texture2DArray::DefaultHeight,
        TerrainComponent::MaxLayers,
        /*linearFilter=*/true,
        /*repeatWrap=*/true
    );
    newEntry->NormalRoughnessArray = std::make_shared<Texture2DArray>(
        Texture2DArray::DefaultWidth,
        Texture2DArray::DefaultHeight,
        TerrainComponent::MaxLayers,
        /*linearFilter=*/true,
        /*repeatWrap=*/true
    );

    const uint32_t arrayW = newEntry->AlbedoHeightArray->GetWidth();
    const uint32_t arrayH = newEntry->AlbedoHeightArray->GetHeight();
    std::vector<uint8_t> albedoHeightScratch;
    std::vector<uint8_t> normalGlossScratch;

    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const auto& layer = layers[i];
        newEntry->LayerHashes[i] = ComputeLayerSignatureHash(layer, i);

        const std::string diffusePath = layer.LayerTexture ? layer.LayerTexture->GetFilepath() : "";
        const ResolvedTerrainMaps resolved = Texture2DArray::ResolveTerrainMapCompanions(
            diffusePath,
            layer.HeightMapPath,
            layer.NormalMapPath
        );

        const std::string& effectiveDiffuse = !resolved.DiffusePath.empty() ? resolved.DiffusePath : diffusePath;
        if (Texture2DArray::BuildAlbedoHeightSliceFromFiles(
                effectiveDiffuse,
                resolved.HeightPath,
                arrayW,
                arrayH,
                kFallbackLayerColors[i],
                albedoHeightScratch)) {
            newEntry->AlbedoHeightArray->SetLayerData(i, albedoHeightScratch.data(), /*generateMips=*/false);
        }

        if (Texture2DArray::BuildNormalGlossSliceFromFiles(
                resolved.NormalPath,
                layer.Glossiness,
                arrayW,
                arrayH,
                normalGlossScratch)) {
            newEntry->NormalRoughnessArray->SetLayerData(i, normalGlossScratch.data(), /*generateMips=*/false);
        }
    }

    newEntry->AlbedoHeightArray->GenerateMipmaps();
    newEntry->NormalRoughnessArray->GenerateMipmaps();
    ENGINE_CORE_DEBUG("TerrainRenderer: Cached new terrain layer set (hash {:016x}, {}x{}) and generated mipmaps",
                      setHash, arrayW, arrayH);

    s_TerrainData->ActiveLayerSet = newEntry.get();
    s_TerrainData->LayerSetCache.emplace(setHash, std::move(newEntry));
}

std::shared_ptr<VertexArray> GetOrCreateStitchedVAO(uint32_t splatResolution) {
    const uint32_t safeRes = std::clamp(splatResolution, 5u, 513u);
    auto it = s_TerrainData->StitchedVAOs.find(safeRes);
    if (it != s_TerrainData->StitchedVAOs.end()) {
        return it->second;
    }

    ENGINE_CORE_DEBUG("TerrainRenderer: Creating stitched terrain quad VAO for splat resolution {}", safeRes);

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
    SyncLayerTextureArrays(layers);
    if (s_TerrainData->ActiveLayerSet &&
        s_TerrainData->ActiveLayerSet->AlbedoHeightArray &&
        s_TerrainData->ActiveLayerSet->NormalRoughnessArray) {
        s_TerrainData->ActiveLayerSet->AlbedoHeightArray->Bind(2);
        s_TerrainData->ActiveLayerSet->NormalRoughnessArray->Bind(3);
    }

    std::array<float, TerrainComponent::MaxLayers> layerTiling{};
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        layerTiling[i] = layers[i].TilingScale;
    }

    auto& shader = *s_TerrainData->SplatShader;
    shader.Bind();
    shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    shader.SetFloatArray("u_LayerTilingScales", layerTiling.data(), TerrainComponent::MaxLayers);
    shader.SetFloat("u_BlendSharpness", blendSharpness);
    shader.SetFloat("u_EdgeDarkening", edgeDarkening);
    shader.SetFloat3("u_CameraPos", camera.GetPosition());

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

    // C-17: Restore active texture to slot 0 so subsequent texture uploads / bindings
    // don't inadvertently unbind or corrupt u_ShadowMap (slot 5) or u_TorchShadowMap (slot 6).
    glActiveTexture(GL_TEXTURE0);
}

} // namespace

void TerrainRenderer::Init() {
    if (s_TerrainData) {
        return;
    }

    ENGINE_CORE_INFO("Initializing TerrainRenderer (8-Layer Texture2DArray + Height/Normal/Specular Shader)...");
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

    const uint8_t defaultSplatPixel0[4] = { 255, 0, 0, 0 };
    s_TerrainData->FallbackSplatTex0 = std::make_shared<Texture>(
        1u, 1u, defaultSplatPixel0, /*linearFilter=*/false, /*repeatWrap=*/false
    );
    const uint8_t defaultSplatPixel1[4] = { 0, 0, 0, 0 };
    s_TerrainData->FallbackSplatTex1 = std::make_shared<Texture>(
        1u, 1u, defaultSplatPixel1, /*linearFilter=*/false, /*repeatWrap=*/false
    );

    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    s_TerrainData->WhiteFallbackDepthTex = std::make_shared<Texture>(
        1u, 1u, whitePixel, /*linearFilter=*/false, /*repeatWrap=*/false
    );

    std::string shaderPath = ResourceManager::ResolvePath("assets/shaders/TerrainSplatmap.glsl");
    s_TerrainData->SplatShader = std::make_shared<Shader>(shaderPath);
    if (!s_TerrainData->SplatShader->IsValid()) {
        ENGINE_CORE_ERROR("TerrainRenderer: Failed to compile TerrainSplatmap shader from '{}'", shaderPath);
    } else {
        s_TerrainData->SplatShader->Bind();
        s_TerrainData->SplatShader->SetInt("u_SplatWeights0", 0);
        s_TerrainData->SplatShader->SetInt("u_SplatWeights1", 1);
        s_TerrainData->SplatShader->SetInt("u_AlbedoHeightArray", 2);
        s_TerrainData->SplatShader->SetInt("u_NormalRoughnessArray", 3);
        s_TerrainData->SplatShader->SetInt("u_ShadowMap", 5);
        s_TerrainData->SplatShader->SetInt("u_TorchShadowMap", 6);
        s_TerrainData->SplatShader->BindUniformBlock("PointLightBlock", SceneLightingData::PointLightUBOBinding);
        s_TerrainData->SplatShader->Unbind();
    }

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
    if (!s_TerrainData || !s_TerrainData->SplatShader || !s_TerrainData->SplatShader->IsValid()) {
        return;
    }

    auto [splatTex0, splatTex1] = terrain.SyncGpuSplatTextures();
    if (!splatTex0 || splatTex0->GetRendererID() == 0) {
        return;
    }

    const float totalWidth = std::max(0.01f, terrain.GetWidth() * std::abs(scale.x));
    const float totalDepth = std::max(0.01f, terrain.GetDepth() * std::abs(scale.z));

    BindSplatShaderAndState(terrain.GetLayers(), terrain.GetBlendSharpness(), terrain.GetEdgeDarkening(), camera, lighting);
    splatTex0->Bind(0);
    if (splatTex1 && splatTex1->GetRendererID() != 0) {
        splatTex1->Bind(1);
    } else {
        s_TerrainData->FallbackSplatTex1->Bind(1);
    }

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
    if (!s_TerrainData || !s_TerrainData->SplatShader || !s_TerrainData->SplatShader->IsValid()) {
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
            chunk.SyncGpuSplatTextures();
        }

        const auto& splatTex0 = chunk.HasGpuTexture()
            ? chunk.GpuSplatTexture
            : s_TerrainData->FallbackSplatTex0;
        if (!splatTex0 || splatTex0->GetRendererID() == 0) {
            continue;
        }
        const auto& splatTex1 = (chunk.GpuSplatTexture1 && chunk.GpuSplatTexture1->GetRendererID() != 0)
            ? chunk.GpuSplatTexture1
            : s_TerrainData->FallbackSplatTex1;

        if (!activeVao || boundResolution != chunk.SplatResolution) {
            boundResolution = chunk.SplatResolution;
            activeVao = GetOrCreateStitchedVAO(boundResolution);
            activeVao->Bind();
        }

        splatTex0->Bind(0);
        splatTex1->Bind(1);
        shader.SetFloat3("u_TerrainOrigin", origin);
        shader.SetFloat2("u_TerrainSizeXZ", { chunkSize, chunkSize });
        Renderer::DrawIndexed(activeVao, 6);
    }

    shader.Unbind();
}

void TerrainRenderer::SyncLayerTextures(const std::array<TerrainLayer, TerrainComponent::MaxLayers>& layers) {
    SyncLayerTextureArrays(layers);
}

size_t TerrainRenderer::GetCachedLayerSetCount() {
    return s_TerrainData ? s_TerrainData->LayerSetCache.size() : 0u;
}

uint64_t TerrainRenderer::GetActiveLayerSetHash() {
    return (s_TerrainData && s_TerrainData->ActiveLayerSet) ? s_TerrainData->ActiveLayerSet->SetHash : 0ull;
}

} // namespace Engine
