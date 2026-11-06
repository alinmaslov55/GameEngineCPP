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
namespace {
inline void ComputeStandeeBoundingSphere(const Transform& transform,
                                         glm::vec3& outCenter,
                                         float& outRadius) noexcept {
    const float width  = std::abs(transform.scale.x);
    const float height = std::abs(transform.scale.y);
    const float depth  = std::abs(transform.scale.z);
    outCenter = transform.position + glm::vec3(0.0f, 0.5f * height, 0.0f);
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
    m_PendingDestroyQueue.clear();
    m_PendingSpawnQueue.clear();
    m_EntityUUIDMap.clear();
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}'", m_Name, name);
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
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

    if (!m_IsIteratingEntities) {
        FlushEntityLifecycleQueues();
    }
}

GameObject* Scene::FindGameObjectByUUID(uint64_t uuid) const noexcept {
    auto it = m_EntityUUIDMap.find(uuid);
    if (it != m_EntityUUIDMap.end() && !it->second->IsPendingDestroy()) {
        return it->second;
    }
    return nullptr;
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
                m_GameObjects.push_back(std::move(pending));
                m_StaticCollidersDirty = true;
            }
        }
        m_PendingSpawnQueue.clear();
    }

    // 2. Process pending destroys
    if (!m_PendingDestroyQueue.empty()) {
        bool colliderDestroyed = false;
        b2WorldId worldId = m_IsRuntimeRunning ? b2LoadWorldId(m_PhysicsWorldId) : b2_nullWorldId;

        for (auto* go : m_PendingDestroyQueue) {
            if (!go) continue;

            // Destroy runtime Box2D body if active
            if (m_IsRuntimeRunning && b2World_IsValid(worldId)) {
                if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
                    if (rb2d->RuntimeBody) {
                        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rb2d->RuntimeBody)));
                        if (b2Body_IsValid(bodyId)) {
                            b2DestroyBody(bodyId);
                        }
                        rb2d->RuntimeBody = nullptr;
                    }
                }
                if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
                    bc2d->RuntimeFixture = nullptr;
                }
            }

            if (go->GetComponent<BoxCollider2D>() || go->GetComponent<Tilemap>() || go->GetComponent<TerrainComponent>()) {
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

    UpdateCollidersAndGravity(deltaTime);
    ResolveEntityCollisions();
    ResolveTilemapCollisions();
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
    worldDef.gravity = b2Vec2{ 0.0f, -9.8f };

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
        }
    }
    for (auto& go : m_PendingSpawnQueue) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->RuntimeBody = nullptr;
        }
        if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
            bc2d->RuntimeFixture = nullptr;
        }
    }
    m_StaticCollidersDirty = true;
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

    static const uint64_t kCameraMask = Detail::GetComponentTypeMask<CameraComponent>();
    static const uint64_t kDirLightMask = Detail::GetComponentTypeMask<DirectionalLightComponent>();
    static const uint64_t kPostProcessMask = Detail::GetComponentTypeMask<PostProcessComponent>();
    static const uint64_t kWindMask = Detail::GetComponentTypeMask<WindComponent>();
    static const uint64_t kBoxColliderMask = Detail::GetComponentTypeMask<BoxCollider2D>();
    static const uint64_t kPointLightMask = Detail::GetComponentTypeMask<PointLightComponent>();
    static const uint64_t kTerrainMask = Detail::GetComponentTypeMask<TerrainComponent>();
    static const uint64_t kTilemapMask = Detail::GetComponentTypeMask<Tilemap>();
    static const uint64_t kSpriteMask = Detail::GetComponentTypeMask<SpriteRenderer>();
    static const uint64_t kSquashStretchMask = Detail::GetComponentTypeMask<SquashStretchComponent>();
    static const uint64_t kParticleMask = Detail::GetComponentTypeMask<ParticleSystem>();
    static const uint64_t kUIPanelMask = Detail::GetComponentTypeMask<UIPanel>();
    static const uint64_t kUILabelMask = Detail::GetComponentTypeMask<UILabel>();

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
        const uint64_t mask = gameObject->GetComponentMask();
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
        const uint64_t mask = gameObject->GetComponentMask();

        if (mask & kPointLightMask) {
            if (auto* pl = gameObject->GetComponent<PointLightComponent>()) {
                if (ctx.lighting.PointLightCount < SceneLightingData::MaxPointLights) {
                    auto* t = gameObject->GetTransform();
                    const glm::vec3 basePos = t ? t->position : glm::vec3(0.0f);
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
                        if (camFrustum.IntersectsSphere(transform->position, decalRadius)) {
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
                            cmd.viewDepth = glm::dot(transform->position - camEye, camForward);
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
