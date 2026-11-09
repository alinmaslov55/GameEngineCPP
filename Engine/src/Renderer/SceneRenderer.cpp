#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/Time.h"
#include "Engine/Utils/Logger.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

inline void ComputeStandeeBoundingSphere(const Transform& transform,
                                         const glm::vec3& worldPos,
                                         glm::vec3& outCenter,
                                         float& outRadius) noexcept {
    const float width  = std::abs(transform.scale.x);
    const float height = std::abs(transform.scale.y);
    const float depth  = std::abs(transform.scale.z);
    outCenter = worldPos + glm::vec3(0.0f, 0.5f * height, 0.0f);
    // Generous bounding radius accounting for diagonal corners, squash/stretch, and wind sway
    outRadius = 0.85f * std::max({ width, height, depth }) + 0.75f;
}

} // namespace

SceneWindData SceneRenderContext::BuildWindUniforms(const glm::vec3& swayRightAxis) const {
    SceneWindData windData;
    if (windComp) {
        windData.WindActive = windComp->Enabled;
        windData.TimeSeconds = totalTime;
        windData.Strength = std::max(0.0f, windComp->Strength);
        windData.Speed = windComp->Speed;
        windData.Gustiness = std::clamp(windComp->Gustiness, 0.0f, 1.0f);
        windData.GustFrequency = windComp->GustFrequency;
    }
    windData.SwayRightAxis = swayRightAxis;
    return windData;
}

SceneRenderer::SceneRenderer()
    : m_Camera(32.0f, 16.0f / 9.0f, 0.1f, 250.0f) {
    m_Camera.SetFocalPoint({ 6.5f, 0.0f, 6.5f });
    m_Camera.SetDistance(15.0f);
    m_Camera.SetPitchDegrees(40.0f);
    m_Camera.SetYawDegrees(0.0f);
}

void SceneRenderer::OnViewportResize(Scene& scene, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    m_ViewportWidth = width;
    m_ViewportHeight = height;

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_Camera.SetAspectRatio(aspectRatio);

    for (auto& go : scene.GetGameObjects()) {
        if (!go->IsActiveInHierarchy()) continue;
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            if (!cameraComp->FixedAspectRatio) {
                cameraComp->SetViewportSize(width, height);
            }
        }
    }
}

const PerspectiveCamera& SceneRenderer::GetActiveCamera(const Scene& scene) const {
    for (const auto& go : scene.GetGameObjects()) {
        if (!go->IsActiveInHierarchy()) continue;
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            return cameraComp->Camera;
        }
    }
    return m_Camera;
}

