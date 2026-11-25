#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Components/PostProcessComponent.h"
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/TorchShadowMap.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/Tilemap.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Particles/DecalManager.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/UI/UIMinimapComponent.h"
#include "Engine/UI/UIRadialWheelComponent.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Core/Time.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>

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

struct DebugHitboxEntry {
    enum class ShapeType { Arc, Box, Circle } Type = ShapeType::Circle;
    glm::vec3 Position{ 0.0f };
    glm::vec2 ForwardXZ{ 0.0f, 1.0f };
    glm::vec2 SizeXZ{ 1.0f, 1.0f };
    float Radius = 1.0f;
    float HalfAngleDeg = 60.0f;
    float YawRad = 0.0f;
    float RemainingLifetime = 0.0f;
};

static bool s_ShowDebugGizmos = false;
static std::vector<DebugHitboxEntry> s_DebugHitboxes;

static void PruneDebugHitboxes(float dt) {
    for (auto it = s_DebugHitboxes.begin(); it != s_DebugHitboxes.end(); ) {
        if (it->RemainingLifetime <= 0.0f) {
            it = s_DebugHitboxes.erase(it);
        } else {
            it->RemainingLifetime -= dt;
            if (it->RemainingLifetime <= 0.0f) {
                it = s_DebugHitboxes.erase(it);
            } else {
                ++it;
            }
        }
    }
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
        ENGINE_CORE_WARN("SceneRenderer::OnViewportResize ignored zero dimension ({}x{})", width, height);
        return;
    }
    m_ViewportWidth = width;
    m_ViewportHeight = height;

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_Camera.SetAspectRatio(aspectRatio);

    uint32_t updatedCameras = 0;
    for (auto& go : scene.GetGameObjects()) {
        if (!go->IsActiveInHierarchy()) continue;
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            if (!cameraComp->FixedAspectRatio) {
                cameraComp->SetViewportSize(width, height);
                ++updatedCameras;
            }
        }
    }
    ENGINE_CORE_DEBUG("SceneRenderer: Viewport resized to {}x{} (aspect={:.3f}, updatedCameras={})",
                      width, height, aspectRatio, updatedCameras);
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
    ctx.decalManager = &scene.GetDecalManager();

    static const auto kCameraMask = Detail::GetComponentTypeMask<CameraComponent>();
    static const auto kDirLightMask = Detail::GetComponentTypeMask<DirectionalLightComponent>();
    static const auto kPostProcessMask = Detail::GetComponentTypeMask<PostProcessComponent>();
    static const auto kWindMask = Detail::GetComponentTypeMask<WindComponent>();
    static const auto kRigidbodyMask = Detail::GetComponentTypeMask<Rigidbody2D>();
    static const auto kPointLightMask = Detail::GetComponentTypeMask<PointLightComponent>();
    static const auto kTerrainMask = Detail::GetComponentTypeMask<TerrainComponent>();
    static const auto kTerrainChunkMask = Detail::GetComponentTypeMask<TerrainChunkManager>();
    static const auto kTilemapMask = Detail::GetComponentTypeMask<Tilemap>();
    static const auto kSpriteMask = Detail::GetComponentTypeMask<SpriteRenderer>();
    static const auto kSquashStretchMask = Detail::GetComponentTypeMask<SquashStretchComponent>();
    static const auto kParticleMask = Detail::GetComponentTypeMask<ParticleSystem>();
    static const auto kUIPanelMask = Detail::GetComponentTypeMask<UIPanel>();
    static const auto kUILabelMask = Detail::GetComponentTypeMask<UILabel>();
    static const auto kUIMinimapMask = Detail::GetComponentTypeMask<UIMinimapComponent>();
    static const auto kUIRadialWheelMask = Detail::GetComponentTypeMask<UIRadialWheelComponent>();

    ctx.activeCam = overrideCamera;
    if (!ctx.activeCam) {
        for (const auto& go : scene.GetGameObjects()) {
            if (!go->IsActiveInHierarchy()) {
                continue;
            }
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
    const glm::vec3 camFocal = ctx.activeCam->GetFocalPoint();
    const glm::vec3 camForward = ctx.activeCam->GetForward();
    const glm::vec3 camRightXZ = ctx.activeCam->GetRightXZ();

    // Pre-pass: Stream terrain chunks before locking entity iteration or caching m_GameObjects pointers.
    // UpdateStreaming() may spawn/acquire pooled chunk entities and reallocate m_GameObjects.
    m_TerrainChunkScratch.clear();
    for (const auto& go : scene.GetGameObjects()) {
        if (!go->IsActiveInHierarchy()) {
            continue;
        }
        if (go->GetComponentMask() & kTerrainChunkMask) {
            if (auto* chunkMgr = go->GetComponent<TerrainChunkManager>()) {
                m_TerrainChunkScratch.emplace_back(chunkMgr, go->GetTransform());
            }
        }
    }
    for (const auto& [chunkMgr, tr] : m_TerrainChunkScratch) {
        (void)tr;
        if (chunkMgr && chunkMgr->AutoStreamWithCamera) {
            chunkMgr->UpdateStreaming(camFocal);
        }
    }

    Scene::ScopedIterationGuard iterationGuard(scene);
    const auto& gameObjects = scene.GetGameObjects();

    m_StandeeCommands.clear();
    m_StandeeCommands.reserve(gameObjects.size());
    m_SilhouetteCommands.clear();
    m_ShadowStandeeCommands.clear();
    m_ShadowStandeeCommands.reserve(gameObjects.size());
    m_TorchShadowStandeeCommands.clear();
    m_PointLightScratch.clear();
    m_FoliageScratch.clear();
    m_FoliageScratch.reserve(8);
    m_TerrainScratch.clear();
    m_TerrainChunkScratch.clear();
    m_TilemapRenderScratch.clear();
    m_FlatDecalScratch.clear();
    m_ParticleScratch.clear();
    m_UIPanelScratch.clear();
    m_UILabelScratch.clear();
    m_UIMinimapScratch.clear();
    m_UIRadialWheelScratch.clear();

    // Pass 1: Scene-level singletons, point lights, global wind field, and dynamic foliage colliders
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
        if (mask & kPointLightMask) {
            if (auto* pl = gameObject->GetComponent<PointLightComponent>()) {
                auto* t = gameObject->GetTransform();
                const glm::vec3 basePos = t ? t->GetWorldPosition() : glm::vec3(0.0f);
                const glm::vec3 lightWorldPos = basePos + glm::vec3(0.0f, pl->HeightOffset, 0.0f);
                const float effectiveRadius = std::max(0.05f, pl->Radius);

                if (camFrustum.IntersectsSphere(lightWorldPos, effectiveRadius)) {
                    const float effectiveIntensity = pl->EvaluateEffectiveIntensity(
                        ctx.totalTime,
                        static_cast<uint32_t>(gameObject->GetUUID())
                    );
                    const glm::vec3 toFocal = lightWorldPos - camFocal;

                    PointLightCandidate candidate;
                    candidate.Uniform.PositionAndRadius = { lightWorldPos.x, lightWorldPos.y, lightWorldPos.z, effectiveRadius };
                    candidate.Uniform.ColorAndIntensity = { pl->Color.r, pl->Color.g, pl->Color.b, effectiveIntensity };
                    candidate.Uniform.Params = {
                        std::max(0.1f, pl->Falloff),
                        pl->CastShadows ? 1.0f : 0.0f,
                        std::max(0.25f, pl->ShadowSoftness),
                        std::clamp(pl->ShadowBias, 0.0001f, 0.05f)
                    };
                    candidate.CastShadows = pl->CastShadows;
                    candidate.Priority = static_cast<uint8_t>(pl->Priority);
                    candidate.DistToFocalSq = glm::dot(toFocal, toFocal);
                    candidate.SubmissionOrder = static_cast<uint32_t>(m_PointLightScratch.size());
                    m_PointLightScratch.push_back(candidate);
                }
            }
        }
    }

    if (!m_PointLightScratch.empty()) {
        auto compareLights = [](const PointLightCandidate& a, const PointLightCandidate& b) noexcept {
            if (a.CastShadows != b.CastShadows) {
                return a.CastShadows > b.CastShadows;
            }
            if (a.Priority != b.Priority) {
                return a.Priority > b.Priority;
            }
            const bool aNan = std::isnan(a.DistToFocalSq);
            const bool bNan = std::isnan(b.DistToFocalSq);
            if (aNan != bNan) {
                return !aNan; // Valid numbers come before NaNs
            }
            if (!aNan && a.DistToFocalSq != b.DistToFocalSq) {
                return a.DistToFocalSq < b.DistToFocalSq;
            }
            return a.SubmissionOrder < b.SubmissionOrder;
        };

        const size_t keepCount = std::min(m_PointLightScratch.size(),
                                          static_cast<size_t>(SceneLightingData::MaxPointLights));
        if (m_PointLightScratch.size() > keepCount) {
            std::partial_sort(
                m_PointLightScratch.begin(),
                m_PointLightScratch.begin() + static_cast<ptrdiff_t>(keepCount),
                m_PointLightScratch.end(),
                compareLights
            );
        } else {
            std::sort(m_PointLightScratch.begin(), m_PointLightScratch.end(), compareLights);
        }

        ctx.lighting.PointLightCount = static_cast<uint32_t>(keepCount);
        for (size_t i = 0; i < keepCount; ++i) {
            ctx.lighting.PointLights[i] = m_PointLightScratch[i].Uniform;
        }
    }

    const bool torchShadowsActive = TorchShadowMap::IsValid() &&
                                    ctx.lighting.PointLightCount > 0 &&
                                    ctx.lighting.PointLights[0].Params.y > 0.5f &&
                                    ctx.lighting.PointLights[0].ColorAndIntensity.a > 0.001f;
    const glm::vec3 torchPos = torchShadowsActive
        ? glm::vec3(ctx.lighting.PointLights[0].PositionAndRadius)
        : glm::vec3(0.0f);
    const float torchShadowRadius = torchShadowsActive
        ? std::clamp(ctx.lighting.PointLights[0].PositionAndRadius.w, TorchShadowMap::NearPlane + 0.5f, TorchShadowMap::MaxShadowRadius)
        : 0.0f;

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

    // Populate 2D spatial hash grid for foliage interactors (reusing scratch storage to avoid per-frame allocations)
    m_FoliageInteractorGrid.clear();
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
                    m_FoliageInteractorGrid[key].push_back(i);
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

        if (mask & kTerrainMask) {
            if (auto* terrain = gameObject->GetComponent<TerrainComponent>()) {
                m_TerrainScratch.emplace_back(terrain, gameObject->GetTransform());
            }
        }

        if (mask & kTerrainChunkMask) {
            if (auto* chunkMgr = gameObject->GetComponent<TerrainChunkManager>()) {
                m_TerrainChunkScratch.emplace_back(chunkMgr, gameObject->GetTransform());
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
                        bool inTorchShadow = false;
                        if (torchShadowsActive && sprite->GetCastShadows()) {
                            const float dx = worldPos.x - torchPos.x;
                            const float dz = worldPos.z - torchPos.z;
                            const float horizDistSq = dx * dx + dz * dz;
                            if (horizDistSq >= TorchShadowMap::NearPlane * TorchShadowMap::NearPlane) {
                                const glm::vec3 toBounds = boundsCenter - torchPos;
                                const float maxReach = torchShadowRadius + boundsRadius;
                                inTorchShadow = (glm::dot(toBounds, toBounds) <= maxReach * maxReach);
                            }
                        }

                        if (inCamera || inShadow || inTorchShadow) {
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
                            if (ctx.windComp && ctx.windComp->InteractiveRustle && rawWindWeight > 0.0001f && !m_FoliageInteractorGrid.empty()) {
                                const int32_t scx = static_cast<int32_t>(std::floor(worldPos.x / kSpatialCellSize));
                                const int32_t scz = static_cast<int32_t>(std::floor(worldPos.z / kSpatialCellSize));
                                const uint64_t cellKey = (static_cast<uint64_t>(static_cast<uint32_t>(scx)) << 32) |
                                                         static_cast<uint64_t>(static_cast<uint32_t>(scz));
                                const auto it = m_FoliageInteractorGrid.find(cellKey);
                                if (it != m_FoliageInteractorGrid.end() && !it->second.empty()) {
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
                            cmd.cutoutFX = sprite->GetCutoutFX();
                            cmd.renderSilhouette = sprite->GetRenderSilhouette();
                            cmd.silhouetteColor = sprite->GetSilhouetteColor();

                            if (inCamera) {
                                m_StandeeCommands.push_back(cmd);
                                if (cmd.renderSilhouette) {
                                    m_SilhouetteCommands.push_back(cmd);
                                }
                            }
                            if (inShadow) {
                                m_ShadowStandeeCommands.push_back(cmd);
                            }
                            if (inTorchShadow) {
                                m_TorchShadowStandeeCommands.push_back(cmd);
                            }
                        }
                    }
                }
            }
        }

        if (mask & kParticleMask) {
            if (auto* ps = gameObject->GetComponent<ParticleSystem>()) {
                if (ps->IsEnabled()) {
                    m_ParticleScratch.push_back(ps);
                }
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

        if (mask & kUIMinimapMask) {
            for (auto* minimap : gameObject->GetComponents<UIMinimapComponent>()) {
                m_UIMinimapScratch.push_back(minimap);
            }
        }

        if (mask & kUIRadialWheelMask) {
            for (auto* wheel : gameObject->GetComponents<UIRadialWheelComponent>()) {
                m_UIRadialWheelScratch.push_back(wheel);
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

void SceneRenderer::RenderPassTorchShadowMap(SceneRenderContext& ctx) {
    if (!TorchShadowMap::IsValid() || ctx.lighting.PointLightCount == 0) {
        return;
    }

    const auto& primaryLight = ctx.lighting.PointLights[0];
    if (primaryLight.Params.y < 0.5f || primaryLight.ColorAndIntensity.a <= 0.001f) {
        return;
    }

    const glm::vec3 torchPos = glm::vec3(primaryLight.PositionAndRadius);
    const float shadowRadius = std::clamp(
        primaryLight.PositionAndRadius.w,
        TorchShadowMap::NearPlane + 0.5f,
        TorchShadowMap::MaxShadowRadius
    );

    const auto quadrantMatrices = TorchShadowMap::ComputeAllQuadrantMatrices(torchPos, shadowRadius);

    ctx.lighting.TorchShadowsEnabled = true;
    ctx.lighting.TorchShadowMapTextureID = TorchShadowMap::GetDepthTextureID();
    ctx.lighting.TorchShadowPosRadius = { torchPos.x, torchPos.y, torchPos.z, shadowRadius };
    ctx.lighting.TorchShadowTexelSize = {
        1.0f / static_cast<float>(TorchShadowMap::GetAtlasWidth()),
        1.0f / static_cast<float>(TorchShadowMap::GetAtlasHeight())
    };
    ctx.lighting.TorchShadowSoftness = primaryLight.Params.z;
    ctx.lighting.TorchShadowBias = primaryLight.Params.w;
    ctx.lighting.TorchLightMatrices = quadrantMatrices;

    // Clear entire 4-quadrant depth atlas once before rendering per-quadrant standee geometry
    TorchShadowMap::BeginCapture();

    for (uint32_t q = 0; q < TorchShadowMap::QuadrantCount; ++q) {
        const auto quadrant = static_cast<TorchShadowQuadrant>(q);
        const glm::mat4& lightVP = quadrantMatrices[q];
        const Frustum quadFrustum = Frustum::FromViewProjection(lightVP);

        m_TorchQuadrantScratch.clear();
        for (const auto& cmd : m_TorchShadowStandeeCommands) {
            if (quadFrustum.IntersectsSphere(cmd.boundsCenter, cmd.boundsRadius)) {
                m_TorchQuadrantScratch.push_back(cmd);
            }
        }
        if (m_TorchQuadrantScratch.empty()) {
            continue;
        }

        TorchShadowMap::BeginQuadrant(quadrant);
        Renderer2D::BeginShadowPass(lightVP, 0.15f);
        if (ctx.windComp) {
            Renderer2D::SetWindData(ctx.BuildWindUniforms(TorchShadowMap::GetQuadrantRight(quadrant)));
        }
        Renderer2D::DrawStandeesTorchShadowParallel(m_TorchQuadrantScratch, torchPos);
        Renderer2D::EndScene();
    }

    TorchShadowMap::EndCapture();
}

void SceneRenderer::RenderPassGroundPlane(SceneRenderContext& ctx) {
    Renderer::SetDepthTest(true);
    Renderer::SetDepthMask(true);

    // 1A. Continuous 4-Layer Splatmap Terrain (Finite & Infinite Chunked Managers)
    bool hasSplatTerrain = false;
    Renderer::SetPolygonOffset(true, 1.0f, 1.0f);
    for (const auto& [terrain, transform] : m_TerrainScratch) {
        const glm::vec3 origin = transform ? transform->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec3 scale  = transform ? transform->GetWorldScale()    : glm::vec3(1.0f);
        TerrainRenderer::RenderTerrain(*terrain, origin, scale, *ctx.activeCam, &ctx.lighting);
        hasSplatTerrain = true;
    }
    for (const auto& [chunkMgr, transform] : m_TerrainChunkScratch) {
        const float groundY = transform ? transform->GetWorldPosition().y : 0.0f;
        TerrainRenderer::RenderChunkManager(*chunkMgr, groundY, *ctx.activeCam, &ctx.lighting);
        hasSplatTerrain = true;
    }

    // 1B. Discrete XZ Tilemaps (layered cleanly at +0.002f above splatmap when coexisting)
    Renderer::SetPolygonOffset(true, hasSplatTerrain ? 0.5f : 1.0f, hasSplatTerrain ? 0.5f : 1.0f);
    Renderer2D::BeginScene(*ctx.activeCam, ctx.lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.0f);

    const glm::mat4 groundFlatRotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const float tileOverlayLift = hasSplatTerrain ? 0.002f : 0.0f;

    for (const auto& [tilemap, transform] : m_TilemapRenderScratch) {
        if (!tilemap || !tilemap->tileSet) continue;

        const glm::vec3 mapOrigin = transform ? transform->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec3 mapScale  = transform ? transform->GetWorldScale()    : glm::vec3(1.0f);
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

        glm::vec3 decalPos = transform->GetWorldPosition();
        decalPos.y += 0.004f;
        const float decalEmissive = sprite->GetReceiveLighting() ? sprite->GetEmissive() : -1.0f;
        const glm::vec3 worldRot = transform->GetWorldRotation();
        const glm::vec3 worldScale = transform->GetWorldScale();

        if (std::abs(worldRot.y) < 1e-4f) {
            const glm::vec2 decalSize = { worldScale.x, worldScale.z };
            if (sprite->GetSubTexture()) {
                Renderer2D::DrawGroundTile(decalPos, decalSize, sprite->GetSubTexture(), sprite->GetColor(),
                                           sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            } else {
                Renderer2D::DrawGroundTile(decalPos, decalSize, sprite->GetTexture(), sprite->GetColor(),
                                           sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            }
        } else {
            const glm::mat4 decalMatrix = glm::translate(glm::mat4(1.0f), decalPos) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                          groundFlatRotation *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(worldScale.x, worldScale.z, 1.0f));

            if (sprite->GetSubTexture()) {
                Renderer2D::DrawQuad(decalMatrix, sprite->GetSubTexture(), sprite->GetColor(),
                                     sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            } else {
                Renderer2D::DrawQuad(decalMatrix, sprite->GetTexture(), sprite->GetColor(),
                                     sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
            }
        }
    }

    // Render ring-buffered ground decals (blood splatters, scorched craters) with depth write OFF
    // so fading translucent edges never punch holes in underlying decals
    if (ctx.decalManager && ctx.decalManager->GetActiveCount() > 0u) {
        Renderer2D::Flush();
        Renderer::SetDepthMask(false);
        ctx.decalManager->Render(ctx.activeCam->GetFrustum());
        Renderer2D::EndScene();
        Renderer::SetDepthMask(true);
    } else {
        Renderer2D::EndScene();
    }
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

    // Canopy Occlusion / Shadow Sight Silhouette Pass:
    // Second lightweight draw state for entities flagged RenderSilhouette == true occluded by foreground geometry.
    if (!m_SilhouetteCommands.empty()) {
        std::sort(m_SilhouetteCommands.begin(), m_SilhouetteCommands.end(), [](const auto& a, const auto& b) noexcept {
            return a.viewDepth > b.viewDepth;
        });

        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        Renderer2D::BeginScene(*ctx.activeCam, ctx.lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.01f);
        if (ctx.windComp) {
            Renderer2D::SetWindData(ctx.BuildWindUniforms(camRightXZ));
        }

        Renderer2D::DrawStandeesParallel(m_SilhouetteCommands, ctx.activeCam->GetRight(), ctx.activeCam->GetUp(), camRightXZ,
                                         /*isShadowPass=*/false, /*isTorchShadowPass=*/false, /*isSilhouettePass=*/true);

        Renderer2D::EndScene();

        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
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
            particleEmissive,
            particleSystem->ParticleTexture.get(),
            particleSystem->Orientation,
            particleSystem->Flipbook
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

    std::stable_sort(m_UIPanelScratch.begin(), m_UIPanelScratch.end(),
        [](const UIPanel* a, const UIPanel* b) {
            return (a && b) ? (a->rectTransform.ZOrder < b->rectTransform.ZOrder) : (a != nullptr);
        });

    std::stable_sort(m_UILabelScratch.begin(), m_UILabelScratch.end(),
        [](const UILabel* a, const UILabel* b) {
            return (a && b) ? (a->rectTransform.ZOrder < b->rectTransform.ZOrder) : (a != nullptr);
        });

    const float dpiScale = UICanvas::ComputeScaleFactor(m_ViewportWidth, m_ViewportHeight);

    for (UIPanel* uiPanel : m_UIPanelScratch) {
        if (!uiPanel) continue;

        glm::vec2 pos = uiPanel->position;
        glm::vec2 sz  = uiPanel->size;
        if (uiPanel->useRectTransform) {
            UICanvas::ResolveScreenRect(uiPanel->rectTransform, m_ViewportWidth, m_ViewportHeight, pos, sz);
        }

        const auto quads = uiPanel->GenerateQuads(pos, sz, dpiScale);
        for (const auto& quad : quads) {
            const glm::vec3 positions[4] = {
                { quad.Vertices[0].Position.x, quad.Vertices[0].Position.y, 0.0f },
                { quad.Vertices[1].Position.x, quad.Vertices[1].Position.y, 0.0f },
                { quad.Vertices[2].Position.x, quad.Vertices[2].Position.y, 0.0f },
                { quad.Vertices[3].Position.x, quad.Vertices[3].Position.y, 0.0f }
            };
            const glm::vec2 texCoords[4] = {
                quad.Vertices[0].TexCoord,
                quad.Vertices[1].TexCoord,
                quad.Vertices[2].TexCoord,
                quad.Vertices[3].TexCoord
            };

            if (uiPanel->texture) {
                Renderer2D::DrawQuad(positions, texCoords, uiPanel->texture, quad.Color);
            } else {
                Renderer2D::DrawQuad(positions, texCoords, quad.Color);
            }
        }
    }

    for (UILabel* uiLabel : m_UILabelScratch) {
        if (!uiLabel || !uiLabel->font || uiLabel->text.empty()) continue;

        glm::vec2 startPos = uiLabel->position;
        float effectiveScale = uiLabel->scale;
        float maxWrap = uiLabel->maxWrapWidth;

        if (uiLabel->useRectTransform) {
            glm::vec2 resolvedPos, resolvedSize;
            UICanvas::ResolveScreenRect(uiLabel->rectTransform, m_ViewportWidth, m_ViewportHeight, resolvedPos, resolvedSize);
            startPos = resolvedPos;
            effectiveScale = uiLabel->scale * dpiScale;
            if (maxWrap > 0.0f) {
                maxWrap *= dpiScale;
            } else if (resolvedSize.x > 0.0f && uiLabel->alignment != TextAlignH::Left) {
                maxWrap = resolvedSize.x;
            }
        }

        const TextLayoutResult layout = uiLabel->font->ComputeLayout(
            uiLabel->text,
            startPos,
            effectiveScale,
            maxWrap,
            uiLabel->alignment
        );

        // Pass 1: Drop Shadow
        if (uiLabel->dropShadow && uiLabel->shadowColor.a > 0.0f) {
            const glm::vec2 sOffset = uiLabel->shadowOffset * dpiScale;
            for (const auto& glyph : layout.glyphs) {
                const glm::vec3 positions[4] = {
                    { glyph.position.x + sOffset.x,                glyph.position.y + sOffset.y,                0.0f },
                    { glyph.position.x + glyph.size.x + sOffset.x, glyph.position.y + sOffset.y,                0.0f },
                    { glyph.position.x + glyph.size.x + sOffset.x, glyph.position.y + glyph.size.y + sOffset.y, 0.0f },
                    { glyph.position.x + sOffset.x,                glyph.position.y + glyph.size.y + sOffset.y, 0.0f }
                };

                if (glyph.subTexture && glyph.subTexture->GetTexture()) {
                    const glm::vec2 texCoords[4] = {
                        { glyph.subTexture->GetMin().x, glyph.subTexture->GetMin().y },
                        { glyph.subTexture->GetMax().x, glyph.subTexture->GetMin().y },
                        { glyph.subTexture->GetMax().x, glyph.subTexture->GetMax().y },
                        { glyph.subTexture->GetMin().x, glyph.subTexture->GetMax().y }
                    };
                    Renderer2D::DrawQuad(positions, texCoords, glyph.subTexture->GetTexture(), uiLabel->shadowColor);
                } else {
                    static const glm::vec2 kDefaultUV[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };
                    Renderer2D::DrawQuad(positions, kDefaultUV, uiLabel->shadowColor);
                }
            }
        }

        // Pass 2: Main Text
        for (const auto& glyph : layout.glyphs) {
            const glm::vec3 positions[4] = {
                { glyph.position.x,                glyph.position.y,                0.0f },
                { glyph.position.x + glyph.size.x, glyph.position.y,                0.0f },
                { glyph.position.x + glyph.size.x, glyph.position.y + glyph.size.y, 0.0f },
                { glyph.position.x,                glyph.position.y + glyph.size.y, 0.0f }
            };

            if (glyph.subTexture && glyph.subTexture->GetTexture()) {
                const glm::vec2 texCoords[4] = {
                    { glyph.subTexture->GetMin().x, glyph.subTexture->GetMin().y },
                    { glyph.subTexture->GetMax().x, glyph.subTexture->GetMin().y },
                    { glyph.subTexture->GetMax().x, glyph.subTexture->GetMax().y },
                    { glyph.subTexture->GetMin().x, glyph.subTexture->GetMax().y }
                };
                Renderer2D::DrawQuad(positions, texCoords, glyph.subTexture->GetTexture(), uiLabel->color);
            } else {
                static const glm::vec2 kDefaultUV[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };
                Renderer2D::DrawQuad(positions, kDefaultUV, uiLabel->color);
            }
        }
    }

    // Pass: Minimap HUD widgets
    for (UIMinimapComponent* minimap : m_UIMinimapScratch) {
        if (!minimap) continue;
        glm::vec2 pos = { 0.0f, 0.0f };
        glm::vec2 sz  = { 220.0f, 220.0f };
        if (minimap->useRectTransform) {
            UICanvas::ResolveScreenRect(minimap->rectTransform, m_ViewportWidth, m_ViewportHeight, pos, sz);
        }

        const auto quads = minimap->GenerateQuads(pos, sz, dpiScale);
        for (const auto& quad : quads) {
            const glm::vec3 positions[4] = {
                { quad.Vertices[0].Position.x, quad.Vertices[0].Position.y, 0.0f },
                { quad.Vertices[1].Position.x, quad.Vertices[1].Position.y, 0.0f },
                { quad.Vertices[2].Position.x, quad.Vertices[2].Position.y, 0.0f },
                { quad.Vertices[3].Position.x, quad.Vertices[3].Position.y, 0.0f }
            };
            const glm::vec2 texCoords[4] = {
                quad.Vertices[0].TexCoord,
                quad.Vertices[1].TexCoord,
                quad.Vertices[2].TexCoord,
                quad.Vertices[3].TexCoord
            };

            if (quad.QuadTexture && quad.QuadTexture->GetRendererID() != 0) {
                Renderer2D::DrawQuad(positions, texCoords, quad.QuadTexture, quad.Color);
            } else {
                Renderer2D::DrawQuad(positions, texCoords, quad.Color);
            }
        }
    }

    // Pass: Tactical Radial Drop Wheels (when open)
    for (UIRadialWheelComponent* wheel : m_UIRadialWheelScratch) {
        if (!wheel || !wheel->IsOpen) continue;
        glm::vec2 pos = { 0.0f, 0.0f };
        glm::vec2 sz  = { 360.0f, 360.0f };
        if (wheel->useRectTransform) {
            UICanvas::ResolveScreenRect(wheel->rectTransform, m_ViewportWidth, m_ViewportHeight, pos, sz);
        }

        const auto quads = wheel->GenerateQuads(pos, sz, dpiScale);
        for (const auto& quad : quads) {
            const glm::vec3 positions[4] = {
                { quad.Vertices[0].Position.x, quad.Vertices[0].Position.y, 0.0f },
                { quad.Vertices[1].Position.x, quad.Vertices[1].Position.y, 0.0f },
                { quad.Vertices[2].Position.x, quad.Vertices[2].Position.y, 0.0f },
                { quad.Vertices[3].Position.x, quad.Vertices[3].Position.y, 0.0f }
            };
            const glm::vec2 texCoords[4] = {
                quad.Vertices[0].TexCoord,
                quad.Vertices[1].TexCoord,
                quad.Vertices[2].TexCoord,
                quad.Vertices[3].TexCoord
            };

            if (quad.QuadTexture && quad.QuadTexture->GetRendererID() != 0) {
                Renderer2D::DrawQuad(positions, texCoords, quad.QuadTexture, quad.Color);
            } else {
                Renderer2D::DrawQuad(positions, texCoords, quad.Color);
            }
        }
    }

    Renderer2D::EndScene();
}

void SceneRenderer::SetShowDebugGizmos(bool show) noexcept {
    s_ShowDebugGizmos = show;
}

bool SceneRenderer::IsShowingDebugGizmos() noexcept {
    return s_ShowDebugGizmos;
}

void SceneRenderer::AddDebugHitboxArc(const glm::vec3& origin, const glm::vec2& forwardXZ, float radius, float halfAngleDeg, float lifetimeSec) {
    DebugHitboxEntry entry;
    entry.Type = DebugHitboxEntry::ShapeType::Arc;
    entry.Position = origin;
    entry.ForwardXZ = forwardXZ;
    entry.Radius = radius;
    entry.HalfAngleDeg = halfAngleDeg;
    entry.RemainingLifetime = lifetimeSec;
    s_DebugHitboxes.push_back(entry);
}

void SceneRenderer::AddDebugHitboxBox(const glm::vec3& center, const glm::vec2& sizeXZ, float yawRad, float lifetimeSec) {
    DebugHitboxEntry entry;
    entry.Type = DebugHitboxEntry::ShapeType::Box;
    entry.Position = center;
    entry.SizeXZ = sizeXZ;
    entry.YawRad = yawRad;
    entry.RemainingLifetime = lifetimeSec;
    s_DebugHitboxes.push_back(entry);
}

void SceneRenderer::AddDebugHitboxCircle(const glm::vec3& center, float radius, float lifetimeSec) {
    DebugHitboxEntry entry;
    entry.Type = DebugHitboxEntry::ShapeType::Circle;
    entry.Position = center;
    entry.Radius = radius;
    entry.RemainingLifetime = lifetimeSec;
    s_DebugHitboxes.push_back(entry);
}

void SceneRenderer::ClearDebugHitboxes() {
    s_DebugHitboxes.clear();
}

size_t SceneRenderer::GetDebugHitboxCount() noexcept {
    return s_DebugHitboxes.size();
}

void SceneRenderer::RenderPassDebugGizmos(SceneRenderContext& ctx, Scene& scene) {
    if (!ctx.activeCam) {
        return;
    }

    static const glm::vec4 kColorStaticCollider  { 0.10f, 0.95f, 0.20f, 0.95f }; // Green
    static const glm::vec4 kColorDynamicCollider { 0.00f, 0.90f, 1.00f, 0.95f }; // Cyan
    static const glm::vec4 kColorSensorTrigger   { 1.00f, 0.90f, 0.10f, 0.95f }; // Yellow
    static const glm::vec4 kColorActiveHitbox    { 1.00f, 0.15f, 0.15f, 0.95f }; // Red
    static const glm::vec4 kColorTorchInnerSafe  { 1.00f, 0.84f, 0.10f, 0.90f }; // Radiant Gold
    static const glm::vec4 kColorTorchSearingRim { 1.00f, 0.45f, 0.00f, 0.90f }; // Warm Orange
    static const glm::vec4 kColorTerrainChunkGrid{ 0.45f, 0.45f, 0.45f, 0.55f }; // Dim Slate Grey

    // Render in world-space using active camera, depth testing disabled so wireframes overlay world geometry
    Renderer2D::BeginScene(*ctx.activeCam, /*depthTest=*/false, /*defaultAlphaCutoff=*/0.0f);

    // 1. Terrain Chunk Borders (Dim Slate Grey)
    for (const auto& [chunkMgr, transform] : m_TerrainChunkScratch) {
        if (!chunkMgr) continue;
        const float groundY = transform ? transform->GetWorldPosition().y + 0.01f : 0.01f;
        const float chunkSize = chunkMgr->ChunkSizeMeters > 0.0f ? chunkMgr->ChunkSizeMeters : 16.0f;
        const auto& loadedChunks = chunkMgr->GetChunks();
        for (const auto& [coord, chunk] : loadedChunks) {
            const float x0 = static_cast<float>(coord.cx) * chunkSize;
            const float z0 = static_cast<float>(coord.cz) * chunkSize;
            const float x1 = x0 + chunkSize;
            const float z1 = z0 + chunkSize;
            Renderer2D::DrawDebugLine3D({ x0, groundY, z0 }, { x1, groundY, z0 }, kColorTerrainChunkGrid);
            Renderer2D::DrawDebugLine3D({ x1, groundY, z0 }, { x1, groundY, z1 }, kColorTerrainChunkGrid);
            Renderer2D::DrawDebugLine3D({ x1, groundY, z1 }, { x0, groundY, z1 }, kColorTerrainChunkGrid);
            Renderer2D::DrawDebugLine3D({ x0, groundY, z1 }, { x0, groundY, z0 }, kColorTerrainChunkGrid);
        }
    }
    for (const auto& [terrain, transform] : m_TerrainScratch) {
        if (!terrain) continue;
        const glm::vec3 origin = transform ? transform->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec3 scale = transform ? transform->GetWorldScale() : glm::vec3(1.0f);
        const float w = terrain->GetWidth() * scale.x;
        const float d = terrain->GetDepth() * scale.z;
        const float groundY = origin.y + 0.01f;
        Renderer2D::DrawDebugWireBoxXZ({ origin.x + 0.5f * w, groundY, origin.z + 0.5f * d }, { w, d }, 0.0f, kColorTerrainChunkGrid);
    }

    // 2. Point Light Radii (Gold Inner Safe & Orange Outer Searing Rim)
    for (uint32_t i = 0; i < ctx.lighting.PointLightCount; ++i) {
        const auto& pl = ctx.lighting.PointLights[i];
        const glm::vec3 lightPos = { pl.PositionAndRadius.x, pl.PositionAndRadius.y, pl.PositionAndRadius.z };
        const float radius = pl.PositionAndRadius.w;
        if (radius <= 0.05f) continue;

        const glm::vec3 groundCenter = { lightPos.x, 0.02f, lightPos.z };
        const float innerSafeRadius = radius * 0.82f;

        // Inner Safe Zone: Radiant Gold
        Renderer2D::DrawDebugWireCircleXZ(groundCenter, innerSafeRadius, kColorTorchInnerSafe, 32);
        // Outer Searing Rim: Warm Orange
        Renderer2D::DrawDebugWireCircleXZ(groundCenter, radius, kColorTorchSearingRim, 32);
    }

    // 3. 2.5D Physics Colliders (Static Green, Dynamic Cyan, Sensor Yellow)
    for (const auto& go : scene.GetGameObjects()) {
        if (!go || !go->IsActiveInHierarchy()) continue;

        auto* rb = go->GetComponent<Rigidbody2D>();
        const bool isDynamic = (rb != nullptr && rb->Type != Rigidbody2D::BodyType::Static);

        auto* transform = go->GetTransform();
        const glm::vec3 worldPos = transform ? transform->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec3 worldScale = transform ? transform->GetWorldScale() : glm::vec3(1.0f);
        const float yawRad = transform ? glm::radians(transform->GetWorldRotation().y) : 0.0f;
        const float groundY = worldPos.y + 0.02f;

        // BoxCollider2D
        if (auto* box = go->GetComponent<BoxCollider2D>()) {
            const glm::vec4 colColor = box->IsSensor
                ? kColorSensorTrigger
                : (isDynamic ? kColorDynamicCollider : kColorStaticCollider);

            const glm::vec2 scaledSize = {
                box->size.x * std::abs(worldScale.x),
                box->size.y * std::abs(worldScale.z)
            };

            glm::vec2 offsetRotated = {
                box->offset.x * std::abs(worldScale.x),
                box->offset.y * std::abs(worldScale.z)
            };
            if (std::abs(yawRad) > 1e-5f) {
                const float cosY = std::cos(yawRad);
                const float sinY = std::sin(yawRad);
                const float rx = offsetRotated.x * cosY + offsetRotated.y * sinY;
                const float rz = -offsetRotated.x * sinY + offsetRotated.y * cosY;
                offsetRotated = { rx, rz };
            }

            const glm::vec3 boxCenter = {
                worldPos.x + offsetRotated.x,
                groundY,
                worldPos.z + offsetRotated.y
            };
            Renderer2D::DrawDebugWireBoxXZ(boxCenter, scaledSize, yawRad, colColor);
        }

        // CircleCollider2D
        if (auto* circle = go->GetComponent<CircleCollider2D>()) {
            const glm::vec4 colColor = circle->IsSensor
                ? kColorSensorTrigger
                : (isDynamic ? kColorDynamicCollider : kColorStaticCollider);

            const float scaledRadius = circle->Radius * std::max(std::abs(worldScale.x), std::abs(worldScale.z));

            glm::vec2 offsetRotated = {
                circle->OffsetXZ.x * std::abs(worldScale.x),
                circle->OffsetXZ.y * std::abs(worldScale.z)
            };
            if (std::abs(yawRad) > 1e-5f) {
                const float cosY = std::cos(yawRad);
                const float sinY = std::sin(yawRad);
                const float rx = offsetRotated.x * cosY + offsetRotated.y * sinY;
                const float rz = -offsetRotated.x * sinY + offsetRotated.y * cosY;
                offsetRotated = { rx, rz };
            }

            const glm::vec3 circleCenter = {
                worldPos.x + offsetRotated.x,
                groundY,
                worldPos.z + offsetRotated.y
            };
            Renderer2D::DrawDebugWireCircleXZ(circleCenter, scaledRadius, colColor, 32);
        }
    }

    // 4. Transient Combat Hitboxes (Red)
    for (const auto& hb : s_DebugHitboxes) {
        switch (hb.Type) {
            case DebugHitboxEntry::ShapeType::Arc:
                Renderer2D::DrawDebugWireArcXZ(hb.Position, hb.ForwardXZ, hb.Radius, hb.HalfAngleDeg, kColorActiveHitbox);
                break;
            case DebugHitboxEntry::ShapeType::Box:
                Renderer2D::DrawDebugWireBoxXZ(hb.Position, hb.SizeXZ, hb.YawRad, kColorActiveHitbox);
                break;
            case DebugHitboxEntry::ShapeType::Circle:
                Renderer2D::DrawDebugWireCircleXZ(hb.Position, hb.Radius, kColorActiveHitbox, 32);
                break;
        }
    }

    // Age and prune transient hitboxes
    PruneDebugHitboxes(Time::GetDeltaTime());

    Renderer2D::EndScene();
}

void SceneRenderer::Render(Scene& scene, const PerspectiveCamera* overrideCamera) {
    ResourceManager::ProcessPendingUploads();
    SceneRenderContext ctx = GatherRenderContext(scene, overrideCamera);

    RenderPassShadowMap(ctx);
    RenderPassTorchShadowMap(ctx);

    if (ctx.useHDRPostProcess) {
        PostProcessor::BeginHDRScene(m_ViewportWidth, m_ViewportHeight, ctx.postProcess->BackdropColor);
    }

    Renderer::UploadPointLights(&ctx.lighting);
    RenderPassGroundPlane(ctx);
    RenderPassCutoutStandees(ctx);
    RenderPassParticles(ctx);

    if (ctx.useHDRPostProcess) {
        PostProcessor::EndHDRAndComposite(*ctx.postProcess);
    }

    if (s_ShowDebugGizmos) {
        RenderPassDebugGizmos(ctx, scene);
    } else {
        PruneDebugHitboxes(Time::GetDeltaTime());
    }

    RenderPassScreenUI();
}

} // namespace Engine
