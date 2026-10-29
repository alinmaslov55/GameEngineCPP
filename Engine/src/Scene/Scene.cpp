#include "Engine/Scene/Scene.h"
#include "Engine/Core/Time.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SceneLighting.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Math/Isometric.h"
#include "Engine/Utils/Logger.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Particles/ParticleSystem.h"

#include <box2d/box2d.h>
#include <box2d/math_functions.h>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Engine {
Scene::Scene(const std::string& name)
    : m_Name(name), m_Camera(32.0f, 16.0f / 9.0f, 0.1f, 250.0f) {
    m_Camera.SetFocalPoint({ 6.5f, 0.0f, 6.5f });
    m_Camera.SetDistance(15.0f);
    m_Camera.SetPitchDegrees(40.0f);
    m_Camera.SetYawDegrees(0.0f);
    ENGINE_CORE_INFO("Scene created: '{}'", m_Name);
}

Scene::~Scene() {
    if (m_IsRuntimeRunning) {
        OnRuntimeStop();
    }
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}'", m_Name, name);
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

GameObject* Scene::CreateGameObjectWithUUID(uint64_t uuid, const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}' with UUID {}", m_Name, name, uuid);
    auto gameObject = std::make_unique<GameObject>(uuid, name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

void Scene::OnViewportResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }

    m_ViewportWidth = width;
    m_ViewportHeight = height;

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_Camera.SetAspectRatio(aspectRatio);

    for (auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            if (!cameraComp->FixedAspectRatio) {
                cameraComp->SetViewportSize(width, height);
            }
        }
    }

    ENGINE_CORE_DEBUG("Scene '{}': Viewport resized to {}x{} (aspect: {:.3f})", m_Name, width, height, aspectRatio);
}

const PerspectiveCamera& Scene::GetActiveCamera() const {
    for (const auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            return cameraComp->Camera;
        }
    }
    return m_Camera;
}

void Scene::SetOrthographicSize(float size) {
    if (size <= 0.0f) {
        return;
    }
    m_OrthographicSize = size;
}

void Scene::DestroyGameObject(GameObject* gameObject) {
    if (!gameObject) {
        ENGINE_CORE_WARN("Scene '{}': Attempted to destroy null GameObject", m_Name);
        return;
    }
    ENGINE_CORE_DEBUG("Scene '{}': Destroying GameObject '{}'", m_Name, gameObject->GetName());
    m_GameObjects.erase(
        std::remove_if(m_GameObjects.begin(), m_GameObjects.end(),
            [gameObject](const std::unique_ptr<GameObject>& ptr) {
                return ptr.get() == gameObject;
            }),
        m_GameObjects.end()
    );
}