SceneRenderContext SceneRenderer::GatherRenderContext(Scene& scene, const PerspectiveCamera* overrideCamera) {
    SceneRenderContext ctx;
    ctx.totalTime = Time::GetTotalTime();
    ctx.physicsAlpha = scene.GetPhysicsAlpha();

    static const auto kCameraMask = Detail::GetComponentTypeMask<CameraComponent>();
    static const auto kDirLightMask = Detail::GetComponentTypeMask<DirectionalLightComponent>();
    static const auto kPostProcessMask = Detail::GetComponentTypeMask<PostProcessComponent>();
    static const auto kWindMask = Detail::GetComponentTypeMask<WindComponent>();
    static const auto kRigidbodyMask = Detail::GetComponentTypeMask<Rigidbody2D>();
    static const auto kPointLightMask = Detail::GetComponentTypeMask<PointLightComponent>();
    static const auto kTerrainMask = Detail::GetComponentTypeMask<TerrainComponent>();
    static const auto kTilemapMask = Detail::GetComponentTypeMask<Tilemap>();
    static const auto kSpriteMask = Detail::GetComponentTypeMask<SpriteRenderer>();
    static const auto kSquashStretchMask = Detail::GetComponentTypeMask<SquashStretchComponent>();
    static const auto kParticleMask = Detail::GetComponentTypeMask<ParticleSystem>();
    static const auto kUIPanelMask = Detail::GetComponentTypeMask<UIPanel>();
    static const auto kUILabelMask = Detail::GetComponentTypeMask<UILabel>();

    const auto& gameObjects = scene.GetGameObjects();

    ctx.activeCam = overrideCamera;
    if (!ctx.activeCam) {
        for (auto& go : gameObjects) {
            if (go->GetComponentMask() & kCameraMask) {
                if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
                    cameraComp->SyncFromTransform(go->GetTransform());
                    ctx.activeCam = &cameraComp->Camera;
                    break;
                }
            }
        }
        if (!ctx.activeCam) {
            ctx.activeCam = &m_Camera;
        }
    }

    const Frustum& camFrustum = ctx.activeCam->GetFrustum();
    const glm::vec3 camEye = ctx.activeCam->GetPosition();
    const glm::vec3 camForward = ctx.activeCam->GetForward();
    const glm::vec3 camRightXZ = ctx.activeCam->GetRightXZ();

    m_StandeeCommands.clear();
    m_StandeeCommands.reserve(gameObjects.size());
    m_ShadowStandeeCommands.clear();
    m_ShadowStandeeCommands.reserve(gameObjects.size());
    m_FoliageScratch.clear();
    m_FoliageScratch.reserve(8);
    m_TerrainScratch.clear();
    m_TilemapRenderScratch.clear();
    m_FlatDecalScratch.clear();
    m_ParticleScratch.clear();
    m_UIPanelScratch.clear();
    m_UILabelScratch.clear();

    // Pass 1: Scene-level singletons (directional sun, post process, global wind field, dynamic foliage colliders)
    for (auto& gameObject : gameObjects) {
        if (!gameObject->IsActiveInHierarchy()) {
            continue;
        }
        const auto mask = gameObject->GetComponentMask();
        if (!ctx.dirLight && (mask & kDirLightMask)) {
            ctx.dirLight = gameObject->GetComponent<DirectionalLightComponent>();
        }
        if (!ctx.postProcess && (mask & kPostProcessMask)) {
            if (auto* pp = gameObject->GetComponent<PostProcessComponent>(); pp && pp->Enabled) {
                ctx.postProcess = pp;
            }
        }
        if ((!ctx.windComp || !ctx.windComp->Enabled) && (mask & kWindMask)) {
            if (auto* wc = gameObject->GetComponent<WindComponent>()) {
                if (!ctx.windComp || (!ctx.windComp->Enabled && wc->Enabled)) {
                    ctx.windComp = wc;
                }
            }
        }
        if (m_FoliageScratch.size() < 8 && (mask & kRigidbodyMask)) {
            if (auto* rb = gameObject->GetComponent<Rigidbody2D>(); rb && rb->Type == Rigidbody2D::BodyType::Dynamic) {
                if (auto* t = gameObject->GetTransform()) {
                    m_FoliageScratch.emplace_back(gameObject.get(), t->GetWorldPosition());
                }
            }
        }
    }

    if (ctx.dirLight) {
        ctx.lighting.LightingActive = true;
        ctx.lighting.SunDirection = ctx.dirLight->GetLightDirection();
        ctx.lighting.SunColor = ctx.dirLight->SunColor;
        ctx.lighting.SunIntensity = std::max(0.0f, ctx.dirLight->SunIntensity);
        ctx.lighting.AmbientColor = ctx.dirLight->AmbientColor;
        ctx.lighting.AmbientIntensity = std::max(0.0f, ctx.dirLight->AmbientIntensity);
        ctx.lighting.ShadowSoftness = std::max(0.0f, ctx.dirLight->ShadowSoftness);
        ctx.lighting.ShadowBias = std::clamp(ctx.dirLight->ShadowBias, 0.0001f, 0.05f);
    }

    const bool shadowsActive = (ctx.dirLight && ctx.dirLight->CastShadows && ctx.dirLight->SunIntensity > 0.001f && ShadowMap::IsValid());
    Frustum shadowFrustum;
    glm::vec3 sunRightXZ{ 1.0f, 0.0f, 0.0f };
    if (shadowsActive) {
        const float orthoRadius = std::clamp(ctx.activeCam->GetDistance() * 0.95f, 10.0f, 35.0f);
        ctx.lighting.LightSpaceMatrix = ctx.dirLight->ComputeLightSpaceMatrix(
            ctx.activeCam->GetFocalPoint(),
            orthoRadius,
            ShadowMap::GetResolution()
        );
        shadowFrustum = Frustum::FromViewProjection(ctx.lighting.LightSpaceMatrix);
        sunRightXZ = ctx.dirLight->GetSunRightXZ();
    }

    // Populate 2D spatial hash grid for foliage interactors
    std::unordered_map<uint64_t, std::vector<uint32_t>> foliageInteractorGrid;
    static constexpr float kFoliageInteractionRadius = 1.25f;
    static constexpr float kSpatialCellSize = 2.0f;
    if (ctx.windComp && ctx.windComp->InteractiveRustle && !m_FoliageScratch.empty()) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_FoliageScratch.size()); ++i) {
            const glm::vec3& pos = m_FoliageScratch[i].second;
            const int32_t minCX = static_cast<int32_t>(std::floor((pos.x - kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t maxCX = static_cast<int32_t>(std::floor((pos.x + kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t minCZ = static_cast<int32_t>(std::floor((pos.z - kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t maxCZ = static_cast<int32_t>(std::floor((pos.z + kFoliageInteractionRadius) / kSpatialCellSize));

            for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
                for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                                         static_cast<uint64_t>(static_cast<uint32_t>(cz));
                    foliageInteractorGrid[key].push_back(i);
                }
            }
        }
    }

    // Pass 2: Renderable entity dispatch, frustum culling, and StandeeRenderCommand caching
    for (auto& gameObject : gameObjects) {
        if (!gameObject->IsActiveInHierarchy()) {
            continue;
        }
        const auto mask = gameObject->GetComponentMask();

        if (mask & kPointLightMask) {
            if (auto* pl = gameObject->GetComponent<PointLightComponent>()) {
                if (ctx.lighting.PointLightCount < SceneLightingData::MaxPointLights) {
                    auto* t = gameObject->GetTransform();
                    const glm::vec3 basePos = t ? t->GetWorldPosition() : glm::vec3(0.0f);
                    const glm::vec3 lightWorldPos = basePos + glm::vec3(0.0f, pl->HeightOffset, 0.0f);
                    const float effectiveRadius = std::max(0.05f, pl->Radius);

                    if (camFrustum.IntersectsSphere(lightWorldPos, effectiveRadius)) {
                        const float effectiveIntensity = pl->EvaluateEffectiveIntensity(
                            ctx.totalTime,
                            static_cast<uint32_t>(gameObject->GetUUID())
                        );

                        PointLightUniform& uniformSlot = ctx.lighting.PointLights[ctx.lighting.PointLightCount++];
                        uniformSlot.PositionAndRadius = { lightWorldPos.x, lightWorldPos.y, lightWorldPos.z, effectiveRadius };
                        uniformSlot.ColorAndIntensity = { pl->Color.r, pl->Color.g, pl->Color.b, effectiveIntensity };
                        uniformSlot.Falloff = std::max(0.1f, pl->Falloff);
                    }
                }
            }
        }

        if (mask & kTerrainMask) {
            if (auto* terrain = gameObject->GetComponent<TerrainComponent>()) {
                m_TerrainScratch.emplace_back(terrain, gameObject->GetTransform());
            }
        }

        if (mask & kTilemapMask) {
            if (auto* tilemap = gameObject->GetComponent<Tilemap>()) {
                m_TilemapRenderScratch.emplace_back(tilemap, gameObject->GetTransform());
            }
        }

        if (mask & kSpriteMask) {
            if (auto* sprite = gameObject->GetComponent<SpriteRenderer>()) {
                if (auto* transform = gameObject->GetTransform()) {
                    const bool useInterp = scene.IsRuntimeRunning() && (ctx.physicsAlpha > 0.0f);
                    const glm::vec3 worldPos = useInterp
                        ? transform->GetInterpolatedWorldPosition(ctx.physicsAlpha)
                        : transform->GetWorldPosition();
                    const glm::vec3 worldRot = useInterp
                        ? transform->GetInterpolatedWorldRotation(ctx.physicsAlpha)
                        : transform->GetWorldRotation();

                    if (sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::FlatGround) {
                        const float decalRadius = 0.75f * std::max(std::abs(transform->scale.x), std::abs(transform->scale.z)) + 0.5f;
                        if (camFrustum.IntersectsSphere(worldPos, decalRadius)) {
                            m_FlatDecalScratch.emplace_back(sprite, transform);
                        }
                    } else {
                        glm::vec3 boundsCenter{ 0.0f };
                        float boundsRadius = 0.0f;
                        ComputeStandeeBoundingSphere(*transform, worldPos, boundsCenter, boundsRadius);

                        const bool inCamera = camFrustum.IntersectsSphere(boundsCenter, boundsRadius);
                        const bool inShadow = shadowsActive && sprite->GetCastShadows() && shadowFrustum.IntersectsSphere(boundsCenter, boundsRadius);

                        if (inCamera || inShadow) {
                            glm::vec2 scaleMult{ 1.0f, 1.0f };
                            float tiltOffset = 0.0f;
                            if (mask & kSquashStretchMask) {
                                if (const auto* ss = gameObject->GetComponent<SquashStretchComponent>()) {
                                    scaleMult = ss->GetScaleMultiplier();
                                    tiltOffset = ss->GetVisualTiltDegrees();
                                }
                            }

                            const glm::vec2 standeeSize = { transform->scale.x * scaleMult.x, transform->scale.y * scaleMult.y };
                            const float effectiveRotZ = transform->rotation.z + tiltOffset;

                            const float rawWindWeight = sprite->GetWindSwayWeight();
                            const float heightFactor = std::clamp(0.55f + 0.25f * std::abs(standeeSize.y), 0.50f, 1.45f);
                            const float windWeight = rawWindWeight * heightFactor;
                            const float spatialPhase = ctx.windComp
                                ? ctx.windComp->ComputeSpatialPhase(worldPos, sprite->GetWindPhaseOffset())
                                : sprite->GetWindPhaseOffset();

                            float camProximityBend = 0.0f;
                            float shadowProximityBend = 0.0f;
                            if (ctx.windComp && ctx.windComp->InteractiveRustle && rawWindWeight > 0.0001f && !foliageInteractorGrid.empty()) {
                                const int32_t scx = static_cast<int32_t>(std::floor(worldPos.x / kSpatialCellSize));
                                const int32_t scz = static_cast<int32_t>(std::floor(worldPos.z / kSpatialCellSize));
                                const uint64_t cellKey = (static_cast<uint64_t>(static_cast<uint32_t>(scx)) << 32) |
                                                         static_cast<uint64_t>(static_cast<uint32_t>(scz));
                                const auto it = foliageInteractorGrid.find(cellKey);
                                if (it != foliageInteractorGrid.end() && !it->second.empty()) {
                                    float camBendSum = 0.0f;
                                    float shadowBendSum = 0.0f;
                                    for (uint32_t interactorIdx : it->second) {
                                        const auto& [interactorObj, interactorPos] = m_FoliageScratch[interactorIdx];
                                        if (interactorObj == gameObject.get()) continue;
                                        if (inCamera) {
                                            camBendSum += ctx.windComp->EvaluateProximityBend(
                                                worldPos, interactorPos, camRightXZ, rawWindWeight, ctx.totalTime, 1.2f);
                                        }
                                        if (inShadow) {
                                            shadowBendSum += ctx.windComp->EvaluateProximityBend(
                                                worldPos, interactorPos, sunRightXZ, rawWindWeight, ctx.totalTime, 1.2f);
                                        }
                                    }
                                    if (inCamera) camProximityBend = std::clamp(camBendSum, -0.65f, 0.65f);
                                    if (inShadow) shadowProximityBend = std::clamp(shadowBendSum, -0.65f, 0.65f);
                                }
                            }

                            StandeeRenderCommand cmd;
                            cmd.transform = transform;
                            cmd.sprite = sprite;
                            cmd.worldPosition = worldPos;
                            cmd.worldRotation = worldRot;
                            cmd.scaleZ = transform->scale.z;
                            cmd.size = standeeSize;
                            cmd.rotationZ = effectiveRotZ;
                            cmd.windWeight = windWeight;
                            cmd.spatialPhase = spatialPhase;
                            cmd.camProximityBend = camProximityBend;
                            cmd.shadowProximityBend = shadowProximityBend;
                            cmd.alphaCutoff = sprite->GetAlphaCutoff();
                            cmd.emissive = sprite->GetReceiveLighting() ? sprite->GetEmissive() : -1.0f;
                            cmd.receiverOffset = std::max(0.48f, 0.55f * std::abs(standeeSize.x));
                            cmd.color = sprite->GetColor();
                            cmd.viewDepth = glm::dot(worldPos - camEye, camForward);
                            cmd.boundsCenter = boundsCenter;
                            cmd.boundsRadius = boundsRadius;

                            if (inCamera) {
                                m_StandeeCommands.push_back(cmd);
                            }
                            if (inShadow) {
                                m_ShadowStandeeCommands.push_back(cmd);
                            }
                        }
                    }
                }
            }
        }

        if (mask & kParticleMask) {
            if (auto* ps = gameObject->GetComponent<ParticleSystem>()) {
                m_ParticleScratch.push_back(ps);
            }
        }

        if (mask & kUIPanelMask) {
            for (auto* panel : gameObject->GetComponents<UIPanel>()) {
                m_UIPanelScratch.push_back(panel);
            }
        }

        if (mask & kUILabelMask) {
            for (auto* label : gameObject->GetComponents<UILabel>()) {
                m_UILabelScratch.push_back(label);
            }
        }
    }

    if (!ctx.dirLight && ctx.lighting.PointLightCount > 0) {
        ctx.lighting.LightingActive = true;
        ctx.lighting.AmbientColor = { 0.55f, 0.55f, 0.62f };
        ctx.lighting.AmbientIntensity = 1.0f;
        ctx.lighting.SunIntensity = 0.0f;
    }

    ctx.useHDRPostProcess = (ctx.postProcess != nullptr && ctx.postProcess->Enabled && PostProcessor::IsValid());
    return ctx;
}

void SceneRenderer::RenderPassShadowMap(SceneRenderContext& ctx) {
    if (!ctx.dirLight || !ctx.dirLight->CastShadows || ctx.dirLight->SunIntensity <= 0.001f || !ShadowMap::IsValid()) {
        return;
    }

    const glm::vec3 sunRightXZ = ctx.dirLight->GetSunRightXZ();
    const glm::vec3 worldUp = { 0.0f, 1.0f, 0.0f };

    Renderer2D::BeginShadowPass(ctx.lighting.LightSpaceMatrix, 0.15f);
    if (ctx.windComp) {
        Renderer2D::SetWindData(ctx.BuildWindUniforms(sunRightXZ));
    }
    ShadowMap::BeginCapture();

    Renderer2D::DrawStandeesParallel(m_ShadowStandeeCommands, sunRightXZ, worldUp, sunRightXZ, /*isShadowPass=*/true);

    Renderer2D::EndScene();
    ShadowMap::EndCapture();

    ctx.lighting.ShadowsEnabled = true;
    ctx.lighting.ShadowMapTextureID = ShadowMap::GetDepthTextureID();
    ctx.lighting.ShadowMapTexelSize = 1.0f / static_cast<float>(ShadowMap::GetResolution());
}

void SceneRenderer::RenderPassGroundPlane(SceneRenderContext& ctx) {
    Renderer::SetDepthTest(true);
    Renderer::SetDepthMask(true);

    // 1A. Continuous 4-Layer Splatmap Terrain (Base Ground at Y = origin.y)
    bool hasSplatTerrain = false;
    Renderer::SetPolygonOffset(true, 1.0f, 1.0f);
    for (const auto& [terrain, transform] : m_TerrainScratch) {
        const glm::vec3 origin = transform ? transform->position : glm::vec3(0.0f);
        const glm::vec3 scale  = transform ? transform->scale    : glm::vec3(1.0f);
        TerrainRenderer::RenderTerrain(*terrain, origin, scale, *ctx.activeCam, &ctx.lighting);
        hasSplatTerrain = true;
    }

    // 1B. Discrete XZ Tilemaps (layered cleanly at +0.002f above splatmap when coexisting)
    Renderer::SetPolygonOffset(true, hasSplatTerrain ? 0.5f : 1.0f, hasSplatTerrain ? 0.5f : 1.0f);
    Renderer2D::BeginScene(*ctx.activeCam, ctx.lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.0f);

    const glm::mat4 groundFlatRotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const float tileOverlayLift = hasSplatTerrain ? 0.002f : 0.0f;

    for (const auto& [tilemap, transform] : m_TilemapRenderScratch) {
        if (!tilemap || !tilemap->tileSet) continue;

        const glm::vec3 mapOrigin = transform ? transform->position : glm::vec3(0.0f);
        const glm::vec3 mapScale  = transform ? transform->scale    : glm::vec3(1.0f);
        const float scaleX = (std::abs(mapScale.x) > 1e-4f) ? std::abs(mapScale.x) : 1.0f;
        const float scaleZ = (std::abs(mapScale.z) > 1e-4f) ? std::abs(mapScale.z) : 1.0f;

        glm::vec2 visMinXZ{ 0.0f };
        glm::vec2 visMaxXZ{ 0.0f };
        ctx.activeCam->GetGroundPlaneVisibleAABB(mapOrigin.y + tileOverlayLift, visMinXZ, visMaxXZ);

        const int mapW = static_cast<int>(tilemap->GetWidth());
        const int mapH = static_cast<int>(tilemap->GetHeight());

        const int minX = std::max(0, static_cast<int>(std::floor((visMinXZ.x - mapOrigin.x) / scaleX)) - 1);
        const int maxX = std::min(mapW - 1, static_cast<int>(std::ceil((visMaxXZ.x - mapOrigin.x) / scaleX)) + 1);
        const int minZ = std::max(0, static_cast<int>(std::floor((visMinXZ.y - mapOrigin.z) / scaleZ)) - 1);
        const int maxZ = std::min(mapH - 1, static_cast<int>(std::ceil((visMaxXZ.y - mapOrigin.z) / scaleZ)) + 1);

        if (minX > maxX || minZ > maxZ) {
            continue;
        }

        const glm::vec2 tileSize = { mapScale.x, mapScale.z };

        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const uint32_t ux = static_cast<uint32_t>(x);
                const uint32_t uz = static_cast<uint32_t>(z);
                const uint32_t tileId = tilemap->GetTile(ux, uz).id;
                if (tileId == 0) continue;

                const auto& subTexture = tilemap->tileSet->GetTileTexture(tileId);
                if (!subTexture) continue;

                const glm::vec3 worldPos = mapOrigin + glm::vec3(
                    static_cast<float>(x) * mapScale.x,
                    tileOverlayLift,
                    static_cast<float>(z) * mapScale.z
                );

                Renderer2D::DrawGroundTile(worldPos, tileSize, subTexture, glm::vec4(1.0f), 0.0f, 0.0f, 0.02f);
            }
        }
    }

    // Render FlatGround decal sprites just above the tilemap
    for (const auto& [sprite, transform] : m_FlatDecalScratch) {
        if (!sprite || !transform) continue;

        glm::vec3 decalPos = transform->position;
        decalPos.y += 0.004f;
        const float decalEmissive = sprite->GetReceiveLighting() ? sprite->GetEmissive() : -1.0f;

        if (std::abs(transform->rotation.y) < 1e-4f) {
            const glm::vec2 decalSize = { transform->scale.x, transform->scale.z };
            if (sprite->GetSubTexture()) {
                Renderer2D::DrawGroundTile(decalPos, decalSize, sprite->GetSubTexture(), sprite->GetColor(),
                                           sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            } else {
                Renderer2D::DrawGroundTile(decalPos, decalSize, sprite->GetTexture(), sprite->GetColor(),
                                           sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            }
        } else {
            const glm::mat4 decalMatrix = glm::translate(glm::mat4(1.0f), decalPos) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                          groundFlatRotation *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(transform->scale.x, transform->scale.z, 1.0f));

            if (sprite->GetSubTexture()) {
                Renderer2D::DrawQuad(decalMatrix, sprite->GetSubTexture(), sprite->GetColor(),
                                     sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            } else {
                Renderer2D::DrawQuad(decalMatrix, sprite->GetTexture(), sprite->GetColor(),
                                     sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            }
        }
    }

    Renderer2D::EndScene();
    Renderer::SetPolygonOffset(false);
}

void SceneRenderer::RenderPassCutoutStandees(SceneRenderContext& ctx) {
    // Schwartzian transform sort: back-to-front by precalculated viewDepth
    std::sort(m_StandeeCommands.begin(), m_StandeeCommands.end(), [](const auto& a, const auto& b) noexcept {
        return a.viewDepth > b.viewDepth;
    });

    const glm::vec3 camRightXZ = ctx.activeCam->GetRightXZ();
    Renderer2D::BeginScene(*ctx.activeCam, ctx.lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.15f);
    if (ctx.windComp) {
        Renderer2D::SetWindData(ctx.BuildWindUniforms(camRightXZ));
    }

    Renderer2D::DrawStandeesParallel(m_StandeeCommands, ctx.activeCam->GetRight(), ctx.activeCam->GetUp(), camRightXZ, /*isShadowPass=*/false);

    Renderer2D::EndScene();
}

void SceneRenderer::RenderPassParticles(SceneRenderContext& ctx) {
    Renderer::SetDepthMask(false);
    Renderer2D::BeginScene(*ctx.activeCam, ctx.lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.0f);

    for (ParticleSystem* particleSystem : m_ParticleScratch) {
        if (!particleSystem) continue;
        const uint32_t activeCount = particleSystem->GetActiveParticleCount();
        if (activeCount == 0) continue;

        const float particleEmissive = particleSystem->EmissiveParticles ? 1.65f : 0.0f;
        Renderer2D::DrawParticlesParallel(
            particleSystem->GetParticles().data(),
            activeCount,
            ctx.activeCam->GetRight(),
            ctx.activeCam->GetUp(),
            particleEmissive
        );
    }

    Renderer2D::EndScene();
    Renderer::SetDepthMask(true);
}

void SceneRenderer::RenderPassScreenUI() {
    glm::mat4 uiProjection = glm::ortho(
        0.0f, static_cast<float>(m_ViewportWidth),
        0.0f, static_cast<float>(m_ViewportHeight),
        -1.0f, 1.0f
    );

    Renderer2D::BeginScene(uiProjection, /*depthTest=*/false, /*defaultAlphaCutoff=*/0.0f);

    for (UIPanel* uiPanel : m_UIPanelScratch) {
        if (!uiPanel) continue;

        glm::mat4 uiTransform = glm::translate(glm::mat4(1.0f), glm::vec3(uiPanel->position, 0.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(uiPanel->size, 1.0f));

        if (uiPanel->texture) {
            Renderer2D::DrawQuad(uiTransform, uiPanel->texture, uiPanel->color);
        } else {
            Renderer2D::DrawQuad(uiTransform, uiPanel->color);
        }
    }

    for (UILabel* uiLabel : m_UILabelScratch) {
        if (!uiLabel || !uiLabel->font) continue;

        float cursorX = uiLabel->position.x;
        float cursorY = uiLabel->position.y;

        for (char c : uiLabel->text) {
            const Character& ch = uiLabel->font->GetCharacter(c);
            float xPos = cursorX + (ch.bearing.x * uiLabel->scale);
            float yPos = cursorY - ((ch.size.y - ch.bearing.y) * uiLabel->scale);
            glm::vec2 currentSize = ch.size * uiLabel->scale;

            glm::mat4 charTransform = glm::translate(glm::mat4(1.0f), glm::vec3(xPos, yPos, 0.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(currentSize, 1.0f));

            Renderer2D::DrawQuad(charTransform, ch.subTexture, uiLabel->color);
            cursorX += (ch.advance * uiLabel->scale);
        }
    }

    Renderer2D::EndScene();
}

void SceneRenderer::Render(Scene& scene, const PerspectiveCamera* overrideCamera) {
    ENGINE_CORE_TRACE("SceneRenderer: Render (2.5D Diorama)");

    ResourceManager::ProcessPendingUploads();
    SceneRenderContext ctx = GatherRenderContext(scene, overrideCamera);

    RenderPassShadowMap(ctx);

    if (ctx.useHDRPostProcess) {
        PostProcessor::BeginHDRScene(m_ViewportWidth, m_ViewportHeight, ctx.postProcess->BackdropColor);
    }

    RenderPassGroundPlane(ctx);
    RenderPassCutoutStandees(ctx);
    RenderPassParticles(ctx);

    if (ctx.useHDRPostProcess) {
        PostProcessor::EndHDRAndComposite(*ctx.postProcess);
    }

    RenderPassScreenUI();
}

} // namespace Engine
