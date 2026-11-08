#include "Engine/Scene/Scene.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/ResourceManager.h"
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
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/SceneSerializer.h"
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
#include <unordered_set>
#include <utility>
#include <vector>

namespace Engine {
namespace {
inline void ComputeStandeeBoundingSphere(const Transform& transform,
                                         glm::vec3& outCenter,
                                         float& outRadius) noexcept {
    const float width  = std::abs(transform.scale.x);
    const float height = std::abs(transform.scale.y);
    const float depth  = std::abs(transform.scale.z);
    outCenter = transform.GetWorldPosition() + glm::vec3(0.0f, 0.5f * height, 0.0f);
    // Generous bounding radius accounting for diagonal corners, squash/stretch, and wind sway
    outRadius = 0.85f * std::max({ width, height, depth }) + 0.75f;
}
} // namespace

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
    ClearAllPools();
    m_PendingDestroyQueue.clear();
    m_PendingSpawnQueue.clear();
    m_EntityUUIDMap.clear();
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}'", m_Name, name);
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    rawPtr->m_Scene = this;
    m_EntityUUIDMap[rawPtr->GetUUID()] = rawPtr;

    if (m_IsIteratingEntities) {
        m_PendingSpawnQueue.push_back(std::move(gameObject));
    } else {
        m_GameObjects.push_back(std::move(gameObject));
        m_StaticCollidersDirty = true;
    }
    return rawPtr;
}

GameObject* Scene::CreateGameObjectWithUUID(uint64_t uuid, const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}' with UUID {}", m_Name, name, uuid);
    auto gameObject = std::make_unique<GameObject>(uuid, name);
    GameObject* rawPtr = gameObject.get();
    rawPtr->m_Scene = this;
    m_EntityUUIDMap[rawPtr->GetUUID()] = rawPtr;

    if (m_IsIteratingEntities) {
        m_PendingSpawnQueue.push_back(std::move(gameObject));
    } else {
        m_GameObjects.push_back(std::move(gameObject));
        m_StaticCollidersDirty = true;
    }
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
        if (!go->IsActiveInHierarchy()) continue;
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
        if (!go->IsActiveInHierarchy()) continue;
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
    if (gameObject->IsPendingDestroy()) {
        return;
    }
    ENGINE_CORE_DEBUG("Scene '{}': Destroying GameObject '{}' (UUID: {})", m_Name, gameObject->GetName(), gameObject->GetUUID());
    gameObject->m_PendingDestroy = true;
    m_PendingDestroyQueue.push_back(gameObject);

    if (m_IsRuntimeRunning) {
        if (auto* rb2d = gameObject->GetComponent<Rigidbody2D>()) {
            if (rb2d->RuntimeBody) {
                b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
                if (b2Body_IsValid(bodyId)) {
                    b2Body_Disable(bodyId);
                }
            }
        }
    }

    if (gameObject->IsPooled()) {
        auto poolIt = m_EntityPools.find(gameObject->GetPoolKey());
        if (poolIt != m_EntityPools.end()) {
            std::erase(poolIt->second.freeList, gameObject);
            std::erase(poolIt->second.allInstances, gameObject);
        }
    }

    // Recursively cascade destruction to all child GameObjects in hierarchy
    if (auto* transform = gameObject->GetTransform()) {
        auto children = transform->GetChildren(); // Copy to avoid mutation while queueing
        for (auto* child : children) {
            if (child && child->GetGameObject()) {
                DestroyGameObject(child->GetGameObject());
            }
        }
    }

    if (!m_IsIteratingEntities) {
        FlushEntityLifecycleQueues();
    }
}