void Scene::Update(float deltaTime) {
    ENGINE_CORE_TRACE("Scene '{}': Update (deltaTime: {:.4f}s, {} GameObjects)", m_Name, deltaTime, m_GameObjects.size());
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }

    m_ColliderScratch.clear();
    m_TilemapScratch.clear();
    for (auto& gameObject : m_GameObjects) {
        if (auto* collider = gameObject->GetComponent<BoxCollider2D>()) {
            auto* transform = gameObject->GetTransform();
            m_ColliderScratch.push_back({ gameObject.get(), transform, collider });

            // Apply vertical isometric gravity & ground clamping for dynamic bodies
            if (collider->isDynamic && collider->useGravity && transform) {
                collider->verticalVelocity += collider->gravity * deltaTime;
                transform->position.y += collider->verticalVelocity * deltaTime;
                if (transform->position.y <= collider->groundLevel) {
                    transform->position.y = collider->groundLevel;
                    collider->verticalVelocity = 0.0f;
                }
            }
        }
        if (gameObject->GetComponent<Tilemap>()) {
            m_TilemapScratch.push_back(gameObject.get());
        }
    }

    // 1. Entity-vs-Entity AABB Collision on XZ Ground Plane
    for (size_t i = 0; i < m_ColliderScratch.size(); i++) {
        for (size_t j = i + 1; j < m_ColliderScratch.size(); j++) {
            const auto& entryA = m_ColliderScratch[i];
            const auto& entryB = m_ColliderScratch[j];

            auto* cA = entryA.collider;
            auto* cB = entryB.collider;

            if (!cA->isDynamic && !cB->isDynamic) continue;

            auto* tA = entryA.transform;
            auto* tB = entryB.transform;
            if (!tA || !tB) continue;

            // Calculate AABB bounds on the XZ ground plane for Object A
            float aLeft  = tA->position.x + cA->offset.x - (cA->size.x * tA->scale.x) / 2.0f;
            float aRight = tA->position.x + cA->offset.x + (cA->size.x * tA->scale.x) / 2.0f;
            float aBack  = tA->position.z + cA->offset.y - (cA->size.y * tA->scale.z) / 2.0f;
            float aFront = tA->position.z + cA->offset.y + (cA->size.y * tA->scale.z) / 2.0f;

            // Calculate AABB bounds on the XZ ground plane for Object B
            float bLeft  = tB->position.x + cB->offset.x - (cB->size.x * tB->scale.x) / 2.0f;
            float bRight = tB->position.x + cB->offset.x + (cB->size.x * tB->scale.x) / 2.0f;
            float bBack  = tB->position.z + cB->offset.y - (cB->size.y * tB->scale.z) / 2.0f;
            float bFront = tB->position.z + cB->offset.y + (cB->size.y * tB->scale.z) / 2.0f;

            float overlapX = std::min(aRight, bRight) - std::max(aLeft, bLeft);
            float overlapZ = std::min(aFront, bFront) - std::max(aBack, bBack);

            if (overlapX > 0 && overlapZ > 0) {
                ENGINE_CORE_TRACE("Scene '{}': Collision resolved between '{}' and '{}' (overlap: {:.3f}, {:.3f})",
                                  m_Name, entryA.gameObject->GetName(), entryB.gameObject->GetName(), overlapX, overlapZ);
                glm::vec2 push(0.0f);

                if (overlapX < overlapZ) {
                    push.x = (tA->position.x < tB->position.x) ? -overlapX : overlapX;
                } else {
                    push.y = (tA->position.z < tB->position.z) ? -overlapZ : overlapZ;
                }

                if (cA->isDynamic && !cB->isDynamic) {
                    tA->position.x += push.x;
                    tA->position.z += push.y;
                } else if (!cA->isDynamic && cB->isDynamic) {
                    tB->position.x -= push.x;
                    tB->position.z -= push.y;
                } else {
                    tA->position.x += push.x * 0.5f;
                    tA->position.z += push.y * 0.5f;
                    tB->position.x -= push.x * 0.5f;
                    tB->position.z -= push.y * 0.5f;
                }
            }
        }
    }

    // 2. Dynamic Entity-vs-Tilemap Non-Walkable Tile Collision on XZ Ground Plane
    for (const auto& entryA : m_ColliderScratch) {
        auto* cA = entryA.collider;
        if (!cA->isDynamic) continue;

        auto* tA = entryA.transform;
        if (!tA) continue;

        float halfW = (cA->size.x * tA->scale.x) * 0.5f;
        float halfD = (cA->size.y * tA->scale.z) * 0.5f;

        for (GameObject* mapObj : m_TilemapScratch) {
            auto* tilemap = mapObj->GetComponent<Tilemap>();
            auto* mapTransform = mapObj->GetTransform();
            if (!tilemap || tilemap->GetWidth() == 0 || tilemap->GetHeight() == 0 || !mapTransform) continue;

            float aLeft  = tA->position.x + cA->offset.x - halfW;
            float aRight = tA->position.x + cA->offset.x + halfW;
            float aBack  = tA->position.z + cA->offset.y - halfD;
            float aFront = tA->position.z + cA->offset.y + halfD;

            int minGridX = std::max(0, static_cast<int>(std::floor(aLeft - mapTransform->position.x + 0.5f)));
            int maxGridX = std::min(static_cast<int>(tilemap->GetWidth()) - 1,
                                    static_cast<int>(std::floor(aRight - mapTransform->position.x + 0.5f)));
            int minGridZ = std::max(0, static_cast<int>(std::floor(aBack - mapTransform->position.z + 0.5f)));
            int maxGridZ = std::min(static_cast<int>(tilemap->GetHeight()) - 1,
                                    static_cast<int>(std::floor(aFront - mapTransform->position.z + 0.5f)));

            for (int gz = minGridZ; gz <= maxGridZ; ++gz) {
                for (int gx = minGridX; gx <= maxGridX; ++gx) {
                    if (tilemap->GetTile(static_cast<uint32_t>(gx), static_cast<uint32_t>(gz)).walkable) {
                        continue;
                    }

                    // Recompute dynamic AABB in case a neighboring tile already adjusted tA->position
                    aLeft  = tA->position.x + cA->offset.x - halfW;
                    aRight = tA->position.x + cA->offset.x + halfW;
                    aBack  = tA->position.z + cA->offset.y - halfD;
                    aFront = tA->position.z + cA->offset.y + halfD;

                    float tileCenterX = mapTransform->position.x + static_cast<float>(gx);
                    float tileCenterZ = mapTransform->position.z + static_cast<float>(gz);
                    float tileHalfX   = 0.5f * mapTransform->scale.x;
                    float tileHalfZ   = 0.5f * mapTransform->scale.z;

                    float tLeft  = tileCenterX - tileHalfX;
                    float tRight = tileCenterX + tileHalfX;
                    float tBack  = tileCenterZ - tileHalfZ;
                    float tFront = tileCenterZ + tileHalfZ;

                    float overlapX = std::min(aRight, tRight) - std::max(aLeft, tLeft);
                    float overlapZ = std::min(aFront, tFront) - std::max(aBack, tBack);

                    if (overlapX > 0.0f && overlapZ > 0.0f) {
                        float centerAX = tA->position.x + cA->offset.x;
                        float centerAZ = tA->position.z + cA->offset.y;
                        if (overlapX < overlapZ) {
                            tA->position.x += (centerAX < tileCenterX) ? -overlapX : overlapX;
                        } else {
                            tA->position.z += (centerAZ < tileCenterZ) ? -overlapZ : overlapZ;
                        }
                    }
                }
            }
        }
    }
}

