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

    if (IsIteratingEntities()) {
        rawPtr->m_LifecycleState = EntityLifecycle::PendingSpawn;
        m_PendingSpawnQueue.push_back(std::move(gameObject));
    } else {
        rawPtr->m_LifecycleState = EntityLifecycle::Alive;
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

    if (IsIteratingEntities()) {
        rawPtr->m_LifecycleState = EntityLifecycle::PendingSpawn;
        m_PendingSpawnQueue.push_back(std::move(gameObject));
    } else {
        rawPtr->m_LifecycleState = EntityLifecycle::Alive;
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

    ScopedIterationGuard guard(*this);

    ENGINE_CORE_DEBUG("Scene '{}': Destroying GameObject '{}' (UUID: {})", m_Name, gameObject->GetName(), gameObject->GetUUID());
    const bool wasPendingSpawn = (gameObject->m_LifecycleState == EntityLifecycle::PendingSpawn);
    gameObject->m_LifecycleState = EntityLifecycle::PendingDestroy;
    if (!wasPendingSpawn) {
        m_PendingDestroyQueue.push_back(gameObject);
    }
    std::erase(m_PendingPhysicsSync, gameObject);

    if (m_IsRuntimeRunning) {
        if (auto* rb2d = gameObject->GetComponent<Rigidbody2D>()) {
            if (rb2d->RuntimeBody) {
                b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
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
}

void Scene::MarkPhysicsDirty(GameObject* go) {
    if (!m_IsRuntimeRunning || !go || go->IsPendingDestroy()) return;
    if (std::find(m_PendingPhysicsSync.begin(), m_PendingPhysicsSync.end(), go) == m_PendingPhysicsSync.end()) {
        m_PendingPhysicsSync.push_back(go);
    }
}

void Scene::RegisterRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    auto* transform = go->GetTransform();
    if (!transform) return;

    auto* rb2d = go->GetComponent<Rigidbody2D>();
    auto colliders = go->GetComponents<Collider2D>();
    auto* tilemap = go->GetComponent<Tilemap>();

    if (rb2d) {
        // If there was an old static body from previously standalone colliders, destroy it
        BodyHandle oldStaticBody;
        for (auto* col : colliders) {
            if (col->RuntimeBody.IsValid() && col->RuntimeBody != rb2d->RuntimeBody) {
                if (!oldStaticBody.IsValid()) oldStaticBody = col->RuntimeBody;
                col->RuntimeBody.Reset();
                col->RuntimeShape.Reset();
            }
        }
        if (oldStaticBody.IsValid()) {
            b2BodyId oldStatic = b2LoadBodyId(oldStaticBody.Value);
            if (b2Body_IsValid(oldStatic)) {
                b2DestroyBody(oldStatic);
            }
        }

        b2BodyId bodyId;
        bool baseMassRecomputed = false;
        if (!rb2d->RuntimeBody.IsValid()) {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            if (rb2d->IsStasisFrozen()) {
                bodyDef.type = b2_kinematicBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Static) {
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
            rb2d->RuntimeBody = BodyHandle{ b2StoreBodyId(bodyId) };
            baseMassRecomputed = true;

            if (rb2d->IsStasisFrozen()) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                b2Body_SetAngularVelocity(bodyId, 0.0f);
            } else if (rb2d->InitialLinearVelocityXZ.x != 0.0f || rb2d->InitialLinearVelocityXZ.y != 0.0f) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ rb2d->InitialLinearVelocityXZ.x, rb2d->InitialLinearVelocityXZ.y });
            }

            if (!go->IsActiveInHierarchy()) {
                b2Body_Disable(bodyId);
            }
        } else {
            bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
            if (b2Body_IsValid(bodyId)) {
                if (b2Body_GetUserData(bodyId) != go) {
                    b2Body_SetUserData(bodyId, go);
                }
                if (!rb2d->IsStasisFrozen()) {
                    b2BodyType targetType = b2_dynamicBody;
                    if (rb2d->Type == Rigidbody2D::BodyType::Static) targetType = b2_staticBody;
                    else if (rb2d->Type == Rigidbody2D::BodyType::Kinematic) targetType = b2_kinematicBody;
                    if (b2Body_GetType(bodyId) != targetType) {
                        b2Body_SetType(bodyId, targetType);
                        baseMassRecomputed = true;
                    }
                }
                if (b2Body_IsFixedRotation(bodyId) != rb2d->FixedRotation) {
                    b2Body_SetFixedRotation(bodyId, rb2d->FixedRotation);
                    baseMassRecomputed = true;
                }
                if (std::abs(b2Body_GetLinearDamping(bodyId) - rb2d->LinearDamping) > 1e-5f) {
                    b2Body_SetLinearDamping(bodyId, rb2d->LinearDamping);
                }
                if (std::abs(b2Body_GetAngularDamping(bodyId) - rb2d->AngularDamping) > 1e-5f) {
                    b2Body_SetAngularDamping(bodyId, rb2d->AngularDamping);
                }
                if (b2Body_IsBullet(bodyId) != rb2d->IsBullet) {
                    b2Body_SetBullet(bodyId, rb2d->IsBullet);
                }
            }
        }

        for (auto* col : colliders) {
            col->RuntimeBody = rb2d->RuntimeBody;
            const bool hadShape = col->RuntimeShape.IsValid();
            col->CreateOrUpdateShape(rb2d->RuntimeBody, *transform, go);
            if (!hadShape && col->RuntimeShape.IsValid()) {
                baseMassRecomputed = true;
            }
        }

        if (baseMassRecomputed && b2Body_IsValid(bodyId)) {
            if (b2Body_GetShapeCount(bodyId) > 0) {
                b2Body_ApplyMassFromShapes(bodyId);
            }
            const float unscaledMass = b2Body_GetMass(bodyId);
            rb2d->SetBaseMass((unscaledMass > 0.0001f) ? unscaledMass : 1.0f);
        }
        rb2d->ApplyMassMultiplierToBody();
    } else if (!colliders.empty()) {
        BodyHandle bodyHandle;
        for (auto* col : colliders) {
            if (col->RuntimeBody.IsValid()) {
                bodyHandle = col->RuntimeBody;
                break;
            }
        }

        b2BodyId bodyId;
        if (bodyHandle.IsValid()) {
            bodyId = b2LoadBodyId(bodyHandle.Value);
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
            bodyHandle = BodyHandle{ b2StoreBodyId(bodyId) };
        }

        for (auto* col : colliders) {
            col->RuntimeBody = bodyHandle;
            col->CreateOrUpdateShape(bodyHandle, *transform, go);
        }
    }

    if (tilemap && !tilemap->RuntimeBody.IsValid()) {
        CreateRuntimeTilemapBody(go);
    }
}