void Scene::RegisterRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    auto* transform = go->GetTransform();
    if (!transform) return;

    auto* rb2d = go->GetComponent<Rigidbody2D>();
    auto* bc2d = go->GetComponent<BoxCollider2D>();
    auto* cc2d = go->GetComponent<CircleCollider2D>();
    auto* cap2d = go->GetComponent<CapsuleCollider2D>();
    auto* tilemap = go->GetComponent<Tilemap>();

    if (rb2d) {
        // If there was a static body from previous standalone colliders, destroy it
        void* oldStaticBodyPtr = nullptr;
        if (bc2d && bc2d->RuntimeBody != nullptr && bc2d->RuntimeBody != rb2d->RuntimeBody) {
            oldStaticBodyPtr = bc2d->RuntimeBody;
            bc2d->RuntimeBody = nullptr;
            bc2d->RuntimeFixture = nullptr;
        }
        if (cc2d && cc2d->RuntimeBody != nullptr && cc2d->RuntimeBody != rb2d->RuntimeBody) {
            if (!oldStaticBodyPtr) oldStaticBodyPtr = cc2d->RuntimeBody;
            cc2d->RuntimeBody = nullptr;
            cc2d->RuntimeShape = nullptr;
        }
        if (cap2d && cap2d->RuntimeBody != nullptr && cap2d->RuntimeBody != rb2d->RuntimeBody) {
            if (!oldStaticBodyPtr) oldStaticBodyPtr = cap2d->RuntimeBody;
            cap2d->RuntimeBody = nullptr;
            cap2d->RuntimeShape = nullptr;
        }
        if (oldStaticBodyPtr) {
            b2BodyId oldStaticBody = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(oldStaticBodyPtr)));
            if (b2Body_IsValid(oldStaticBody)) {
                b2DestroyBody(oldStaticBody);
            }
        }

        b2BodyId bodyId;
        if (rb2d->RuntimeBody == nullptr) {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            if (rb2d->Type == Rigidbody2D::BodyType::Static) {
                bodyDef.type = b2_staticBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Dynamic) {
                bodyDef.type = b2_dynamicBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Kinematic) {
                bodyDef.type = b2_kinematicBody;
            }

            const glm::vec3 worldPos = transform->GetWorldPosition();
            bodyDef.position = b2Vec2{ worldPos.x, worldPos.z };
            bodyDef.rotation = b2MakeRot(glm::radians(transform->rotation.y));
            bodyDef.fixedRotation = rb2d->FixedRotation;
            bodyDef.linearDamping = rb2d->LinearDamping;
            bodyDef.angularDamping = rb2d->AngularDamping;
            bodyDef.isBullet = rb2d->IsBullet;
            bodyDef.name = go->GetName().c_str();
            bodyDef.userData = go;

            bodyId = b2CreateBody(worldId, &bodyDef);
            rb2d->RuntimeBody = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreBodyId(bodyId)));

            if (rb2d->InitialLinearVelocityXZ.x != 0.0f || rb2d->InitialLinearVelocityXZ.y != 0.0f) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ rb2d->InitialLinearVelocityXZ.x, rb2d->InitialLinearVelocityXZ.y });
            }

            if (!go->IsActiveInHierarchy()) {
                b2Body_Disable(bodyId);
            }
        } else {
            bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetUserData(bodyId, go);
                b2Body_SetFixedRotation(bodyId, rb2d->FixedRotation);
                b2Body_SetLinearDamping(bodyId, rb2d->LinearDamping);
                b2Body_SetAngularDamping(bodyId, rb2d->AngularDamping);
                b2Body_SetBullet(bodyId, rb2d->IsBullet);
            }
        }

        if (bc2d && b2Body_IsValid(bodyId)) {
            if (bc2d->RuntimeFixture != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(bc2d->RuntimeFixture)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != bc2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        bc2d->RuntimeFixture = nullptr;
                    }
                } else {
                    bc2d->RuntimeFixture = nullptr;
                }
            }

            b2Vec2 center = { bc2d->offset.x, bc2d->offset.y };
            b2Rot rot = b2MakeRot(0.0f);
            float halfW = std::max(0.001f, bc2d->size.x * std::abs(transform->scale.x) * 0.5f);
            float halfD = std::max(0.001f, bc2d->size.y * std::abs(transform->scale.z) * 0.5f);
            b2Polygon box = b2MakeOffsetBox(halfW, halfD, center, rot);

            if (bc2d->RuntimeFixture == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = bc2d->density;
                shapeDef.material.friction = bc2d->friction;
                shapeDef.material.restitution = bc2d->restitution;
                shapeDef.isSensor = bc2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = bc2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = bc2d;
                if (bc2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    bc2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = bc2d->CategoryBits;
                shapeDef.filter.maskBits = bc2d->MaskBits;
                shapeDef.filter.groupIndex = bc2d->GroupIndex;

                b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
                bc2d->RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(bc2d->RuntimeFixture)));
                b2Polygon cur = b2Shape_GetPolygon(shapeId);
                if (cur.count != box.count ||
                    std::abs(cur.vertices[0].x - box.vertices[0].x) > 1e-4f ||
                    std::abs(cur.vertices[0].y - box.vertices[0].y) > 1e-4f) {
                    b2Shape_SetPolygon(shapeId, &box);
                }
            }
        }

        if (cc2d && b2Body_IsValid(bodyId)) {
            if (cc2d->RuntimeShape != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cc2d->RuntimeShape)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != cc2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        cc2d->RuntimeShape = nullptr;
                    }
                } else {
                    cc2d->RuntimeShape = nullptr;
                }
            }

            float scaleFactor = std::max(std::abs(transform->scale.x), std::abs(transform->scale.z));
            float effectiveRadius = std::max(0.001f, cc2d->Radius * scaleFactor);
            b2Circle circle;
            circle.center = b2Vec2{ cc2d->OffsetXZ.x, cc2d->OffsetXZ.y };
            circle.radius = effectiveRadius;

            if (cc2d->RuntimeShape == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = cc2d->Density;
                shapeDef.material.friction = cc2d->Friction;
                shapeDef.material.restitution = cc2d->Restitution;
                shapeDef.isSensor = cc2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = cc2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = cc2d;
                if (cc2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    cc2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = cc2d->CategoryBits;
                shapeDef.filter.maskBits = cc2d->MaskBits;
                shapeDef.filter.groupIndex = cc2d->GroupIndex;

                b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
                cc2d->RuntimeShape = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cc2d->RuntimeShape)));
                b2Circle cur = b2Shape_GetCircle(shapeId);
                if (std::abs(cur.radius - effectiveRadius) > 1e-4f ||
                    std::abs(cur.center.x - circle.center.x) > 1e-4f ||
                    std::abs(cur.center.y - circle.center.y) > 1e-4f) {
                    b2Shape_SetCircle(shapeId, &circle);
                }
            }
        }

        if (cap2d && b2Body_IsValid(bodyId)) {
            if (cap2d->RuntimeShape != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cap2d->RuntimeShape)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != cap2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        cap2d->RuntimeShape = nullptr;
                    }
                } else {
                    cap2d->RuntimeShape = nullptr;
                }
            }

            float scaleX = std::abs(transform->scale.x);
            float scaleZ = std::abs(transform->scale.z);
            float scaleFactor = std::max(scaleX, scaleZ);
            b2Capsule capsule;
            capsule.center1 = b2Vec2{ cap2d->Center1.x * scaleX, cap2d->Center1.y * scaleZ };
            capsule.center2 = b2Vec2{ cap2d->Center2.x * scaleX, cap2d->Center2.y * scaleZ };
            capsule.radius = std::max(0.001f, cap2d->Radius * scaleFactor);

            if (cap2d->RuntimeShape == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = cap2d->Density;
                shapeDef.material.friction = cap2d->Friction;
                shapeDef.material.restitution = cap2d->Restitution;
                shapeDef.isSensor = cap2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = cap2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = cap2d;
                if (cap2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    cap2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = cap2d->CategoryBits;
                shapeDef.filter.maskBits = cap2d->MaskBits;
                shapeDef.filter.groupIndex = cap2d->GroupIndex;

                b2ShapeId shapeId = b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
                cap2d->RuntimeShape = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cap2d->RuntimeShape)));
                b2Capsule cur = b2Shape_GetCapsule(shapeId);
                if (std::abs(cur.radius - capsule.radius) > 1e-4f ||
                    std::abs(cur.center1.x - capsule.center1.x) > 1e-4f ||
                    std::abs(cur.center1.y - capsule.center1.y) > 1e-4f ||
                    std::abs(cur.center2.x - capsule.center2.x) > 1e-4f ||
                    std::abs(cur.center2.y - capsule.center2.y) > 1e-4f) {
                    b2Shape_SetCapsule(shapeId, &capsule);
                }
            }
        }

        if (rb2d->MassMultiplier != 1.0f && b2Body_IsValid(bodyId)) {
            b2MassData massData = b2Body_GetMassData(bodyId);
            massData.mass *= rb2d->MassMultiplier;
            b2Body_SetMassData(bodyId, massData);
        }
    } else if (bc2d || cc2d || cap2d) {
        b2BodyId bodyId;
        void* existingBodyPtr = nullptr;
        if (bc2d && bc2d->RuntimeBody) existingBodyPtr = bc2d->RuntimeBody;
        else if (cc2d && cc2d->RuntimeBody) existingBodyPtr = cc2d->RuntimeBody;
        else if (cap2d && cap2d->RuntimeBody) existingBodyPtr = cap2d->RuntimeBody;

        if (existingBodyPtr) {
            bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(existingBodyPtr)));
        } else {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_staticBody;
            const glm::vec3 worldPos = transform->GetWorldPosition();
            bodyDef.position = b2Vec2{ worldPos.x, worldPos.z };
            bodyDef.rotation = b2MakeRot(glm::radians(transform->rotation.y));
            bodyDef.name = go->GetName().c_str();
            bodyDef.userData = go;

            bodyId = b2CreateBody(worldId, &bodyDef);
            if (!go->IsActiveInHierarchy()) {
                b2Body_Disable(bodyId);
            }
        }

        void* storedBodyPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreBodyId(bodyId)));
        if (bc2d) bc2d->RuntimeBody = storedBodyPtr;
        if (cc2d) cc2d->RuntimeBody = storedBodyPtr;
        if (cap2d) cap2d->RuntimeBody = storedBodyPtr;

        if (bc2d && b2Body_IsValid(bodyId)) {
            if (bc2d->RuntimeFixture != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(bc2d->RuntimeFixture)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != bc2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        bc2d->RuntimeFixture = nullptr;
                    }
                } else {
                    bc2d->RuntimeFixture = nullptr;
                }
            }

            b2Vec2 center = { bc2d->offset.x, bc2d->offset.y };
            b2Rot rot = b2MakeRot(0.0f);
            float halfW = std::max(0.001f, bc2d->size.x * std::abs(transform->scale.x) * 0.5f);
            float halfD = std::max(0.001f, bc2d->size.y * std::abs(transform->scale.z) * 0.5f);
            b2Polygon box = b2MakeOffsetBox(halfW, halfD, center, rot);

            if (bc2d->RuntimeFixture == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = bc2d->density;
                shapeDef.material.friction = bc2d->friction;
                shapeDef.material.restitution = bc2d->restitution;
                shapeDef.isSensor = bc2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = bc2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = bc2d;
                if (bc2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    bc2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = bc2d->CategoryBits;
                shapeDef.filter.maskBits = bc2d->MaskBits;
                shapeDef.filter.groupIndex = bc2d->GroupIndex;

                b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
                bc2d->RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(bc2d->RuntimeFixture)));
                b2Polygon cur = b2Shape_GetPolygon(shapeId);
                if (cur.count != box.count ||
                    std::abs(cur.vertices[0].x - box.vertices[0].x) > 1e-4f ||
                    std::abs(cur.vertices[0].y - box.vertices[0].y) > 1e-4f) {
                    b2Shape_SetPolygon(shapeId, &box);
                }
            }
        }

        if (cc2d && b2Body_IsValid(bodyId)) {
            if (cc2d->RuntimeShape != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cc2d->RuntimeShape)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != cc2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        cc2d->RuntimeShape = nullptr;
                    }
                } else {
                    cc2d->RuntimeShape = nullptr;
                }
            }

            float scaleFactor = std::max(std::abs(transform->scale.x), std::abs(transform->scale.z));
            float effectiveRadius = std::max(0.001f, cc2d->Radius * scaleFactor);
            b2Circle circle;
            circle.center = b2Vec2{ cc2d->OffsetXZ.x, cc2d->OffsetXZ.y };
            circle.radius = effectiveRadius;

            if (cc2d->RuntimeShape == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = cc2d->Density;
                shapeDef.material.friction = cc2d->Friction;
                shapeDef.material.restitution = cc2d->Restitution;
                shapeDef.isSensor = cc2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = cc2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = cc2d;
                if (cc2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    cc2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = cc2d->CategoryBits;
                shapeDef.filter.maskBits = cc2d->MaskBits;
                shapeDef.filter.groupIndex = cc2d->GroupIndex;

                b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
                cc2d->RuntimeShape = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cc2d->RuntimeShape)));
                b2Circle cur = b2Shape_GetCircle(shapeId);
                if (std::abs(cur.radius - effectiveRadius) > 1e-4f ||
                    std::abs(cur.center.x - circle.center.x) > 1e-4f ||
                    std::abs(cur.center.y - circle.center.y) > 1e-4f) {
                    b2Shape_SetCircle(shapeId, &circle);
                }
            }
        }

        if (cap2d && b2Body_IsValid(bodyId)) {
            if (cap2d->RuntimeShape != nullptr) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cap2d->RuntimeShape)));
                if (b2Shape_IsValid(shapeId)) {
                    if (b2Shape_IsSensor(shapeId) != cap2d->IsSensor) {
                        b2DestroyShape(shapeId, true);
                        cap2d->RuntimeShape = nullptr;
                    }
                } else {
                    cap2d->RuntimeShape = nullptr;
                }
            }

            float scaleX = std::abs(transform->scale.x);
            float scaleZ = std::abs(transform->scale.z);
            float scaleFactor = std::max(scaleX, scaleZ);
            b2Capsule capsule;
            capsule.center1 = b2Vec2{ cap2d->Center1.x * scaleX, cap2d->Center1.y * scaleZ };
            capsule.center2 = b2Vec2{ cap2d->Center2.x * scaleX, cap2d->Center2.y * scaleZ };
            capsule.radius = std::max(0.001f, cap2d->Radius * scaleFactor);

            if (cap2d->RuntimeShape == nullptr) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = cap2d->Density;
                shapeDef.material.friction = cap2d->Friction;
                shapeDef.material.restitution = cap2d->Restitution;
                shapeDef.isSensor = cap2d->IsSensor;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableHitEvents = cap2d->EnableHitEvents;
                shapeDef.enableContactEvents = true;
                shapeDef.userData = cap2d;
                if (cap2d->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                    cap2d->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
                }
                shapeDef.filter.categoryBits = cap2d->CategoryBits;
                shapeDef.filter.maskBits = cap2d->MaskBits;
                shapeDef.filter.groupIndex = cap2d->GroupIndex;

                b2ShapeId shapeId = b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
                cap2d->RuntimeShape = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreShapeId(shapeId)));
            } else {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cap2d->RuntimeShape)));
                b2Capsule cur = b2Shape_GetCapsule(shapeId);
                if (std::abs(cur.radius - capsule.radius) > 1e-4f ||
                    std::abs(cur.center1.x - capsule.center1.x) > 1e-4f ||
                    std::abs(cur.center1.y - capsule.center1.y) > 1e-4f ||
                    std::abs(cur.center2.x - capsule.center2.x) > 1e-4f ||
                    std::abs(cur.center2.y - capsule.center2.y) > 1e-4f) {
                    b2Shape_SetCapsule(shapeId, &capsule);
                }
            }
        }
    }

    if (tilemap && tilemap->RuntimeBody == nullptr) {
        CreateRuntimeTilemapBody(go);
    }
}