void Scene::OnRuntimeStart() {
    if (m_IsRuntimeRunning) {
        ENGINE_CORE_WARN("Scene '{}': OnRuntimeStart called while already running", m_Name);
        return;
    }

    ENGINE_CORE_INFO("Scene '{}': Starting Box2D runtime physics", m_Name);

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{ 0.0f, -9.8f };
    b2WorldId worldId = b2CreateWorld(&worldDef);
    m_PhysicsWorldId = b2StoreWorldId(worldId);
    m_IsRuntimeRunning = true;

    for (auto& go : m_GameObjects) {
        auto* transform = go->GetTransform();
        auto* rb2d = go->GetComponent<Rigidbody2D>();

        if (rb2d && transform) {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            if (rb2d->Type == Rigidbody2D::BodyType::Static) {
                bodyDef.type = b2_staticBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Dynamic) {
                bodyDef.type = b2_dynamicBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Kinematic) {
                bodyDef.type = b2_kinematicBody;
            }

            bodyDef.position = b2Vec2{ transform->position.x, transform->position.y };
            bodyDef.rotation = b2MakeRot(glm::radians(transform->rotation.z));
            bodyDef.fixedRotation = rb2d->FixedRotation;
            bodyDef.name = go->GetName().c_str();

            b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
            rb2d->RuntimeBody = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreBodyId(bodyId)));

            if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = bc2d->density;
                shapeDef.material.friction = bc2d->friction;
                shapeDef.material.restitution = bc2d->restitution;

                b2Vec2 center = { bc2d->offset.x, bc2d->offset.y };
                b2Rot rot = b2MakeRot(0.0f);
                float halfW = std::max(0.001f, bc2d->size.x * std::abs(transform->scale.x) * 0.5f);
                float halfH = std::max(0.001f, bc2d->size.y * std::abs(transform->scale.y) * 0.5f);
                b2Polygon box = b2MakeOffsetBox(halfW, halfH, center, rot);

                b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
                bc2d->RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            }
        }
    }
}

void Scene::OnRuntimeStop() {
    if (!m_IsRuntimeRunning) {
        return;
    }

    ENGINE_CORE_INFO("Scene '{}': Stopping Box2D runtime physics", m_Name);

    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (b2World_IsValid(worldId)) {
        b2DestroyWorld(worldId);
    }
    m_PhysicsWorldId = 0;
    m_IsRuntimeRunning = false;

    for (auto& go : m_GameObjects) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->RuntimeBody = nullptr;
        }
        if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
            bc2d->RuntimeFixture = nullptr;
        }
    }
}

void Scene::UpdateRuntime(float deltaTime) {
    if (m_IsRuntimeRunning) {
        b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
        if (b2World_IsValid(worldId)) {
            b2World_Step(worldId, deltaTime, 4);
        }

        // Sync Box2D physics simulation results back to Engine Transform
        for (auto& go : m_GameObjects) {
            auto* transform = go->GetTransform();
            auto* rb2d = go->GetComponent<Rigidbody2D>();

            if (rb2d && transform && rb2d->RuntimeBody) {
                b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
                if (b2Body_IsValid(bodyId)) {
                    b2Vec2 pos = b2Body_GetPosition(bodyId);
                    b2Rot rot = b2Body_GetRotation(bodyId);
                    transform->position.x = pos.x;
                    transform->position.y = pos.y;
                    transform->rotation.z = glm::degrees(b2Rot_GetAngle(rot));
                }
            }
        }
    }

    Update(deltaTime);
}