void Scene::UnregisterRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    std::erase(m_PendingPhysicsSync, go);

    BodyHandle destroyedBody;
    if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
        if (rb2d->RuntimeBody.IsValid()) {
            destroyedBody = rb2d->RuntimeBody;
            b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
        }
        rb2d->ResetRuntimeState();
    }

    BodyHandle staticBody;
    for (auto* col : go->GetComponents<Collider2D>()) {
        if (col->RuntimeBody.IsValid() && col->RuntimeBody != destroyedBody) {
            if (!staticBody.IsValid()) staticBody = col->RuntimeBody;
        }
        col->RuntimeBody.Reset();
        col->RuntimeShape.Reset();
    }

    if (staticBody.IsValid()) {
        b2BodyId bodyId = b2LoadBodyId(staticBody.Value);
        if (b2Body_IsValid(bodyId)) {
            b2DestroyBody(bodyId);
        }
    }

    if (auto* tilemap = go->GetComponent<Tilemap>()) {
        if (tilemap->RuntimeBody.IsValid()) {
            b2BodyId bodyId = b2LoadBodyId(tilemap->RuntimeBody.Value);
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
            tilemap->RuntimeBody.Reset();
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

    if (tilemap->RuntimeBody.IsValid()) return;

    const uint32_t width = tilemap->GetWidth();
    const uint32_t height = tilemap->GetHeight();
    if (width == 0 || height == 0) return;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_staticBody;
    bodyDef.position = b2Vec2{ 0.0f, 0.0f };
    bodyDef.name = go->GetName().c_str();
    bodyDef.userData = go;

    b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
    tilemap->RuntimeBody = BodyHandle{ b2StoreBodyId(bodyId) };

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
            auto* rb2d = instance->GetComponent<Rigidbody2D>();
            if (rb2d) {
                if (rb2d->RuntimeBody.IsValid()) {
                    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
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
            for (auto* col : instance->GetComponents<Collider2D>()) {
                if (col->RuntimeBody.IsValid() && (!rb2d || col->RuntimeBody != rb2d->RuntimeBody)) {
                    b2BodyId bodyId = b2LoadBodyId(col->RuntimeBody.Value);
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

        instance->SetActive(true);
    }

    return instance;
}

void Scene::ReleaseToPool(const std::string& poolKey, GameObject* instance) {
    if (!instance) return;

    // Reset physics state
    auto* rb2d = instance->GetComponent<Rigidbody2D>();
    if (rb2d) {
        if (m_IsRuntimeRunning && rb2d->RuntimeBody.IsValid()) {
            b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                b2Body_SetAngularVelocity(bodyId, 0.0f);
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
        rb2d->VerticalVelocity = 0.0f;
    }
    for (auto* col : instance->GetComponents<Collider2D>()) {
        if (m_IsRuntimeRunning && col->RuntimeBody.IsValid() && (!rb2d || col->RuntimeBody != rb2d->RuntimeBody)) {
            b2BodyId bodyId = b2LoadBodyId(col->RuntimeBody.Value);
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

void Scene::EndIteration() {
    if (m_IterationDepth > 0) {
        --m_IterationDepth;
        if (m_IterationDepth == 0) {
            FlushEntityLifecycleQueues();
        }
    }
}

void Scene::FlushEntityLifecycleQueues() {
    if (m_IterationDepth > 0) {
        return;
    }
    if (m_PendingSpawnQueue.empty() && m_PendingDestroyQueue.empty()) {
        return;
    }

    struct FlushDepthGuard {
        uint32_t& depth;
        explicit FlushDepthGuard(uint32_t& d) noexcept : depth(d) { depth = 1; }
        ~FlushDepthGuard() noexcept { depth = 0; }
    } flushGuard(m_IterationDepth);

    constexpr uint32_t kMaxFlushPasses = 16;
    uint32_t passes = 0;

    while ((!m_PendingSpawnQueue.empty() || !m_PendingDestroyQueue.empty()) && passes < kMaxFlushPasses) {
        ++passes;

        // 1. Process pending spawns
        if (!m_PendingSpawnQueue.empty()) {
            auto pendingSpawns = std::move(m_PendingSpawnQueue);
            m_PendingSpawnQueue.clear();

            for (auto& pending : pendingSpawns) {
                if (!pending) continue;

                if (pending->IsPendingDestroy()) {
                    pending->m_LifecycleState = EntityLifecycle::Destroyed;

                    if (m_IsRuntimeRunning) {
                        UnregisterRuntimeBody(pending.get());
                    }

                    if (pending->GetComponent<BoxCollider2D>() || pending->GetComponent<CircleCollider2D>() ||
                        pending->GetComponent<CapsuleCollider2D>() || pending->GetComponent<Tilemap>() ||
                        pending->GetComponent<TerrainComponent>()) {
                        m_StaticCollidersDirty = true;
                    }

                    for (size_t i = 0; i < pending->m_Components.size(); ++i) {
                        if (pending->m_Components[i]) {
                            pending->m_Components[i]->OnDestroy();
                        }
                    }

                    m_EntityUUIDMap.erase(pending->GetUUID());
                } else {
                    pending->m_LifecycleState = EntityLifecycle::Alive;
                    pending->ProcessPendingStarts();
                    if (m_IsRuntimeRunning) {
                        RegisterRuntimeBody(pending.get());
                    }
                    m_GameObjects.push_back(std::move(pending));
                    m_StaticCollidersDirty = true;
                }
            }
        }

        // 2. Process pending destroys
        if (!m_PendingDestroyQueue.empty()) {
            auto pendingDestroys = std::move(m_PendingDestroyQueue);
            m_PendingDestroyQueue.clear();

            bool colliderDestroyed = false;

            for (auto* go : pendingDestroys) {
                if (!go) continue;

                go->m_LifecycleState = EntityLifecycle::Destroyed;

                // Destroy runtime Box2D body if active
                if (m_IsRuntimeRunning) {
                    UnregisterRuntimeBody(go);
                }

                if (go->GetComponent<BoxCollider2D>() || go->GetComponent<CircleCollider2D>() ||
                    go->GetComponent<CapsuleCollider2D>() || go->GetComponent<Tilemap>() ||
                    go->GetComponent<TerrainComponent>()) {
                    colliderDestroyed = true;
                }

                // Invoke OnDestroy() on all components
                for (size_t i = 0; i < go->m_Components.size(); ++i) {
                    if (go->m_Components[i]) {
                        go->m_Components[i]->OnDestroy();
                    }
                }

                m_EntityUUIDMap.erase(go->GetUUID());
            }

            // Erase destroyed GameObjects from m_GameObjects in a single erase_if pass
            std::erase_if(m_GameObjects, [](const std::unique_ptr<GameObject>& ptr) {
                return ptr->IsDestroyed();
            });

            if (colliderDestroyed) {
                m_StaticCollidersDirty = true;
            }
        }
    }

    if (passes >= kMaxFlushPasses) {
        ENGINE_CORE_ERROR("Scene '{}': Reached max flush passes ({}), possible infinite lifecycle recursion", m_Name, kMaxFlushPasses);
    }
}

void Scene::Update(float deltaTime) {
    ENGINE_CORE_TRACE("Scene '{}': Update (deltaTime: {:.4f}s, {} GameObjects)", m_Name, deltaTime, m_GameObjects.size());

    {
        ScopedIterationGuard guard(*this);
        for (size_t i = 0; i < m_GameObjects.size(); ++i) {
            auto& gameObject = m_GameObjects[i];
            if (gameObject->IsActiveInHierarchy()) {
                gameObject->Update(deltaTime);
            }
        }
    }

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
            auto* rb = gameObject->GetComponent<Rigidbody2D>();
            const bool isDynamic = rb && (rb->Type == Rigidbody2D::BodyType::Dynamic);
            if (isDynamic) {
                m_DynamicColliders.push_back({ gameObject.get(), transform, collider });
                // Apply vertical isometric gravity & ground clamping for dynamic bodies
                if (rb->UseGravityY && transform) {
                    rb->VerticalVelocity += rb->GravityY * rb->GravityScaleY * deltaTime;
                    transform->position.y += rb->VerticalVelocity * deltaTime;
                    if (transform->position.y <= rb->GroundLevelY) {
                        transform->position.y = rb->GroundLevelY;
                        rb->VerticalVelocity = 0.0f;
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
                auto* rb = gameObject->GetComponent<Rigidbody2D>();
                const bool isDynamic = rb && (rb->Type == Rigidbody2D::BodyType::Dynamic);
                if (!isDynamic) {
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

            auto* rbA = entryA.gameObject ? entryA.gameObject->GetComponent<Rigidbody2D>() : nullptr;
            auto* rbB = entryB.gameObject ? entryB.gameObject->GetComponent<Rigidbody2D>() : nullptr;
            const bool isDynA = rbA && (rbA->Type == Rigidbody2D::BodyType::Dynamic);
            const bool isDynB = rbB && (rbB->Type == Rigidbody2D::BodyType::Dynamic);

            if (isDynA && !isDynB) {
                tA->position.x += push.x;
                tA->position.z += push.y;
            } else if (!isDynA && isDynB) {
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
        auto* rbA = entryA.gameObject ? entryA.gameObject->GetComponent<Rigidbody2D>() : nullptr;
        if (!rbA || rbA->Type != Rigidbody2D::BodyType::Dynamic) continue;

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
    m_PendingPhysicsSync.clear();
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
    m_PendingPhysicsSync.clear();
    ResetPhysicsAccumulator();

    for (auto& slot : m_Box2DTasks) {
        slot.inUse.store(false, std::memory_order_relaxed);
        slot.done.store(true, std::memory_order_relaxed);
        slot.remaining.store(0, std::memory_order_relaxed);
    }

    for (auto& go : m_GameObjects) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->ResetRuntimeState();
        }
        for (auto* col : go->GetComponents<Collider2D>()) {
            col->RuntimeShape.Reset();
            col->RuntimeBody.Reset();
        }
        if (auto* tilemap = go->GetComponent<Tilemap>()) {
            tilemap->RuntimeBody.Reset();
        }
    }
    for (auto& go : m_PendingSpawnQueue) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->ResetRuntimeState();
        }
        for (auto* col : go->GetComponents<Collider2D>()) {
            col->RuntimeShape.Reset();
            col->RuntimeBody.Reset();
        }
        if (auto* tilemap = go->GetComponent<Tilemap>()) {
            tilemap->RuntimeBody.Reset();
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
        if (!m_PendingPhysicsSync.empty()) {
            auto pendingSync = std::move(m_PendingPhysicsSync);
            m_PendingPhysicsSync.clear();
            for (auto* go : pendingSync) {
                if (go && !go->IsPendingDestroy() && go->IsActiveInHierarchy()) {
                    RegisterRuntimeBody(go);
                }
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
            {
                ScopedIterationGuard guard(*this);
                for (size_t i = 0; i < m_GameObjects.size(); ++i) {
                    auto& go = m_GameObjects[i];
                    if (go->IsActiveInHierarchy()) {
                        go->FixedUpdate(fixedDt);
                    }
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
                    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
                    if (b2Body_IsValid(bodyId)) {
                        b2Vec2 pos = b2Body_GetPosition(bodyId);
                        b2Rot rot = b2Body_GetRotation(bodyId);
                        if (transform->GetParent()) {
                            transform->SetWorldPosition({ pos.x, transform->position.y, pos.y });
                        } else {
                            transform->position.x = pos.x;
                            transform->position.z = pos.y;
                            transform->SetDirty();
                        }
                        if (!rb2d->FixedRotation) {
                            transform->rotation.y = glm::degrees(b2Rot_GetAngle(rot));
                            transform->SetDirty();
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
        }
    }
}

void Scene::ApplyAirbornePassThroughFilter(GameObject* go, bool airborne) {
    if (!go) return;
    auto* rb2d = go->GetComponent<Rigidbody2D>();
    const uint64_t excludedMask = rb2d ? rb2d->AirbornePassThroughMask : PhysicsLayer::DefaultAirborneExcluded;

    for (auto* col : go->GetComponents<Collider2D>()) {
        if (!col->RuntimeShape.IsValid()) continue;
        b2ShapeId shapeId = b2LoadShapeId(col->RuntimeShape.Value);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter filter = b2Shape_GetFilter(shapeId);
            filter.maskBits = airborne ? (col->MaskBits & ~excludedMask) : col->MaskBits;
            b2Shape_SetFilter(shapeId, filter);
        }
    }
}

void Scene::ProcessPhysicsEvents() {
    if (!m_IsRuntimeRunning) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    b2ContactEvents contacts = b2World_GetContactEvents(worldId);
    b2SensorEvents sensors = b2World_GetSensorEvents(worldId);

    // Guard entity lifecycle so DestroyGameObject calls during event dispatch defer to lifecycle queues
    ScopedIterationGuard guard(*this);

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
    static const auto kRigidbodyMask = Detail::GetComponentTypeMask<Rigidbody2D>();
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
                    const bool useInterp = m_IsRuntimeRunning && (m_PhysicsAlpha > 0.0f);
                    const glm::vec3 worldPos = useInterp
                        ? transform->GetInterpolatedWorldPosition(m_PhysicsAlpha)
                        : transform->GetWorldPosition();
                    const glm::vec3 worldRot = useInterp
                        ? transform->GetInterpolatedWorldRotation(m_PhysicsAlpha)
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
                            if (ctx.windComp && ctx.windComp->InteractiveRustle && rawWindWeight > 0.0001f && !m_FoliageInteractorGrid.empty()) {
                                const int32_t scx = static_cast<int32_t>(std::floor(worldPos.x / kSpatialCellSize));
                                const int32_t scz = static_cast<int32_t>(std::floor(worldPos.z / kSpatialCellSize));
                                const auto it = m_FoliageInteractorGrid.find(GetCellKey(scx, scz));
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