void Scene::UnregisterRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;

    void* destroyedBodyPtr = nullptr;
    if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
        if (rb2d->RuntimeBody) {
            destroyedBodyPtr = rb2d->RuntimeBody;
            b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
            rb2d->RuntimeBody = nullptr;
        }
    }

    void* staticBodyPtr = nullptr;
    if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
        if (bc2d->RuntimeBody && bc2d->RuntimeBody != destroyedBodyPtr) {
            staticBodyPtr = bc2d->RuntimeBody;
        }
        bc2d->RuntimeBody = nullptr;
        bc2d->RuntimeFixture = nullptr;
    }
    if (auto* cc2d = go->GetComponent<CircleCollider2D>()) {
        if (cc2d->RuntimeBody && cc2d->RuntimeBody != destroyedBodyPtr) {
            if (!staticBodyPtr) staticBodyPtr = cc2d->RuntimeBody;
        }
        cc2d->RuntimeBody = nullptr;
        cc2d->RuntimeShape = nullptr;
    }
    if (auto* cap2d = go->GetComponent<CapsuleCollider2D>()) {
        if (cap2d->RuntimeBody && cap2d->RuntimeBody != destroyedBodyPtr) {
            if (!staticBodyPtr) staticBodyPtr = cap2d->RuntimeBody;
        }
        cap2d->RuntimeBody = nullptr;
        cap2d->RuntimeShape = nullptr;
    }

    if (staticBodyPtr) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(staticBodyPtr)));
        if (b2Body_IsValid(bodyId)) {
            b2DestroyBody(bodyId);
        }
    }

    if (auto* tilemap = go->GetComponent<Tilemap>()) {
        if (tilemap->RuntimeBody) {
            b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tilemap->RuntimeBody)));
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
            tilemap->RuntimeBody = nullptr;
        }
    }
}

void Scene::CreateRuntimePhysicsBody(GameObject* go) {
    RegisterRuntimeBody(go);
}

void Scene::CreateRuntimeTilemapBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    auto* tilemap = go->GetComponent<Tilemap>();
    auto* transform = go->GetTransform();
    if (!tilemap || !transform) return;

    if (tilemap->RuntimeBody != nullptr) return;

    const uint32_t width = tilemap->GetWidth();
    const uint32_t height = tilemap->GetHeight();
    if (width == 0 || height == 0) return;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_staticBody;
    bodyDef.position = b2Vec2{ 0.0f, 0.0f };
    bodyDef.name = go->GetName().c_str();
    bodyDef.userData = go;

    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    tilemap->RuntimeBody = reinterpret_cast<void*>(static_cast<uintptr_t>(b2StoreBodyId(bodyId)));

    if (!go->IsActiveInHierarchy()) {
        b2Body_Disable(bodyId);
    }

    const glm::vec3 origin = transform->position;
    const glm::vec3 scale = transform->scale;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.material.friction = 0.5f;
    shapeDef.material.restitution = 0.0f;
    shapeDef.filter.categoryBits = PhysicsLayer::Environment;
    shapeDef.filter.maskBits = PhysicsLayer::All;

    for (uint32_t gz = 0; gz < height; ++gz) {
        for (uint32_t gx = 0; gx < width; ++gx) {
            if (tilemap->GetTile(gx, gz).walkable) {
                continue;
            }

            uint32_t startGx = gx;
            while (gx + 1 < width && !tilemap->GetTile(gx + 1, gz).walkable) {
                ++gx;
            }
            uint32_t endGx = gx;

            const float spanTiles = static_cast<float>(endGx - startGx + 1);
            const float halfW = spanTiles * scale.x * 0.5f;
            const float halfD = 1.0f * scale.z * 0.5f;

            const float centerX = origin.x + 0.5f * static_cast<float>(startGx + endGx) * scale.x;
            const float centerZ = origin.z + static_cast<float>(gz) * scale.z;

            b2Polygon tileBox = b2MakeOffsetBox(halfW, halfD, b2Vec2{ centerX, centerZ }, b2MakeRot(0.0f));
            b2CreatePolygonShape(bodyId, &shapeDef, &tileBox);
        }
    }
}

GameObject* Scene::InstantiatePrefab(const Prefab& prefab, const glm::vec3& worldPos) {
    const auto& rootJson = prefab.GetJson();
    if (!rootJson.contains("Entities") || !rootJson["Entities"].is_array() || rootJson["Entities"].empty()) {
        ENGINE_CORE_ERROR("Scene::InstantiatePrefab: Invalid prefab JSON");
        return nullptr;
    }

    const auto& entitiesArray = rootJson["Entities"];
    std::unordered_map<uint64_t, uint64_t> oldToNewUUID;
    std::unordered_map<uint64_t, GameObject*> createdEntities;
    std::vector<std::pair<GameObject*, uint64_t>> parentLinks;
    GameObject* rootGameObject = nullptr;

    // Pass 1: Instantiate GameObjects with fresh UUIDs and deserialize components
    for (const auto& entityJson : entitiesArray) {
        const uint64_t oldUUID = entityJson.value("UUID", 0ULL);
        const std::string name = entityJson.value("Name", "GameObject");

        GameObject* go = CreateGameObject(name);
        const uint64_t newUUID = go->GetUUID();
        if (oldUUID != 0) {
            oldToNewUUID[oldUUID] = newUUID;
        }
        createdEntities[newUUID] = go;

        if (entityJson.contains("Tag")) {
            if (entityJson["Tag"].is_string()) {
                go->SetTag(StringToEntityTag(entityJson["Tag"].get<std::string>()));
            } else if (entityJson["Tag"].is_number()) {
                go->SetTag(static_cast<EntityTag>(entityJson["Tag"].get<uint16_t>()));
            }
        }
        if (entityJson.contains("LayerMask") && entityJson["LayerMask"].is_number()) {
            go->SetLayer(entityJson["LayerMask"].get<uint32_t>());
        }

        if (entityJson.contains("Components")) {
            SceneSerializer::DeserializeEntityComponents(go, entityJson["Components"], name);
        }

        const uint64_t oldParentUUID = entityJson.value("ParentUUID", 0ULL);
        if (oldParentUUID != 0) {
            parentLinks.emplace_back(go, oldParentUUID);
        } else if (!rootGameObject) {
            rootGameObject = go;
        }
    }

    if (!rootGameObject && !createdEntities.empty()) {
        rootGameObject = createdEntities.begin()->second;
    }

    // Pass 2: Position root entity at requested world position
    if (rootGameObject) {
        if (auto* tr = rootGameObject->GetTransform()) {
            tr->position = worldPos;
            tr->SetDirty();
        }
    }

    // Pass 3: Re-link parent-child hierarchy using remapped UUIDs
    for (const auto& [childGo, oldParentUUID] : parentLinks) {
        auto mapIt = oldToNewUUID.find(oldParentUUID);
        if (mapIt != oldToNewUUID.end()) {
            const uint64_t newParentUUID = mapIt->second;
            auto parentGoIt = createdEntities.find(newParentUUID);
            if (parentGoIt != createdEntities.end() && parentGoIt->second) {
                childGo->GetTransform()->SetParent(parentGoIt->second->GetTransform(), /*keepWorldTransform=*/false);
            }
        }
    }

    // Pass 4: Create runtime physics bodies if runtime is active
    if (m_IsRuntimeRunning) {
        for (const auto& [uuid, go] : createdEntities) {
            RegisterRuntimeBody(go);
        }
    }

    return rootGameObject;
}

void Scene::PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count) {
    auto& pool = m_EntityPools[poolKey];
    pool.freeList.reserve(pool.freeList.size() + count);
    pool.allInstances.reserve(pool.allInstances.size() + count);

    for (uint32_t i = 0; i < count; ++i) {
        GameObject* instance = InstantiatePrefab(prefab, glm::vec3(0.0f));
        if (instance) {
            instance->SetPoolKey(poolKey);
            instance->SetActive(false);
            pool.freeList.push_back(instance);
            pool.allInstances.push_back(instance);
        }
    }
}