void Scene::Render(const PerspectiveCamera* overrideCamera) {
    ENGINE_CORE_TRACE("Scene '{}': Render (2.5D Diorama)", m_Name);

    const PerspectiveCamera* activeCam = overrideCamera;
    if (!activeCam) {
        for (auto& go : m_GameObjects) {
            if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
                cameraComp->SyncFromTransform(go->GetTransform());
                activeCam = &cameraComp->Camera;
                break;
            }
        }
        if (!activeCam) {
            activeCam = &m_Camera;
        }
    }

    // Gather upright 2.5D cutout standees once for both Pass 0 (Shadow Capture) and Pass 2 (Camera View)
    m_StandeeScratch.clear();
    m_StandeeScratch.reserve(m_GameObjects.size());

    // Gather dynamic 2.5D scene lighting, global Wind field, dynamic foliage interactors, and HDR PostProcess Volume
    SceneLightingData lighting;
    const DirectionalLightComponent* dirLight = nullptr;
    const PostProcessComponent* postProcess = nullptr;
    const WindComponent* windComp = nullptr;
    m_FoliageScratch.clear();
    m_FoliageScratch.reserve(8);
    const float totalTime = Time::GetTotalTime();

    for (auto& gameObject : m_GameObjects) {
        if (!dirLight) {
            dirLight = gameObject->GetComponent<DirectionalLightComponent>();
        }
        if (!postProcess) {
            if (auto* pp = gameObject->GetComponent<PostProcessComponent>(); pp && pp->Enabled) {
                postProcess = pp;
            }
        }
        if (auto* wc = gameObject->GetComponent<WindComponent>()) {
            if (!windComp || (!windComp->Enabled && wc->Enabled)) {
                windComp = wc;
            }
        }
        if (m_FoliageScratch.size() < 8) {
            if (auto* bc = gameObject->GetComponent<BoxCollider2D>(); bc && bc->isDynamic) {
                if (auto* t = gameObject->GetTransform()) {
                    m_FoliageScratch.emplace_back(gameObject.get(), t->position);
                }
            }
        }

        if (auto* pl = gameObject->GetComponent<PointLightComponent>()) {
            if (lighting.PointLightCount < SceneLightingData::MaxPointLights) {
                auto* t = gameObject->GetTransform();
                const glm::vec3 basePos = t ? t->position : glm::vec3(0.0f);
                const glm::vec3 lightWorldPos = basePos + glm::vec3(0.0f, pl->HeightOffset, 0.0f);
                const float effectiveIntensity = pl->EvaluateEffectiveIntensity(
                    totalTime,
                    static_cast<uint32_t>(gameObject->GetUUID())
                );

                PointLightUniform& uniformSlot = lighting.PointLights[lighting.PointLightCount++];
                uniformSlot.PositionAndRadius = { lightWorldPos.x, lightWorldPos.y, lightWorldPos.z, std::max(0.05f, pl->Radius) };
                uniformSlot.ColorAndIntensity = { pl->Color.r, pl->Color.g, pl->Color.b, effectiveIntensity };
                uniformSlot.Falloff = std::max(0.1f, pl->Falloff);
            }
        }

        auto* sprite = gameObject->GetComponent<SpriteRenderer>();
        if (sprite && gameObject->GetTransform() &&
            sprite->GetBillboardMode() != SpriteRenderer::BillboardMode::FlatGround) {
            m_StandeeScratch.push_back(gameObject.get());
        }
    }

    if (dirLight) {
        lighting.LightingActive = true;
        lighting.SunDirection = dirLight->GetLightDirection();
        lighting.SunColor = dirLight->SunColor;
        lighting.SunIntensity = std::max(0.0f, dirLight->SunIntensity);
        lighting.AmbientColor = dirLight->AmbientColor;
        lighting.AmbientIntensity = std::max(0.0f, dirLight->AmbientIntensity);
        lighting.ShadowSoftness = std::max(0.0f, dirLight->ShadowSoftness);
        lighting.ShadowBias = std::clamp(dirLight->ShadowBias, 0.0001f, 0.05f);
    } else if (lighting.PointLightCount > 0) {
        lighting.LightingActive = true;
        lighting.AmbientColor = { 0.55f, 0.55f, 0.62f };
        lighting.AmbientIntensity = 1.0f;
        lighting.SunIntensity = 0.0f;
    }

    auto buildWindUniforms = [&](const glm::vec3& swayRightAxis) {
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
    };

    auto resolveStandeeMotion = [&](GameObject* go,
                                    const SpriteRenderer* sprite,
                                    const glm::vec3& swayRightAxis,
                                    glm::vec2& outSize,
                                    float& outRotationZ,
                                    float& outWindWeight,
                                    float& outSpatialPhase,
                                    float& outProximityBend) {
        const auto* transform = go->GetTransform();
        glm::vec2 scaleMult = { 1.0f, 1.0f };
        float tiltOffset = 0.0f;
        if (const auto* ss = go->GetComponent<SquashStretchComponent>()) {
            scaleMult = ss->GetScaleMultiplier();
            tiltOffset = ss->GetVisualTiltDegrees();
        }

        outSize = { transform->scale.x * scaleMult.x, transform->scale.y * scaleMult.y };
        outRotationZ = transform->rotation.z + tiltOffset;

        const float rawWindWeight = sprite->GetWindSwayWeight();
        const float heightFactor = std::clamp(0.55f + 0.25f * std::abs(outSize.y), 0.50f, 1.45f);
        outWindWeight = rawWindWeight * heightFactor;
        outSpatialPhase = windComp
            ? windComp->ComputeSpatialPhase(transform->position, sprite->GetWindPhaseOffset())
            : sprite->GetWindPhaseOffset();

        outProximityBend = 0.0f;
        if (windComp && windComp->InteractiveRustle && rawWindWeight > 0.0001f) {
            float bendSum = 0.0f;
            for (const auto& [interactorObj, interactorPos] : m_FoliageScratch) {
                if (interactorObj == go) {
                    continue;
                }
                bendSum += windComp->EvaluateProximityBend(
                    transform->position,
                    interactorPos,
                    swayRightAxis,
                    rawWindWeight,
                    totalTime,
                    1.2f
                );
            }
            outProximityBend = std::clamp(bendSum, -0.65f, 0.65f);
        }
    };

    // ==========================================
    // PASS 0: DIRECTIONAL CUTOUT SHADOW MAP CAPTURE (2048x2048 24-bit Depth FBO)
    // ==========================================
    if (dirLight && dirLight->CastShadows && dirLight->SunIntensity > 0.001f && ShadowMap::IsValid()) {
        const float orthoRadius = std::clamp(activeCam->GetDistance() * 0.95f, 10.0f, 35.0f);
        lighting.LightSpaceMatrix = dirLight->ComputeLightSpaceMatrix(
            activeCam->GetFocalPoint(),
            orthoRadius,
            ShadowMap::GetResolution()
        );

        const glm::vec3 sunRightXZ = dirLight->GetSunRightXZ();
        const glm::vec3 worldUp = { 0.0f, 1.0f, 0.0f };

        Renderer2D::BeginShadowPass(lighting.LightSpaceMatrix, 0.15f);
        if (windComp) {
            Renderer2D::SetWindData(buildWindUniforms(sunRightXZ));
        }
        ShadowMap::BeginCapture();

        for (GameObject* gameObject : m_StandeeScratch) {
            auto* sprite = gameObject->GetComponent<SpriteRenderer>();
            if (!sprite->GetCastShadows()) {
                continue;
            }

            auto* transform = gameObject->GetTransform();
            const float cutoff = std::max(0.05f, sprite->GetAlphaCutoff());
            glm::vec2 standeeSize{ 1.0f };
            float effectiveRotZ = 0.0f;
            float windWeight = 0.0f;
            float spatialPhase = 0.0f;
            float proximityBend = 0.0f;
            resolveStandeeMotion(gameObject, sprite, sunRightXZ,
                                 standeeSize, effectiveRotZ, windWeight, spatialPhase, proximityBend);

            if (sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::ViewPlane ||
                sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::CylindricalY) {
                // Orient cutout standee perpendicular to the sun's horizontal azimuth at its Y=0 anchor
                // so silhouette shadows remain crisp and sway in lockstep with Pass 2 standees
                if (sprite->GetSubTexture()) {
                    Renderer2D::DrawBillboard(transform->position, standeeSize, sunRightXZ, worldUp,
                                              effectiveRotZ, sprite->GetSubTexture(), glm::vec4(1.0f),
                                              cutoff, -1.0f, 0.0f,
                                              windWeight, spatialPhase, proximityBend);
                } else {
                    Renderer2D::DrawBillboard(transform->position, standeeSize, sunRightXZ, worldUp,
                                              effectiveRotZ, sprite->GetTexture(), glm::vec4(1.0f),
                                              cutoff, -1.0f, 0.0f,
                                              windWeight, spatialPhase, proximityBend);
                }
            } else {
                const glm::vec3 scaled3D = { standeeSize.x, standeeSize.y, transform->scale.z };
                const glm::vec3 centerPos = transform->position + glm::vec3(0.0f, 0.5f * standeeSize.y, 0.0f);
                const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), centerPos) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                              glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRotZ), glm::vec3(0.0f, 0.0f, 1.0f)) *
                                              glm::scale(glm::mat4(1.0f), scaled3D);
                if (sprite->GetSubTexture()) {
                    Renderer2D::DrawQuad(modelMatrix, sprite->GetSubTexture(), glm::vec4(1.0f), cutoff, -1.0f, 0.0f);
                } else {
                    Renderer2D::DrawQuad(modelMatrix, sprite->GetTexture(), glm::vec4(1.0f), cutoff, -1.0f, 0.0f);
                }
            }
        }

        Renderer2D::EndScene();
        ShadowMap::EndCapture();

        lighting.ShadowsEnabled = true;
        lighting.ShadowMapTextureID = ShadowMap::GetDepthTextureID();
        lighting.ShadowMapTexelSize = 1.0f / static_cast<float>(ShadowMap::GetResolution());
    }

    const bool useHDRPostProcess = (postProcess != nullptr && postProcess->Enabled && PostProcessor::IsValid());
    if (useHDRPostProcess) {
        PostProcessor::BeginHDRScene(m_ViewportWidth, m_ViewportHeight, postProcess->BackdropColor);
    }

    // ==========================================
    // PASS 1: 3D GROUND PLANE (Splatmap Terrain -> Tilemaps -> Flat Decals)
    // ==========================================
    Renderer::SetDepthTest(true);
    Renderer::SetDepthMask(true);

    // 1A. Continuous 4-Layer Splatmap Terrain (Base Ground at Y = origin.y)
    bool hasSplatTerrain = false;
    Renderer::SetPolygonOffset(true, 1.0f, 1.0f);
    for (auto& gameObject : m_GameObjects) {
        if (auto* terrain = gameObject->GetComponent<TerrainComponent>()) {
            auto* transform = gameObject->GetTransform();
            const glm::vec3 origin = transform ? transform->position : glm::vec3(0.0f);
            const glm::vec3 scale  = transform ? transform->scale    : glm::vec3(1.0f);
            TerrainRenderer::RenderTerrain(*terrain, origin, scale, *activeCam, &lighting);
            hasSplatTerrain = true;
        }
    }

    // 1B. Discrete XZ Tilemaps (layered cleanly at +0.002f above splatmap when coexisting)
    Renderer::SetPolygonOffset(true, hasSplatTerrain ? 0.5f : 1.0f, hasSplatTerrain ? 0.5f : 1.0f);
    Renderer2D::BeginScene(*activeCam, lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.0f);

    const glm::mat4 groundFlatRotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const float tileOverlayLift = hasSplatTerrain ? 0.002f : 0.0f;

    for (auto& gameObject : m_GameObjects) {
        if (auto* tilemap = gameObject->GetComponent<Tilemap>()) {
            auto* transform = gameObject->GetTransform();
            const glm::vec3 mapOrigin = transform ? transform->position : glm::vec3(0.0f);
            const glm::vec3 mapScale  = transform ? transform->scale    : glm::vec3(1.0f);

            for (uint32_t z = 0; z < tilemap->GetHeight(); z++) {
                for (uint32_t x = 0; x < tilemap->GetWidth(); x++) {
                    const uint32_t tileId = tilemap->GetTile(x, z).id;
                    if (tileId == 0) continue;

                    auto subTexture = tilemap->tileSet->GetTileTexture(tileId);
                    if (!subTexture) continue;

                    const glm::vec3 worldPos = mapOrigin + glm::vec3(
                        static_cast<float>(x) * mapScale.x,
                        tileOverlayLift,
                        static_cast<float>(z) * mapScale.z
                    );

                    const glm::mat4 tileMatrix = glm::translate(glm::mat4(1.0f), worldPos) *
                                                 groundFlatRotation *
                                                 glm::scale(glm::mat4(1.0f), glm::vec3(mapScale.x, mapScale.z, 1.0f));

                    Renderer2D::DrawQuad(tileMatrix, subTexture, glm::vec4(1.0f), 0.0f, 0.0f, 0.02f);
                }
            }
        }
    }

    // Render FlatGround decal sprites just above the tilemap
    for (auto& gameObject : m_GameObjects) {
        auto* sprite = gameObject->GetComponent<SpriteRenderer>();
        auto* transform = gameObject->GetTransform();
        if (!sprite || !transform || sprite->GetBillboardMode() != SpriteRenderer::BillboardMode::FlatGround) {
            continue;
        }

        glm::vec3 decalPos = transform->position;
        decalPos.y += 0.004f;
        const glm::mat4 decalMatrix = glm::translate(glm::mat4(1.0f), decalPos) *
                                      glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                      groundFlatRotation *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(transform->scale.x, transform->scale.z, 1.0f));

        const float decalEmissive = sprite->GetReceiveLighting() ? sprite->GetEmissive() : -1.0f;
        if (sprite->GetSubTexture()) {
            Renderer2D::DrawQuad(decalMatrix, sprite->GetSubTexture(), sprite->GetColor(),
                                 sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
        } else {
            Renderer2D::DrawQuad(decalMatrix, sprite->GetTexture(), sprite->GetColor(),
                                 sprite->GetAlphaCutoff(), decalEmissive, 0.02f);
        }
    }

    Renderer2D::EndScene();
    Renderer::SetPolygonOffset(false);

    // ==========================================
    // PASS 2: UPRIGHT 2.5D CUTOUT STANDEES (Depth Test + Alpha Discard + Soft Shadow Receiving)
    // ==========================================
    const glm::vec3 camEye = activeCam->GetPosition();
    const glm::vec3 camForward = activeCam->GetForward();
    std::sort(m_StandeeScratch.begin(), m_StandeeScratch.end(), [&](GameObject* a, GameObject* b) {
        const float distA = glm::dot(a->GetTransform()->position - camEye, camForward);
        const float distB = glm::dot(b->GetTransform()->position - camEye, camForward);
        return distA > distB; // Back-to-front for clean outline anti-aliasing alongside Z-buffer cutout
    });

    const glm::vec3 camRightXZ = activeCam->GetRightXZ();
    Renderer2D::BeginScene(*activeCam, lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.15f);
    if (windComp) {
        Renderer2D::SetWindData(buildWindUniforms(camRightXZ));
    }

    for (GameObject* gameObject : m_StandeeScratch) {
        auto* sprite = gameObject->GetComponent<SpriteRenderer>();
        auto* transform = gameObject->GetTransform();
        const float cutoff = sprite->GetAlphaCutoff();
        glm::vec2 standeeSize{ 1.0f };
        float effectiveRotZ = 0.0f;
        float windWeight = 0.0f;
        float spatialPhase = 0.0f;
        float proximityBend = 0.0f;
        resolveStandeeMotion(gameObject, sprite, camRightXZ,
                             standeeSize, effectiveRotZ, windWeight, spatialPhase, proximityBend);

        const float standeeEmissive = sprite->GetReceiveLighting() ? sprite->GetEmissive() : -1.0f;
        const float receiverOffset = std::max(0.48f, 0.55f * std::abs(standeeSize.x));

        if (sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::ViewPlane) {
            if (sprite->GetSubTexture()) {
                Renderer2D::DrawBillboard(transform->position, standeeSize, activeCam->GetRight(), activeCam->GetUp(),
                                          effectiveRotZ, sprite->GetSubTexture(), sprite->GetColor(),
                                          cutoff, standeeEmissive, receiverOffset,
                                          windWeight, spatialPhase, proximityBend);
            } else {
                Renderer2D::DrawBillboard(transform->position, standeeSize, activeCam->GetRight(), activeCam->GetUp(),
                                          effectiveRotZ, sprite->GetTexture(), sprite->GetColor(),
                                          cutoff, standeeEmissive, receiverOffset,
                                          windWeight, spatialPhase, proximityBend);
            }
        } else if (sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::CylindricalY) {
            if (sprite->GetSubTexture()) {
                Renderer2D::DrawBillboard(transform->position, standeeSize, camRightXZ, glm::vec3(0.0f, 1.0f, 0.0f),
                                          effectiveRotZ, sprite->GetSubTexture(), sprite->GetColor(),
                                          cutoff, standeeEmissive, receiverOffset,
                                          windWeight, spatialPhase, proximityBend);
            } else {
                Renderer2D::DrawBillboard(transform->position, standeeSize, camRightXZ, glm::vec3(0.0f, 1.0f, 0.0f),
                                          effectiveRotZ, sprite->GetTexture(), sprite->GetColor(),
                                          cutoff, standeeEmissive, receiverOffset,
                                          windWeight, spatialPhase, proximityBend);
            }
        } else {
            const glm::vec3 scaled3D = { standeeSize.x, standeeSize.y, transform->scale.z };
            const glm::vec3 centerPos = transform->position + glm::vec3(0.0f, 0.5f * standeeSize.y, 0.0f);
            const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), centerPos) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRotZ), glm::vec3(0.0f, 0.0f, 1.0f)) *
                                          glm::scale(glm::mat4(1.0f), scaled3D);
            if (sprite->GetSubTexture()) {
                Renderer2D::DrawQuad(modelMatrix, sprite->GetSubTexture(), sprite->GetColor(),
                                     cutoff, standeeEmissive, receiverOffset);
            } else {
                Renderer2D::DrawQuad(modelMatrix, sprite->GetTexture(), sprite->GetColor(),
                                     cutoff, standeeEmissive, receiverOffset);
            }
        }
    }

    Renderer2D::EndScene();

    // ==========================================
    // PASS 3: 3D BILLBOARD PARTICLES (Depth Test ON, Depth Write OFF)
    // ==========================================
    Renderer::SetDepthMask(false);
    Renderer2D::BeginScene(*activeCam, lighting, /*depthTest=*/true, /*defaultAlphaCutoff=*/0.0f);

    for (auto& gameObject : m_GameObjects) {
        if (auto* particleSystem = gameObject->GetComponent<ParticleSystem>()) {
            const float particleEmissive = particleSystem->EmissiveParticles ? 1.65f : 0.0f;
            for (const auto& particle : particleSystem->GetParticles()) {
                if (!particle.active) continue;

                const float lifeRatio = particle.lifeRemaining / particle.lifeTime;
                const float size = glm::mix(particle.sizeEnd, particle.sizeBegin, lifeRatio);
                const glm::vec4 color = glm::mix(particle.colorEnd, particle.colorBegin, lifeRatio);

                const glm::vec3 anchor = particle.position - activeCam->GetUp() * (0.5f * size);
                Renderer2D::DrawBillboard(
                    anchor,
                    { size, size },
                    activeCam->GetRight(),
                    activeCam->GetUp(),
                    glm::degrees(particle.rotation),
                    color,
                    0.0f,
                    particleEmissive,
                    0.25f
                );
            }
        }
    }

    Renderer2D::EndScene();
    Renderer::SetDepthMask(true);

    if (useHDRPostProcess) {
        PostProcessor::EndHDRAndComposite(*postProcess);
    }

    // ==========================================
    // PASS 4: 2D SCREEN-SPACE UI HUD (Depth Test OFF)
    // ==========================================
    glm::mat4 uiProjection = glm::ortho(
        0.0f, static_cast<float>(m_ViewportWidth),
        0.0f, static_cast<float>(m_ViewportHeight),
        -1.0f, 1.0f
    );

    Renderer2D::BeginScene(uiProjection, /*depthTest=*/false, /*defaultAlphaCutoff=*/0.0f); 

    for (auto& gameObject : m_GameObjects) {
        for (auto* uiPanel : gameObject->GetComponents<UIPanel>()) {
            // UI elements don't use the standard Transform component.
            // We construct a simple model matrix using the screen-space pixel coordinates.
            glm::mat4 uiTransform = glm::translate(glm::mat4(1.0f), glm::vec3(uiPanel->position, 0.0f)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(uiPanel->size, 1.0f));

            if (uiPanel->texture) {
                Renderer2D::DrawQuad(uiTransform, uiPanel->texture, uiPanel->color);
            } else {
                Renderer2D::DrawQuad(uiTransform, uiPanel->color);
            }
        }
    }

    for (auto& gameObject : m_GameObjects) {
        for (auto* uiLabel : gameObject->GetComponents<UILabel>()) {
            if (uiLabel->font) {
                // Track the baseline starting position
                float cursorX = uiLabel->position.x;
                float cursorY = uiLabel->position.y;

                for (char c : uiLabel->text) {
                    const Character& ch = uiLabel->font->GetCharacter(c);

                    // Calculate the exact screen position of this specific character quad
                    float xPos = cursorX + (ch.bearing.x * uiLabel->scale);
                    
                    // Subtract (size.y - bearing.y) to align characters with hanging tails (like 'g' or 'p')
                    float yPos = cursorY - ((ch.size.y - ch.bearing.y) * uiLabel->scale);

                    glm::vec2 currentSize = ch.size * uiLabel->scale;

                    // Build the transform matrix
                    glm::mat4 charTransform = glm::translate(glm::mat4(1.0f), glm::vec3(xPos, yPos, 0.0f)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(currentSize, 1.0f));

                    // Draw the character
                    Renderer2D::DrawQuad(charTransform, ch.subTexture, uiLabel->color);

                    // Advance the cursor for the next character
                    cursorX += (ch.advance * uiLabel->scale);
                }
            }
        }
    }

    Renderer2D::EndScene();
}
} // namespace Engine