GameObject* Scene::AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos) {
    auto& pool = m_EntityPools[poolKey];
    GameObject* instance = nullptr;

    while (!pool.freeList.empty()) {
        GameObject* candidate = pool.freeList.back();
        pool.freeList.pop_back();
        if (candidate && !candidate->IsPendingDestroy()) {
            instance = candidate;
            break;
        }
    }

    if (!instance) {
        // Pool exhausted or empty: dynamically allocate fresh instance
        instance = InstantiatePrefab(prefab, worldPos);
        if (instance) {
            instance->SetPoolKey(poolKey);
            pool.allInstances.push_back(instance);
        }
    } else {
        // Reposition existing pooled instance
        if (auto* transform = instance->GetTransform()) {
            transform->SetWorldPosition(worldPos);
            transform->SnapshotPrevious();
        }
    }

    if (instance) {
        // Reset Box2D physics if in runtime
        if (m_IsRuntimeRunning) {
            if (auto* rb2d = instance->GetComponent<Rigidbody2D>()) {
                if (rb2d->RuntimeBody) {
                    b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody));
                    if (b2Body_IsValid(bodyId)) {
                        b2Body_Enable(bodyId);
                        b2Vec2 p{ worldPos.x, worldPos.z };
                        b2Rot r = b2MakeRot(glm::radians(instance->GetTransform()->rotation.y));
                        b2Body_SetTransform(bodyId, p, r);
                        b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                        b2Body_SetAngularVelocity(bodyId, 0.0f);
                        b2Body_SetAwake(bodyId, true);
                    }
                }
                rb2d->VerticalVelocity = 0.0f;
            }
            if (auto* bc2d = instance->GetComponent<BoxCollider2D>()) {
                if (bc2d->RuntimeBody) {
                    b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(bc2d->RuntimeBody));
                    if (b2Body_IsValid(bodyId)) {
                        b2Body_Enable(bodyId);
                        b2Vec2 p{ worldPos.x, worldPos.z };
                        b2Rot r = b2MakeRot(glm::radians(instance->GetTransform()->rotation.y));
                        b2Body_SetTransform(bodyId, p, r);
                    }
                }
                bc2d->verticalVelocity = 0.0f;
            }
            if (auto* cc2d = instance->GetComponent<CircleCollider2D>()) {
                if (cc2d->RuntimeBody) {
                    b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(cc2d->RuntimeBody));
                    if (b2Body_IsValid(bodyId)) {
                        b2Body_Enable(bodyId);
                        b2Vec2 p{ worldPos.x, worldPos.z };
                        b2Rot r = b2MakeRot(glm::radians(instance->GetTransform()->rotation.y));
                        b2Body_SetTransform(bodyId, p, r);
                    }
                }
            }
            if (auto* cap2d = instance->GetComponent<CapsuleCollider2D>()) {
                if (cap2d->RuntimeBody) {
                    b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(cap2d->RuntimeBody));
                    if (b2Body_IsValid(bodyId)) {
                        b2Body_Enable(bodyId);
                        b2Vec2 p{ worldPos.x, worldPos.z };
                        b2Rot r = b2MakeRot(glm::radians(instance->GetTransform()->rotation.y));
                        b2Body_SetTransform(bodyId, p, r);
                    }
                }
            }
        }
        if (auto* rb2d = instance->GetComponent<Rigidbody2D>()) {
            rb2d->VerticalVelocity = 0.0f;
        }
        if (auto* bc2d = instance->GetComponent<BoxCollider2D>()) {
            bc2d->verticalVelocity = 0.0f;
        }

        instance->SetActive(true);
    }

    return instance;
}

void Scene::ReleaseToPool(const std::string& poolKey, GameObject* instance) {
    if (!instance) return;

    // Reset physics state
    if (auto* rb2d = instance->GetComponent<Rigidbody2D>()) {
        if (m_IsRuntimeRunning && rb2d->RuntimeBody) {
            b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody));
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                b2Body_SetAngularVelocity(bodyId, 0.0f);
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
        rb2d->VerticalVelocity = 0.0f;
    }
    if (auto* bc2d = instance->GetComponent<BoxCollider2D>()) {
        if (m_IsRuntimeRunning && bc2d->RuntimeBody) {
            b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(bc2d->RuntimeBody));
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
        bc2d->verticalVelocity = 0.0f;
    }
    if (auto* cc2d = instance->GetComponent<CircleCollider2D>()) {
        if (m_IsRuntimeRunning && cc2d->RuntimeBody) {
            b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(cc2d->RuntimeBody));
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
    }
    if (auto* cap2d = instance->GetComponent<CapsuleCollider2D>()) {
        if (m_IsRuntimeRunning && cap2d->RuntimeBody) {
            b2BodyId bodyId = b2LoadBodyId(reinterpret_cast<uintptr_t>(cap2d->RuntimeBody));
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
    }

    // Reset particles on root and children
    if (auto* ps = instance->GetComponent<ParticleSystem>()) {
        ps->Reset();
    }
    if (auto* tr = instance->GetTransform()) {
        for (auto* childTr : tr->GetChildren()) {
            if (childTr && childTr->GetGameObject()) {
                if (auto* childPs = childTr->GetGameObject()->GetComponent<ParticleSystem>()) {
                    childPs->Reset();
                }
            }
        }
    }

    // Deactivate hierarchy (propagates IsActiveInHierarchy() == false to all children)
    instance->SetActive(false);

    // Push back to free-list if not already present
    auto& pool = m_EntityPools[poolKey];
    auto it = std::find(pool.freeList.begin(), pool.freeList.end(), instance);
    if (it == pool.freeList.end()) {
        pool.freeList.push_back(instance);
    }
}

void Scene::ReleaseToPool(GameObject* instance) {
    if (instance && instance->IsPooled()) {
        ReleaseToPool(instance->GetPoolKey(), instance);
    }
}

size_t Scene::GetPoolFreeCount(const std::string& poolKey) const noexcept {
    auto it = m_EntityPools.find(poolKey);
    if (it == m_EntityPools.end()) return 0;
    return it->second.freeList.size();
}

size_t Scene::GetPoolTotalCount(const std::string& poolKey) const noexcept {
    auto it = m_EntityPools.find(poolKey);
    if (it == m_EntityPools.end()) return 0;
    return it->second.allInstances.size();
}

void Scene::ClearPool(const std::string& poolKey) {
    auto it = m_EntityPools.find(poolKey);
    if (it == m_EntityPools.end()) return;

    for (auto* instance : it->second.allInstances) {
        if (instance && !instance->IsPendingDestroy()) {
            DestroyGameObject(instance);
        }
    }
    m_EntityPools.erase(it);
}

void Scene::ClearAllPools() {
    for (auto& [key, pool] : m_EntityPools) {
        for (auto* instance : pool.allInstances) {
            if (instance && !instance->IsPendingDestroy()) {
                DestroyGameObject(instance);
            }
        }
    }
    m_EntityPools.clear();
}

GameObject* Scene::FindGameObjectByUUID(uint64_t uuid) const noexcept {
    auto it = m_EntityUUIDMap.find(uuid);
    if (it != m_EntityUUIDMap.end() && !it->second->IsPendingDestroy()) {
        return it->second;
    }
    return nullptr;
}

void Scene::FindGameObjectsWithTag(EntityTag tag, std::vector<GameObject*>& out) const {
    out.clear();
    for (const auto& go : m_GameObjects) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->GetTag() == tag) {
            out.push_back(go.get());
        }
    }
    for (const auto& go : m_PendingSpawnQueue) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->GetTag() == tag) {
            out.push_back(go.get());
        }
    }
}

std::vector<GameObject*> Scene::FindGameObjectsWithTag(EntityTag tag) const {
    std::vector<GameObject*> result;
    FindGameObjectsWithTag(tag, result);
    return result;
}

GameObject* Scene::FindFirstGameObjectWithTag(EntityTag tag) const noexcept {
    for (const auto& go : m_GameObjects) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->GetTag() == tag) {
            return go.get();
        }
    }
    for (const auto& go : m_PendingSpawnQueue) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->GetTag() == tag) {
            return go.get();
        }
    }
    return nullptr;
}

void Scene::FindGameObjectsInLayer(uint32_t layerMask, std::vector<GameObject*>& out) const {
    out.clear();
    for (const auto& go : m_GameObjects) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->IsInLayer(layerMask)) {
            out.push_back(go.get());
        }
    }
    for (const auto& go : m_PendingSpawnQueue) {
        if (go->IsActiveInHierarchy() && !go->IsPendingDestroy() && go->IsInLayer(layerMask)) {
            out.push_back(go.get());
        }
    }
}

std::vector<GameObject*> Scene::FindGameObjectsInLayer(uint32_t layerMask) const {
    std::vector<GameObject*> result;
    FindGameObjectsInLayer(layerMask, result);
    return result;
}

void Scene::FlushEntityLifecycleQueues() {
    // 1. Move pending spawns into m_GameObjects
    if (!m_PendingSpawnQueue.empty()) {
        for (auto& pending : m_PendingSpawnQueue) {
            if (pending->IsPendingDestroy()) {
                for (auto& comp : pending->m_Components) {
                    comp->OnDestroy();
                }
                m_EntityUUIDMap.erase(pending->GetUUID());
            } else {
                pending->ProcessPendingStarts();
                if (m_IsRuntimeRunning) {
                    RegisterRuntimeBody(pending.get());
                }
                m_GameObjects.push_back(std::move(pending));
                m_StaticCollidersDirty = true;
            }
        }
        m_PendingSpawnQueue.clear();
    }

    // 2. Process pending destroys
    if (!m_PendingDestroyQueue.empty()) {
        bool colliderDestroyed = false;

        for (auto* go : m_PendingDestroyQueue) {
            if (!go) continue;

            // Destroy runtime Box2D body if active
            if (m_IsRuntimeRunning) {
                UnregisterRuntimeBody(go);
            }

            if (go->GetComponent<BoxCollider2D>() || go->GetComponent<CircleCollider2D>() || go->GetComponent<CapsuleCollider2D>() || go->GetComponent<Tilemap>() || go->GetComponent<TerrainComponent>()) {
                colliderDestroyed = true;
            }

            // Invoke OnDestroy() on all components
            for (auto& comp : go->m_Components) {
                comp->OnDestroy();
            }

            m_EntityUUIDMap.erase(go->GetUUID());
        }

        // Erase destroyed GameObjects from m_GameObjects in a single erase_if pass
        std::erase_if(m_GameObjects, [](const std::unique_ptr<GameObject>& ptr) {
            return ptr->IsPendingDestroy();
        });

        m_PendingDestroyQueue.clear();

        if (colliderDestroyed) {
            m_StaticCollidersDirty = true;
        }
    }
}

void Scene::Update(float deltaTime) {
    ENGINE_CORE_TRACE("Scene '{}': Update (deltaTime: {:.4f}s, {} GameObjects)", m_Name, deltaTime, m_GameObjects.size());

    m_IsIteratingEntities = true;
    for (size_t i = 0; i < m_GameObjects.size(); ++i) {
        auto& gameObject = m_GameObjects[i];
        if (gameObject->IsActiveInHierarchy()) {
            gameObject->Update(deltaTime);
        }
    }
    m_IsIteratingEntities = false;

    FlushEntityLifecycleQueues();

    if (!m_IsRuntimeRunning) {
        UpdateCollidersAndGravity(deltaTime);
        ResolveEntityCollisions();
        ResolveTilemapCollisions();
    }
}

void Scene::UpdateCollidersAndGravity(float deltaTime) {
    m_DynamicColliders.clear();
    m_TilemapScratch.clear();

    size_t staticCount = 0;
    for (auto& gameObject : m_GameObjects) {
        if (!gameObject->IsActiveInHierarchy()) {
            continue;
        }
        if (auto* collider = gameObject->GetComponent<BoxCollider2D>()) {
            auto* transform = gameObject->GetTransform();
            if (collider->isDynamic) {
                m_DynamicColliders.push_back({ gameObject.get(), transform, collider });
                // Apply vertical isometric gravity & ground clamping for dynamic bodies
                if (collider->useGravity && transform) {
                    collider->verticalVelocity += collider->gravity * deltaTime;
                    transform->position.y += collider->verticalVelocity * deltaTime;
                    if (transform->position.y <= collider->groundLevel) {
                        transform->position.y = collider->groundLevel;
                        collider->verticalVelocity = 0.0f;
                    }
                }
            } else {
                ++staticCount;
            }
        }
        if (gameObject->GetComponent<Tilemap>()) {
            m_TilemapScratch.push_back(gameObject.get());
        }
    }

    if (staticCount != m_StaticColliders.size()) {
        m_StaticCollidersDirty = true;
    }

    if (m_StaticCollidersDirty) {
        m_StaticColliders.clear();
        m_StaticGrid.clear();

        for (auto& gameObject : m_GameObjects) {
            if (auto* collider = gameObject->GetComponent<BoxCollider2D>()) {
                if (!collider->isDynamic) {
                    m_StaticColliders.push_back({ gameObject.get(), gameObject->GetTransform(), collider });
                }
            }
        }

        m_StaticTestedStamp.assign(m_StaticColliders.size(), 0);
        m_SpatialQueryStamp = 0;

        for (size_t i = 0; i < m_StaticColliders.size(); ++i) {
            const auto& entry = m_StaticColliders[i];
            auto* t = entry.transform;
            auto* c = entry.collider;
            if (!t || !c) continue;

            float sLeft  = t->position.x + c->offset.x - (c->size.x * t->scale.x) * 0.5f;
            float sRight = t->position.x + c->offset.x + (c->size.x * t->scale.x) * 0.5f;
            float sBack  = t->position.z + c->offset.y - (c->size.y * t->scale.z) * 0.5f;
            float sFront = t->position.z + c->offset.y + (c->size.y * t->scale.z) * 0.5f;

            if (sLeft > sRight) std::swap(sLeft, sRight);
            if (sBack > sFront) std::swap(sBack, sFront);

            const int32_t minCX = static_cast<int32_t>(std::floor(sLeft / kSpatialCellSize));
            const int32_t maxCX = static_cast<int32_t>(std::floor(sRight / kSpatialCellSize));
            const int32_t minCZ = static_cast<int32_t>(std::floor(sBack / kSpatialCellSize));
            const int32_t maxCZ = static_cast<int32_t>(std::floor(sFront / kSpatialCellSize));

            for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
                for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                    m_StaticGrid[GetCellKey(cx, cz)].push_back(static_cast<uint32_t>(i));
                }
            }
        }

        m_StaticCollidersDirty = false;
    }
}

void Scene::ResolveEntityCollisions() {
    auto resolveCollisionPair = [&](const ColliderCacheEntry& entryA, const ColliderCacheEntry& entryB) {
        auto* cA = entryA.collider;
        auto* cB = entryB.collider;
        auto* tA = entryA.transform;
        auto* tB = entryB.transform;
        if (!tA || !tB) return;

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

        if (aLeft > aRight) std::swap(aLeft, aRight);
        if (aBack > aFront) std::swap(aBack, aFront);
        if (bLeft > bRight) std::swap(bLeft, bRight);
        if (bBack > bFront) std::swap(bBack, bFront);

        float overlapX = std::min(aRight, bRight) - std::max(aLeft, bLeft);
        float overlapZ = std::min(aFront, bFront) - std::max(aBack, bBack);

        if (overlapX > 0.0f && overlapZ > 0.0f) {
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
    };

    // 1. Dynamic vs Dynamic
    for (size_t i = 0; i < m_DynamicColliders.size(); i++) {
        for (size_t j = i + 1; j < m_DynamicColliders.size(); j++) {
            resolveCollisionPair(m_DynamicColliders[i], m_DynamicColliders[j]);
        }
    }

    // 2. Dynamic vs Static (using 2D uniform spatial hash grid)
    if (!m_StaticColliders.empty()) {
        for (const auto& dynEntry : m_DynamicColliders) {
            auto* tA = dynEntry.transform;
            auto* cA = dynEntry.collider;
            if (!tA || !cA) continue;

            float aLeft  = tA->position.x + cA->offset.x - (cA->size.x * tA->scale.x) / 2.0f;
            float aRight = tA->position.x + cA->offset.x + (cA->size.x * tA->scale.x) / 2.0f;
            float aBack  = tA->position.z + cA->offset.y - (cA->size.y * tA->scale.z) / 2.0f;
            float aFront = tA->position.z + cA->offset.y + (cA->size.y * tA->scale.z) / 2.0f;

            if (aLeft > aRight) std::swap(aLeft, aRight);
            if (aBack > aFront) std::swap(aBack, aFront);

            const int32_t minCX = static_cast<int32_t>(std::floor(aLeft / kSpatialCellSize));
            const int32_t maxCX = static_cast<int32_t>(std::floor(aRight / kSpatialCellSize));
            const int32_t minCZ = static_cast<int32_t>(std::floor(aBack / kSpatialCellSize));
            const int32_t maxCZ = static_cast<int32_t>(std::floor(aFront / kSpatialCellSize));

            ++m_SpatialQueryStamp;
            if (m_SpatialQueryStamp == 0) {
                m_StaticTestedStamp.assign(m_StaticColliders.size(), 0);
                m_SpatialQueryStamp = 1;
            }

            for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
                for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                    auto it = m_StaticGrid.find(GetCellKey(cx, cz));
                    if (it != m_StaticGrid.end()) {
                        for (uint32_t staticIdx : it->second) {
                            if (staticIdx < m_StaticColliders.size() && m_StaticTestedStamp[staticIdx] != m_SpatialQueryStamp) {
                                m_StaticTestedStamp[staticIdx] = m_SpatialQueryStamp;
                                resolveCollisionPair(dynEntry, m_StaticColliders[staticIdx]);
                            }
                        }
                    }
                }
            }
        }
    }
}

void Scene::ResolveTilemapCollisions() {
    // 2. Dynamic Entity-vs-Tilemap Non-Walkable Tile Collision on XZ Ground Plane
    for (const auto& entryA : m_DynamicColliders) {
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

void* Scene::Box2DEnqueueTask(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext) {
    if (!task || itemCount <= 0) {
        return nullptr;
    }

    auto* scene = static_cast<Scene*>(userContext);
    const uint32_t workers = JobSystem::GetWorkerCount();

    if (minRange <= 0) {
        minRange = 1;
    }

    const int maxChunks = itemCount / minRange;
    const int availableThreads = static_cast<int>(workers + 1);
    const int chunkCount = std::min(availableThreads, maxChunks);

    // If chunkCount <= 1 or JobSystem is not initialized with workers, execute serially on calling thread
    if (chunkCount <= 1 || !JobSystem::IsInitialized() || workers == 0 || !scene) {
        const int32_t currentWorker = JobSystem::GetCurrentWorkerIndex();
        const uint32_t callerWorker = (currentWorker >= 0 && currentWorker < static_cast<int32_t>(workers))
                                          ? static_cast<uint32_t>(currentWorker)
                                          : workers;
        task(0, itemCount, callerWorker, taskContext);
        if (scene) {
            ++scene->m_Box2DSerialTasksExecuted;
        }
        return nullptr;
    }

    // Acquire an idle task slot from the preallocated pool (zero dynamic allocation)
    Box2DTask* taskSlot = nullptr;
    for (auto& slot : scene->m_Box2DTasks) {
        bool expected = false;
        if (slot.inUse.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            taskSlot = &slot;
            break;
        }
    }

    // If all preallocated slots are occupied, fallback to executing serially
    if (!taskSlot) {
        const int32_t currentWorker = JobSystem::GetCurrentWorkerIndex();
        const uint32_t callerWorker = (currentWorker >= 0 && currentWorker < static_cast<int32_t>(workers))
                                          ? static_cast<uint32_t>(currentWorker)
                                          : workers;
        task(0, itemCount, callerWorker, taskContext);
        ++scene->m_Box2DSerialTasksExecuted;
        return nullptr;
    }

    taskSlot->remaining.store(chunkCount, std::memory_order_release);
    taskSlot->done.store(false, std::memory_order_release);
    ++scene->m_Box2DParallelTasksDispatched;

    const int baseSize = itemCount / chunkCount;
    const int remainder = itemCount % chunkCount;

    for (int c = 0; c < chunkCount; ++c) {
        const int start = c * baseSize + std::min(c, remainder);
        const int end = start + baseSize + (c < remainder ? 1 : 0);

        JobSystem::Dispatch([task, start, end, taskContext, taskSlot, workers]() {
            const int32_t currentWorker = JobSystem::GetCurrentWorkerIndex();
            const uint32_t wIdx = (currentWorker >= 0 && currentWorker < static_cast<int32_t>(workers))
                                      ? static_cast<uint32_t>(currentWorker)
                                      : workers;
            task(start, end, wIdx, taskContext);

            if (taskSlot->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                taskSlot->done.store(true, std::memory_order_release);
                taskSlot->done.notify_all();
            }
        });
    }

    return taskSlot;
}

void Scene::Box2DFinishTask(void* userTask, void* userContext) {
    if (!userTask) {
        return;
    }

    (void)userContext;
    auto* taskSlot = static_cast<Box2DTask*>(userTask);

    while (!taskSlot->done.load(std::memory_order_acquire)) {
        if (!JobSystem::ExecuteOneJob()) {
            taskSlot->done.wait(false, std::memory_order_acquire);
        }
    }

    taskSlot->inUse.store(false, std::memory_order_release);
}

void Scene::OnRuntimeStart() {
    if (m_IsRuntimeRunning) {
        ENGINE_CORE_WARN("Scene '{}': OnRuntimeStart called while already running", m_Name);
        return;
    }

    ENGINE_CORE_INFO("Scene '{}': Starting Box2D runtime physics", m_Name);

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{ 0.0f, 0.0f };

    const uint32_t workers = JobSystem::GetWorkerCount();
    if (JobSystem::IsInitialized() && workers > 0) {
        worldDef.workerCount = static_cast<int>(workers + 1);
        worldDef.enqueueTask = &Scene::Box2DEnqueueTask;
        worldDef.finishTask = &Scene::Box2DFinishTask;
        worldDef.userTaskContext = this;
        ENGINE_CORE_INFO("Scene '{}': Enabling Box2D v3 worker multithreading with {} workers", m_Name, worldDef.workerCount);
    }

    b2WorldId worldId = b2CreateWorld(&worldDef);
    m_PhysicsWorldId = b2StoreWorldId(worldId);
    m_IsRuntimeRunning = true;
    ResetPhysicsAccumulator();

    for (auto& go : m_GameObjects) {
        RegisterRuntimeBody(go.get());
    }
    for (auto& go : m_GameObjects) {
        if (auto* transform = go->GetTransform()) {
            transform->SnapshotPrevious();
        }
    }
    m_StaticCollidersDirty = true;
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
    ResetPhysicsAccumulator();

    for (auto& slot : m_Box2DTasks) {
        slot.inUse.store(false, std::memory_order_relaxed);
        slot.done.store(true, std::memory_order_relaxed);
        slot.remaining.store(0, std::memory_order_relaxed);
    }

    for (auto& go : m_GameObjects) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->RuntimeBody = nullptr;
        }
        if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
            bc2d->RuntimeFixture = nullptr;
            bc2d->RuntimeBody = nullptr;
        }
        if (auto* cc2d = go->GetComponent<CircleCollider2D>()) {
            cc2d->RuntimeShape = nullptr;
            cc2d->RuntimeBody = nullptr;
        }
        if (auto* cap2d = go->GetComponent<CapsuleCollider2D>()) {
            cap2d->RuntimeShape = nullptr;
            cap2d->RuntimeBody = nullptr;
        }
        if (auto* tilemap = go->GetComponent<Tilemap>()) {
            tilemap->RuntimeBody = nullptr;
        }
    }
    for (auto& go : m_PendingSpawnQueue) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->RuntimeBody = nullptr;
        }
        if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
            bc2d->RuntimeFixture = nullptr;
            bc2d->RuntimeBody = nullptr;
        }
        if (auto* cc2d = go->GetComponent<CircleCollider2D>()) {
            cc2d->RuntimeShape = nullptr;
            cc2d->RuntimeBody = nullptr;
        }
        if (auto* cap2d = go->GetComponent<CapsuleCollider2D>()) {
            cap2d->RuntimeShape = nullptr;
            cap2d->RuntimeBody = nullptr;
        }
        if (auto* tilemap = go->GetComponent<Tilemap>()) {
            tilemap->RuntimeBody = nullptr;
        }
    }
    m_StaticCollidersDirty = true;
}

void Scene::OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    PhysicsQueries::OverlapCircleXZ(*this, centerXZ, radius, maskBits, outHits);
}

void Scene::OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    PhysicsQueries::OverlapArcXZ(*this, originXZ, forwardXZ, radius, halfAngleDegrees, maskBits, outHits);
}

bool Scene::RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const {
    return PhysicsQueries::RaycastClosestXZ(*this, startXZ, endXZ, maskBits, outHit);
}

void Scene::UpdateRuntime(float deltaTime) {
    if (m_IsRuntimeRunning) {
        for (auto& go : m_GameObjects) {
            if (go->IsActiveInHierarchy()) {
                RegisterRuntimeBody(go.get());
            }
        }

        const float fixedDt = std::max(0.001f, Time::GetFixedDeltaTime());
        constexpr uint32_t kMaxFixedSteps = 6;
        const float maxAccumulation = static_cast<float>(kMaxFixedSteps) * fixedDt;

        m_PhysicsAccumulator += deltaTime;
        if (m_PhysicsAccumulator > maxAccumulation) {
            m_PhysicsAccumulator = maxAccumulation;
        }

        b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);

        while (m_PhysicsAccumulator >= fixedDt) {
            // Snapshot previous positions for sub-frame visual interpolation
            for (auto& go : m_GameObjects) {
                if (!go->IsActiveInHierarchy()) continue;
                if (auto* transform = go->GetTransform()) {
                    transform->SnapshotPrevious();
                }
            }

            // Fixed update dispatch on active components
            for (size_t i = 0; i < m_GameObjects.size(); ++i) {
                auto& go = m_GameObjects[i];
                if (go->IsActiveInHierarchy()) {
                    go->FixedUpdate(fixedDt);
                }
            }

            // Synchronize airborne pass-through filter state before physics simulation step
            for (auto& go : m_GameObjects) {
                if (!go->IsActiveInHierarchy()) continue;
                auto* rb2d = go->GetComponent<Rigidbody2D>();
                if (!rb2d || rb2d->Type != Rigidbody2D::BodyType::Dynamic || !rb2d->EnableAirbornePassThrough) continue;
                auto* t = go->GetTransform();
                if (!t) continue;

                const bool shouldBeAirborne = (t->position.y > rb2d->AirborneClearanceHeight);
                const bool isTouchdown = (t->position.y <= rb2d->GroundLevelY);

                if (shouldBeAirborne && !rb2d->IsAirbornePassThroughActive()) {
                    rb2d->SetAirbornePassThroughActive(true);
                    ApplyAirbornePassThroughFilter(go.get(), true);
                } else if (isTouchdown && rb2d->IsAirbornePassThroughActive()) {
                    rb2d->SetAirbornePassThroughActive(false);
                    ApplyAirbornePassThroughFilter(go.get(), false);
                }
            }

            // Step Box2D simulation
            if (b2World_IsValid(worldId)) {
                b2World_Step(worldId, fixedDt, 4);
                ProcessPhysicsEvents();
            }

            // Sync Box2D physics simulation results back to Engine Transform
            for (auto& go : m_GameObjects) {
                if (!go->IsActiveInHierarchy()) continue;
                auto* transform = go->GetTransform();
                auto* rb2d = go->GetComponent<Rigidbody2D>();

                if (rb2d && transform && rb2d->RuntimeBody) {
                    b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
                    if (b2Body_IsValid(bodyId)) {
                        b2Vec2 pos = b2Body_GetPosition(bodyId);
                        b2Rot rot = b2Body_GetRotation(bodyId);
                        transform->position.x = pos.x;
                        transform->position.z = pos.y;
                        if (!rb2d->FixedRotation) {
                            transform->rotation.y = glm::degrees(b2Rot_GetAngle(rot));
                        }
                    }
                }
            }

            // Integrate 2.5D vertical elevation and ground bounces
            IntegrateVerticalElevation(fixedDt);

            m_PhysicsAccumulator -= fixedDt;
        }

        m_PhysicsAlpha = (fixedDt > 0.0f) ? (m_PhysicsAccumulator / fixedDt) : 0.0f;
    } else {
        m_PhysicsAccumulator = 0.0f;
        m_PhysicsAlpha = 0.0f;
    }

    Update(deltaTime);
}

void Scene::IntegrateVerticalElevation(float fixedDt) {
    for (auto& go : m_GameObjects) {
        if (!go->IsActiveInHierarchy()) continue;
        auto* transform = go->GetTransform();
        if (!transform) continue;

        auto* rb2d = go->GetComponent<Rigidbody2D>();
        auto* bc2d = go->GetComponent<BoxCollider2D>();

        if (rb2d && rb2d->Type == Rigidbody2D::BodyType::Dynamic && rb2d->UseGravityY) {
            if (rb2d->IsStasisFrozen()) {
                continue;
            }
            rb2d->VerticalVelocity += (rb2d->GravityY * rb2d->GravityScaleY) * fixedDt;
            transform->position.y += rb2d->VerticalVelocity * fixedDt;

            if (transform->position.y <= rb2d->GroundLevelY && rb2d->VerticalVelocity < 0.0f) {
                if (rb2d->GroundRestitutionY > 0.05f && std::abs(rb2d->VerticalVelocity) > 0.8f) {
                    transform->position.y = rb2d->GroundLevelY;
                    rb2d->VerticalVelocity = -rb2d->VerticalVelocity * rb2d->GroundRestitutionY;
                } else {
                    transform->position.y = rb2d->GroundLevelY;
                    rb2d->VerticalVelocity = 0.0f;
                }
            }
            if (bc2d) {
                bc2d->verticalVelocity = rb2d->VerticalVelocity;
            }

            // Airborne Elevation Filtering
            if (rb2d->EnableAirbornePassThrough) {
                const bool shouldBeAirborne = (transform->position.y > rb2d->AirborneClearanceHeight);
                const bool isTouchdown = (transform->position.y <= rb2d->GroundLevelY);

                if (shouldBeAirborne && !rb2d->IsAirbornePassThroughActive()) {
                    rb2d->SetAirbornePassThroughActive(true);
                    ApplyAirbornePassThroughFilter(go.get(), true);
                } else if (isTouchdown && rb2d->IsAirbornePassThroughActive()) {
                    rb2d->SetAirbornePassThroughActive(false);
                    ApplyAirbornePassThroughFilter(go.get(), false);
                }
            }
        } else if (!rb2d && bc2d && bc2d->isDynamic && bc2d->useGravity) {
            bc2d->verticalVelocity += bc2d->gravity * fixedDt;
            transform->position.y += bc2d->verticalVelocity * fixedDt;

            if (transform->position.y <= bc2d->groundLevel && bc2d->verticalVelocity < 0.0f) {
                if (bc2d->restitution > 0.05f && std::abs(bc2d->verticalVelocity) > 0.8f) {
                    transform->position.y = bc2d->groundLevel;
                    bc2d->verticalVelocity = -bc2d->verticalVelocity * bc2d->restitution;
                } else {
                    transform->position.y = bc2d->groundLevel;
                    bc2d->verticalVelocity = 0.0f;
                }
            }
        }
    }
}

void Scene::ApplyAirbornePassThroughFilter(GameObject* go, bool airborne) {
    if (!go) return;
    auto* rb2d = go->GetComponent<Rigidbody2D>();
    const uint64_t excludedMask = rb2d ? rb2d->AirbornePassThroughMask : PhysicsLayer::DefaultAirborneExcluded;

    auto updateShape = [airborne, excludedMask](void* runtimeShapeHandle, uint64_t originalMask) {
        if (!runtimeShapeHandle) return;
        b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(runtimeShapeHandle)));
        if (b2Shape_IsValid(shapeId)) {
            b2Filter filter = b2Shape_GetFilter(shapeId);
            filter.maskBits = airborne ? (originalMask & ~excludedMask) : originalMask;
            b2Shape_SetFilter(shapeId, filter);
        }
    };

    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        updateShape(bc->RuntimeFixture, bc->MaskBits);
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        updateShape(cc->RuntimeShape, cc->MaskBits);
    }
    if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
        updateShape(cap->RuntimeShape, cap->MaskBits);
    }
}

void Scene::ProcessPhysicsEvents() {
    if (!m_IsRuntimeRunning) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    b2ContactEvents contacts = b2World_GetContactEvents(worldId);
    b2SensorEvents sensors = b2World_GetSensorEvents(worldId);

    // Guard entity lifecycle so DestroyGameObject calls during event dispatch defer to lifecycle queues
    m_IsIteratingEntities = true;

    struct PairHash {
        size_t operator()(const std::pair<uint64_t, uint64_t>& p) const noexcept {
            return std::hash<uint64_t>{}(p.first) ^ (std::hash<uint64_t>{}(p.second) + 0x9e3779b97f4a7c15ULL + (std::hash<uint64_t>{}(p.first) << 6) + (std::hash<uint64_t>{}(p.first) >> 2));
        }
    };
    std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash> contactedPairs;

    auto makePair = [](b2ShapeId a, b2ShapeId b) {
        uint64_t idA = b2StoreShapeId(a);
        uint64_t idB = b2StoreShapeId(b);
        if (idA > idB) std::swap(idA, idB);
        return std::make_pair(idA, idB);
    };

    // 1. Process Contact Hit Events
    for (int i = 0; i < contacts.hitCount; ++i) {
        const b2ContactHitEvent& hit = contacts.hitEvents[i];
        if (!b2Shape_IsValid(hit.shapeIdA) || !b2Shape_IsValid(hit.shapeIdB)) continue;

        b2BodyId bodyA = b2Shape_GetBody(hit.shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(hit.shapeIdB);
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB)) continue;

        auto* goA = static_cast<GameObject*>(b2Body_GetUserData(bodyA));
        auto* goB = static_cast<GameObject*>(b2Body_GetUserData(bodyB));
        if (!goA || !goB || goA == goB) continue;
        if (goA->IsPendingDestroy() || goB->IsPendingDestroy()) continue;

        contactedPairs.insert(makePair(hit.shapeIdA, hit.shapeIdB));

        const b2BodyType typeA = b2Body_GetType(bodyA);
        const b2BodyType typeB = b2Body_GetType(bodyB);
        const float mA = b2Body_GetMass(bodyA);
        const float mB = b2Body_GetMass(bodyB);

        float reducedMass = 0.0f;
        if (typeA == b2_dynamicBody && typeB == b2_dynamicBody) {
            if (mA + mB > 0.0001f) {
                reducedMass = (mA * mB) / (mA + mB);
            }
        } else if (typeA == b2_dynamicBody) {
            reducedMass = mA;
        } else if (typeB == b2_dynamicBody) {
            reducedMass = mB;
        }

        const float effectiveImpulse = reducedMass * hit.approachSpeed;

        Collision2DInfo infoA;
        infoA.Other = goB;
        infoA.ContactPointXZ = glm::vec2{ hit.point.x, hit.point.y };
        infoA.NormalXZ = glm::vec2{ hit.normal.x, hit.normal.y }; // points from A -> B
        infoA.ApproachSpeed = hit.approachSpeed;
        infoA.EffectiveImpulse = effectiveImpulse;

        Collision2DInfo infoB;
        infoB.Other = goA;
        infoB.ContactPointXZ = glm::vec2{ hit.point.x, hit.point.y };
        infoB.NormalXZ = glm::vec2{ -hit.normal.x, -hit.normal.y }; // points from B -> A
        infoB.ApproachSpeed = hit.approachSpeed;
        infoB.EffectiveImpulse = effectiveImpulse;

        goA->DispatchCollisionEnter2D(infoA);
        if (!goB->IsPendingDestroy()) {
            goB->DispatchCollisionEnter2D(infoB);
        }
    }

    // 2. Process Contact Begin Events (for pairs not already dispatched by hitEvents)
    for (int i = 0; i < contacts.beginCount; ++i) {
        const b2ContactBeginTouchEvent& begin = contacts.beginEvents[i];
        if (!b2Shape_IsValid(begin.shapeIdA) || !b2Shape_IsValid(begin.shapeIdB)) continue;

        auto pair = makePair(begin.shapeIdA, begin.shapeIdB);
        if (contactedPairs.count(pair) > 0) continue;
        contactedPairs.insert(pair);

        b2BodyId bodyA = b2Shape_GetBody(begin.shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(begin.shapeIdB);
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB)) continue;

        auto* goA = static_cast<GameObject*>(b2Body_GetUserData(bodyA));
        auto* goB = static_cast<GameObject*>(b2Body_GetUserData(bodyB));
        if (!goA || !goB || goA == goB) continue;
        if (goA->IsPendingDestroy() || goB->IsPendingDestroy()) continue;

        const b2BodyType typeA = b2Body_GetType(bodyA);
        const b2BodyType typeB = b2Body_GetType(bodyB);
        const float mA = b2Body_GetMass(bodyA);
        const float mB = b2Body_GetMass(bodyB);

        float reducedMass = 0.0f;
        if (typeA == b2_dynamicBody && typeB == b2_dynamicBody) {
            if (mA + mB > 0.0001f) {
                reducedMass = (mA * mB) / (mA + mB);
            }
        } else if (typeA == b2_dynamicBody) {
            reducedMass = mA;
        } else if (typeB == b2_dynamicBody) {
            reducedMass = mB;
        }

        b2Vec2 vA = b2Body_GetLinearVelocity(bodyA);
        b2Vec2 vB = b2Body_GetLinearVelocity(bodyB);
        b2Vec2 relVel{ vA.x - vB.x, vA.y - vB.y };
        b2Vec2 normal = begin.manifold.normal;
        float approachSpeed = std::max(0.0f, -(relVel.x * normal.x + relVel.y * normal.y));
        float effectiveImpulse = reducedMass * approachSpeed;

        glm::vec2 contactPoint{ 0.0f };
        if (begin.manifold.pointCount > 0) {
            contactPoint = { begin.manifold.points[0].point.x, begin.manifold.points[0].point.y };
        } else {
            b2Vec2 posA = b2Body_GetPosition(bodyA);
            b2Vec2 posB = b2Body_GetPosition(bodyB);
            contactPoint = { (posA.x + posB.x) * 0.5f, (posA.y + posB.y) * 0.5f };
        }

        Collision2DInfo infoA;
        infoA.Other = goB;
        infoA.ContactPointXZ = contactPoint;
        infoA.NormalXZ = glm::vec2{ normal.x, normal.y };
        infoA.ApproachSpeed = approachSpeed;
        infoA.EffectiveImpulse = effectiveImpulse;

        Collision2DInfo infoB;
        infoB.Other = goA;
        infoB.ContactPointXZ = contactPoint;
        infoB.NormalXZ = glm::vec2{ -normal.x, -normal.y };
        infoB.ApproachSpeed = approachSpeed;
        infoB.EffectiveImpulse = effectiveImpulse;

        goA->DispatchCollisionEnter2D(infoA);
        if (!goB->IsPendingDestroy()) {
            goB->DispatchCollisionEnter2D(infoB);
        }
    }

    // 3. Process Contact End Events
    for (int i = 0; i < contacts.endCount; ++i) {
        const b2ContactEndTouchEvent& end = contacts.endEvents[i];
        if (!b2Shape_IsValid(end.shapeIdA) || !b2Shape_IsValid(end.shapeIdB)) continue;

        b2BodyId bodyA = b2Shape_GetBody(end.shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(end.shapeIdB);
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB)) continue;

        auto* goA = static_cast<GameObject*>(b2Body_GetUserData(bodyA));
        auto* goB = static_cast<GameObject*>(b2Body_GetUserData(bodyB));
        if (!goA || !goB || goA == goB) continue;

        if (!goA->IsPendingDestroy()) {
            goA->DispatchCollisionExit2D(goB);
        }
        if (!goB->IsPendingDestroy()) {
            goB->DispatchCollisionExit2D(goA);
        }
    }

    // 4. Process Sensor Begin Events
    for (int i = 0; i < sensors.beginCount; ++i) {
        const b2SensorBeginTouchEvent& begin = sensors.beginEvents[i];
        if (!b2Shape_IsValid(begin.sensorShapeId) || !b2Shape_IsValid(begin.visitorShapeId)) continue;

        b2BodyId sensorBody = b2Shape_GetBody(begin.sensorShapeId);
        b2BodyId visitorBody = b2Shape_GetBody(begin.visitorShapeId);
        if (!b2Body_IsValid(sensorBody) || !b2Body_IsValid(visitorBody)) continue;

        auto* sensorGo = static_cast<GameObject*>(b2Body_GetUserData(sensorBody));
        auto* visitorGo = static_cast<GameObject*>(b2Body_GetUserData(visitorBody));
        if (!sensorGo || !visitorGo || sensorGo == visitorGo) continue;

        if (!sensorGo->IsPendingDestroy()) {
            sensorGo->DispatchTriggerEnter2D(visitorGo);
        }
        if (!visitorGo->IsPendingDestroy()) {
            visitorGo->DispatchTriggerEnter2D(sensorGo);
        }
    }

    // 5. Process Sensor End Events
    for (int i = 0; i < sensors.endCount; ++i) {
        const b2SensorEndTouchEvent& end = sensors.endEvents[i];
        if (!b2Shape_IsValid(end.sensorShapeId) || !b2Shape_IsValid(end.visitorShapeId)) continue;

        b2BodyId sensorBody = b2Shape_GetBody(end.sensorShapeId);
        b2BodyId visitorBody = b2Shape_GetBody(end.visitorShapeId);
        if (!b2Body_IsValid(sensorBody) || !b2Body_IsValid(visitorBody)) continue;

        auto* sensorGo = static_cast<GameObject*>(b2Body_GetUserData(sensorBody));
        auto* visitorGo = static_cast<GameObject*>(b2Body_GetUserData(visitorBody));
        if (!sensorGo || !visitorGo || sensorGo == visitorGo) continue;

        if (!sensorGo->IsPendingDestroy()) {
            sensorGo->DispatchTriggerExit2D(visitorGo);
        }
        if (!visitorGo->IsPendingDestroy()) {
            visitorGo->DispatchTriggerExit2D(sensorGo);
        }
    }

    m_IsIteratingEntities = false;
    FlushEntityLifecycleQueues();
}

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

SceneRenderContext Scene::GatherRenderContext(const PerspectiveCamera* overrideCamera) {
    SceneRenderContext ctx;
    ctx.totalTime = Time::GetTotalTime();
    ctx.physicsAlpha = m_PhysicsAlpha;

    static const auto kCameraMask = Detail::GetComponentTypeMask<CameraComponent>();
    static const auto kDirLightMask = Detail::GetComponentTypeMask<DirectionalLightComponent>();
    static const auto kPostProcessMask = Detail::GetComponentTypeMask<PostProcessComponent>();
    static const auto kWindMask = Detail::GetComponentTypeMask<WindComponent>();
    static const auto kBoxColliderMask = Detail::GetComponentTypeMask<BoxCollider2D>();
    static const auto kPointLightMask = Detail::GetComponentTypeMask<PointLightComponent>();
    static const auto kTerrainMask = Detail::GetComponentTypeMask<TerrainComponent>();
    static const auto kTilemapMask = Detail::GetComponentTypeMask<Tilemap>();
    static const auto kSpriteMask = Detail::GetComponentTypeMask<SpriteRenderer>();
    static const auto kSquashStretchMask = Detail::GetComponentTypeMask<SquashStretchComponent>();
    static const auto kParticleMask = Detail::GetComponentTypeMask<ParticleSystem>();
    static const auto kUIPanelMask = Detail::GetComponentTypeMask<UIPanel>();
    static const auto kUILabelMask = Detail::GetComponentTypeMask<UILabel>();

    ctx.activeCam = overrideCamera;
    if (!ctx.activeCam) {
        for (auto& go : m_GameObjects) {
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
    m_StandeeCommands.reserve(m_GameObjects.size());
    m_ShadowStandeeCommands.clear();
    m_ShadowStandeeCommands.reserve(m_GameObjects.size());
    m_FoliageScratch.clear();
    m_FoliageScratch.reserve(8);
    m_TerrainScratch.clear();
    m_TilemapRenderScratch.clear();
    m_FlatDecalScratch.clear();
    m_ParticleScratch.clear();
    m_UIPanelScratch.clear();
    m_UILabelScratch.clear();

    // Pass 1: Scene-level singletons (directional sun, post process, global wind field, dynamic foliage colliders)
    for (auto& gameObject : m_GameObjects) {
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
        if (m_FoliageScratch.size() < 8 && (mask & kBoxColliderMask)) {
            if (auto* bc = gameObject->GetComponent<BoxCollider2D>(); bc && bc->isDynamic) {
                if (auto* t = gameObject->GetTransform()) {
                    m_FoliageScratch.emplace_back(gameObject.get(), t->position);
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
    m_FoliageInteractorGrid.clear();
    static constexpr float kFoliageInteractionRadius = 1.25f;
    if (ctx.windComp && ctx.windComp->InteractiveRustle && !m_FoliageScratch.empty()) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_FoliageScratch.size()); ++i) {
            const glm::vec3& pos = m_FoliageScratch[i].second;
            const int32_t minCX = static_cast<int32_t>(std::floor((pos.x - kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t maxCX = static_cast<int32_t>(std::floor((pos.x + kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t minCZ = static_cast<int32_t>(std::floor((pos.z - kFoliageInteractionRadius) / kSpatialCellSize));
            const int32_t maxCZ = static_cast<int32_t>(std::floor((pos.z + kFoliageInteractionRadius) / kSpatialCellSize));

            for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
                for (int32_t cx = minCX; cx <= maxCX; ++cx) {
                    m_FoliageInteractorGrid[GetCellKey(cx, cz)].push_back(i);
                }
            }
        }
    }

    // Pass 2: Renderable entity dispatch, frustum culling, and StandeeRenderCommand caching
    for (auto& gameObject : m_GameObjects) {
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
                    if (sprite->GetBillboardMode() == SpriteRenderer::BillboardMode::FlatGround) {
                        const float decalRadius = 0.75f * std::max(std::abs(transform->scale.x), std::abs(transform->scale.z)) + 0.5f;
                        if (camFrustum.IntersectsSphere(transform->GetWorldPosition(), decalRadius)) {
                            m_FlatDecalScratch.emplace_back(sprite, transform);
                        }
                    } else {
                        glm::vec3 boundsCenter{ 0.0f };
                        float boundsRadius = 0.0f;
                        ComputeStandeeBoundingSphere(*transform, boundsCenter, boundsRadius);

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
                                ? ctx.windComp->ComputeSpatialPhase(transform->position, sprite->GetWindPhaseOffset())
                                : sprite->GetWindPhaseOffset();

                            float camProximityBend = 0.0f;
                            float shadowProximityBend = 0.0f;
                            if (ctx.windComp && ctx.windComp->InteractiveRustle && rawWindWeight > 0.0001f && !m_FoliageInteractorGrid.empty()) {
                                const int32_t scx = static_cast<int32_t>(std::floor(transform->position.x / kSpatialCellSize));
                                const int32_t scz = static_cast<int32_t>(std::floor(transform->position.z / kSpatialCellSize));
                                const auto it = m_FoliageInteractorGrid.find(GetCellKey(scx, scz));
                                if (it != m_FoliageInteractorGrid.end() && !it->second.empty()) {
                                    float camBendSum = 0.0f;
                                    float shadowBendSum = 0.0f;
                                    for (uint32_t interactorIdx : it->second) {
                                        const auto& [interactorObj, interactorPos] = m_FoliageScratch[interactorIdx];
                                        if (interactorObj == gameObject.get()) continue;
                                        if (inCamera) {
                                            camBendSum += ctx.windComp->EvaluateProximityBend(
                                                transform->position, interactorPos, camRightXZ, rawWindWeight, ctx.totalTime, 1.2f);
                                        }
                                        if (inShadow) {
                                            shadowBendSum += ctx.windComp->EvaluateProximityBend(
                                                transform->position, interactorPos, sunRightXZ, rawWindWeight, ctx.totalTime, 1.2f);
                                        }
                                    }
                                    if (inCamera) camProximityBend = std::clamp(camBendSum, -0.65f, 0.65f);
                                    if (inShadow) shadowProximityBend = std::clamp(shadowBendSum, -0.65f, 0.65f);
                                }
                            }

                            StandeeRenderCommand cmd;
                            cmd.transform = transform;
                            cmd.sprite = sprite;
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
                            cmd.viewDepth = glm::dot(transform->GetWorldPosition() - camEye, camForward);
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

void Scene::RenderPassShadowMap(SceneRenderContext& ctx) {
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

void Scene::RenderPassGroundPlane(SceneRenderContext& ctx) {
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

void Scene::RenderPassCutoutStandees(SceneRenderContext& ctx) {
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

void Scene::RenderPassParticles(SceneRenderContext& ctx) {
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

void Scene::RenderPassScreenUI() {
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

void Scene::Render(const PerspectiveCamera* overrideCamera) {
    ENGINE_CORE_TRACE("Scene '{}': Render (2.5D Diorama)", m_Name);

    ResourceManager::ProcessPendingUploads();
    SceneRenderContext ctx = GatherRenderContext(overrideCamera);

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
