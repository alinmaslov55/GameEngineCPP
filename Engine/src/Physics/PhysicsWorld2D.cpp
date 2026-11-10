#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Time.h"
#include "Engine/Utils/Logger.h"

#include <box2d/box2d.h>
#include <box2d/math_functions.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Engine {

PhysicsWorld2D::PhysicsWorld2D(Scene* scene) noexcept
    : m_Scene(scene) {
    m_ContactedPairsScratch.reserve(64);
}

PhysicsWorld2D::~PhysicsWorld2D() {
    if (m_IsRuntimeRunning) {
        OnRuntimeStop();
    }
}

void PhysicsWorld2D::MarkPhysicsDirty(GameObject* go) {
    if (!m_IsRuntimeRunning || !go || go->IsPendingDestroy()) return;
    if (std::find(m_PendingPhysicsSync.begin(), m_PendingPhysicsSync.end(), go) == m_PendingPhysicsSync.end()) {
        m_PendingPhysicsSync.push_back(go);
    }
}

void PhysicsWorld2D::RegisterRuntimeBody(GameObject* go) {
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
            const glm::vec3 worldRot = transform->GetWorldRotation();
            bodyDef.position = b2Vec2{ worldPos.x, worldPos.z };
            bodyDef.rotation = b2MakeRot(glm::radians(worldRot.y));
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

                const glm::vec3 worldPos = transform->GetWorldPosition();
                const glm::vec3 worldRot = transform->GetWorldRotation();
                b2Vec2 currentB2Pos = b2Body_GetPosition(bodyId);
                b2Rot currentB2Rot = b2Body_GetRotation(bodyId);
                float currentAngle = b2Rot_GetAngle(currentB2Rot);
                float targetAngle = glm::radians(worldRot.y);
                if (std::abs(currentB2Pos.x - worldPos.x) > 1e-4f ||
                    std::abs(currentB2Pos.y - worldPos.z) > 1e-4f ||
                    (!rb2d->FixedRotation && std::abs(currentAngle - targetAngle) > 1e-4f)) {
                    b2Body_SetTransform(bodyId, b2Vec2{ worldPos.x, worldPos.z }, b2MakeRot(targetAngle));
                }
            }
        }

        if (rb2d->Type == Rigidbody2D::BodyType::Dynamic && rb2d->EnableAirbornePassThrough) {
            const float worldY = transform->GetParent() ? transform->GetWorldPosition().y : transform->position.y;
            const bool wasAirborne = rb2d->IsAirbornePassThroughActive();
            if (worldY > rb2d->AirborneClearanceHeight && !wasAirborne) {
                rb2d->SetAirbornePassThroughActive(true);
                ApplyAirbornePassThroughFilter(go, true);
            } else if (worldY <= rb2d->GroundLevelY && wasAirborne) {
                rb2d->SetAirbornePassThroughActive(false);
                ApplyAirbornePassThroughFilter(go, false);
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

        if (rb2d->RuntimeBody.IsValid()) {
            UpsertPhysicsBodyEntry(go, transform, rb2d, rb2d->RuntimeBody);
        } else {
            RemovePhysicsBodyEntry(go);
        }
    } else {
        RemovePhysicsBodyEntry(go);

        if (!colliders.empty()) {
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
                const glm::vec3 worldRot = transform->GetWorldRotation();
                bodyDef.position = b2Vec2{ worldPos.x, worldPos.z };
                bodyDef.rotation = b2MakeRot(glm::radians(worldRot.y));
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
    }

    if (tilemap && !tilemap->RuntimeBody.IsValid()) {
        CreateRuntimeTilemapBody(go);
    }
}

void PhysicsWorld2D::UpsertPhysicsBodyEntry(GameObject* go, Transform* transform, Rigidbody2D* rb2d, BodyHandle handle) {
    if (!go || !transform || !rb2d || !handle.IsValid()) return;
    auto it = m_PhysicsBodyIndices.find(go);
    if (it != m_PhysicsBodyIndices.end()) {
        auto& entry = m_PhysicsBodies[it->second];
        entry.gameObject = go;
        entry.transform = transform;
        entry.rigidbody = rb2d;
        entry.bodyHandle = handle;
    } else {
        const size_t newIndex = m_PhysicsBodies.size();
        m_PhysicsBodies.push_back(PhysicsBodyEntry{ go, transform, rb2d, handle, 0, false });
        m_PhysicsBodyIndices[go] = newIndex;
    }
}

void PhysicsWorld2D::RemovePhysicsBodyEntry(GameObject* go) {
    if (!go) return;
    auto it = m_PhysicsBodyIndices.find(go);
    if (it != m_PhysicsBodyIndices.end()) {
        const size_t idx = it->second;
        const size_t lastIdx = m_PhysicsBodies.size() - 1;
        if (idx != lastIdx) {
            m_PhysicsBodies[idx] = m_PhysicsBodies[lastIdx];
            m_PhysicsBodyIndices[m_PhysicsBodies[idx].gameObject] = idx;
        }
        m_PhysicsBodies.pop_back();
        m_PhysicsBodyIndices.erase(it);
    }
}

void PhysicsWorld2D::UnregisterRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    std::erase(m_PendingPhysicsSync, go);
    RemovePhysicsBodyEntry(go);

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

void PhysicsWorld2D::DisableRuntimeBody(GameObject* go) {
    if (!m_IsRuntimeRunning || !go) return;
    if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
        if (rb2d->RuntimeBody.IsValid()) {
            b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
            if (b2Body_IsValid(bodyId)) {
                b2Body_Disable(bodyId);
            }
        }
    }
}

void PhysicsWorld2D::CreateRuntimePhysicsBody(GameObject* go) {
    RegisterRuntimeBody(go);
}

void PhysicsWorld2D::CreateRuntimeTilemapBody(GameObject* go) {
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

void* PhysicsWorld2D::Box2DEnqueueTask(b2TaskCallback* task, int itemCount, int minRange, void* taskContext, void* userContext) {
    if (!task || itemCount <= 0) {
        return nullptr;
    }

    auto* physWorld = static_cast<PhysicsWorld2D*>(userContext);
    const uint32_t workers = JobSystem::GetWorkerCount();

    if (minRange <= 0) {
        minRange = 1;
    }

    const int maxChunks = itemCount / minRange;
    const int availableThreads = static_cast<int>(workers + 1);
    const int chunkCount = std::min(availableThreads, maxChunks);

    if (chunkCount <= 1 || !JobSystem::IsInitialized() || workers == 0 || !physWorld) {
        const int32_t currentWorker = JobSystem::GetCurrentWorkerIndex();
        const uint32_t callerWorker = (currentWorker >= 0 && currentWorker < static_cast<int32_t>(workers))
                                          ? static_cast<uint32_t>(currentWorker)
                                          : workers;
        task(0, itemCount, callerWorker, taskContext);
        if (physWorld) {
            ++physWorld->m_Box2DSerialTasksExecuted;
        }
        return nullptr;
    }

    Box2DTask* taskSlot = nullptr;
    for (auto& slot : physWorld->m_Box2DTasks) {
        bool expected = false;
        if (slot.inUse.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            taskSlot = &slot;
            break;
        }
    }

    if (!taskSlot) {
        const int32_t currentWorker = JobSystem::GetCurrentWorkerIndex();
        const uint32_t callerWorker = (currentWorker >= 0 && currentWorker < static_cast<int32_t>(workers))
                                          ? static_cast<uint32_t>(currentWorker)
                                          : workers;
        task(0, itemCount, callerWorker, taskContext);
        ++physWorld->m_Box2DSerialTasksExecuted;
        return nullptr;
    }

    taskSlot->remaining.store(chunkCount, std::memory_order_release);
    taskSlot->done.store(false, std::memory_order_release);
    ++physWorld->m_Box2DParallelTasksDispatched;

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

void PhysicsWorld2D::Box2DFinishTask(void* userTask, void* userContext) {
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

void PhysicsWorld2D::OnRuntimeStart() {
    if (m_IsRuntimeRunning) {
        ENGINE_CORE_WARN("PhysicsWorld2D: OnRuntimeStart called while already running");
        return;
    }

    ENGINE_CORE_INFO("PhysicsWorld2D: Starting Box2D runtime physics");

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{ 0.0f, 0.0f };

    const uint32_t workers = JobSystem::GetWorkerCount();
    if (JobSystem::IsInitialized() && workers > 0) {
        worldDef.workerCount = static_cast<int>(workers + 1);
        worldDef.enqueueTask = &PhysicsWorld2D::Box2DEnqueueTask;
        worldDef.finishTask = &PhysicsWorld2D::Box2DFinishTask;
        worldDef.userTaskContext = this;
        ENGINE_CORE_INFO("PhysicsWorld2D: Enabling Box2D v3 worker multithreading with {} workers", worldDef.workerCount);
    }

    b2WorldId worldId = b2CreateWorld(&worldDef);
    m_PhysicsWorldId = b2StoreWorldId(worldId);
    m_IsRuntimeRunning = true;
    m_PendingPhysicsSync.clear();
    m_PhysicsBodies.clear();
    m_PhysicsBodyIndices.clear();
    ResetPhysicsAccumulator();

    if (m_Scene) {
        for (auto& go : m_Scene->GetGameObjects()) {
            RegisterRuntimeBody(go.get());
        }
        for (auto& go : m_Scene->GetGameObjects()) {
            if (auto* transform = go->GetTransform()) {
                transform->SnapshotPrevious();
            }
        }
    }
    m_StaticCollidersDirty = true;
}

void PhysicsWorld2D::OnRuntimeStop() {
    if (!m_IsRuntimeRunning) {
        return;
    }

    ENGINE_CORE_INFO("PhysicsWorld2D: Stopping Box2D runtime physics");

    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (b2World_IsValid(worldId)) {
        b2DestroyWorld(worldId);
    }
    m_PhysicsWorldId = 0;
    m_IsRuntimeRunning = false;
    m_PendingPhysicsSync.clear();
    m_PhysicsBodies.clear();
    m_PhysicsBodyIndices.clear();
    ResetPhysicsAccumulator();

    for (auto& slot : m_Box2DTasks) {
        slot.inUse.store(false, std::memory_order_relaxed);
        slot.done.store(true, std::memory_order_relaxed);
        slot.remaining.store(0, std::memory_order_relaxed);
    }

    if (m_Scene) {
        for (auto& go : m_Scene->GetGameObjects()) {
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
    }
    m_StaticCollidersDirty = true;
}

void PhysicsWorld2D::SyncMovedBodyTransform(const b2BodyMoveEvent& ev) {
    auto* go = static_cast<GameObject*>(ev.userData);
    if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) {
        return;
    }

    auto* transform = go->GetTransform();
    auto* rb2d = go->GetComponent<Rigidbody2D>();
    if (!transform || !rb2d) {
        return;
    }

    const b2Vec2 pos = ev.transform.p;
    const b2Rot rot = ev.transform.q;
    if (transform->GetParent()) {
        transform->SetWorldPosition({ pos.x, transform->GetWorldPosition().y, pos.y });
        if (!rb2d->FixedRotation) {
            transform->SetWorldRotation({ transform->rotation.x, glm::degrees(b2Rot_GetAngle(rot)), transform->rotation.z });
        }
    } else {
        transform->position.x = pos.x;
        transform->position.z = pos.y;
        if (!rb2d->FixedRotation) {
            transform->rotation.y = glm::degrees(b2Rot_GetAngle(rot));
        }
        transform->SetDirty();
    }
}

void PhysicsWorld2D::UpdateRuntime(float deltaTime) {
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
            // Pass 1: Snapshot previous transforms across dense registered physics entries only
            for (const auto& entry : m_PhysicsBodies) {
                if (entry.transform && entry.gameObject && !entry.gameObject->IsPendingDestroy() && entry.gameObject->IsActiveInHierarchy()) {
                    entry.transform->SnapshotPrevious();
                }
            }

            // Pass 2: Fixed update dispatch on active components under iteration guard
            if (m_Scene) {
                Scene::ScopedIterationGuard guard(*m_Scene);
                const auto& gameObjects = m_Scene->GetGameObjects();
                for (size_t i = 0; i < gameObjects.size(); ++i) {
                    auto& go = gameObjects[i];
                    if (go->IsActiveInHierarchy()) {
                        go->FixedUpdate(fixedDt);
                    }
                }
            }

            // Step Box2D, sync ONLY moved bodies via b2World_GetBodyEvents, then dispatch contact/sensor events
            if (b2World_IsValid(worldId)) {
                b2World_Step(worldId, fixedDt, 4);

                const b2BodyEvents bodyEvents = b2World_GetBodyEvents(worldId);
                for (int i = 0; i < bodyEvents.moveCount; ++i) {
                    SyncMovedBodyTransform(bodyEvents.moveEvents[i]);
                }

                ProcessPhysicsEvents();
            }

            // Pass 3: Dense vertical elevation integration & deduplicated airborne pass-through checks
            IntegrateVerticalElevation(fixedDt);

            m_PhysicsAccumulator -= fixedDt;
        }

        m_PhysicsAlpha = (fixedDt > 0.0f) ? (m_PhysicsAccumulator / fixedDt) : 0.0f;
    } else {
        m_PhysicsAccumulator = 0.0f;
        m_PhysicsAlpha = 0.0f;
    }
}

void PhysicsWorld2D::UpdateNonRuntime(float deltaTime) {
    UpdateCollidersAndGravity(deltaTime);
    ResolveEntityCollisions();
    ResolveTilemapCollisions();
}

void PhysicsWorld2D::EvaluateVerticalEntry(PhysicsBodyEntry& entry, float fixedDt, bool evaluateWorldY) noexcept {
    entry.pendingAirborneTransition = 0;
    entry.pendingTransformDirty = false;

    GameObject* go = entry.gameObject;
    Transform* transform = entry.transform;
    Rigidbody2D* rb2d = entry.rigidbody;
    if (!go || !transform || !rb2d || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) {
        return;
    }
    if (rb2d->Type != Rigidbody2D::BodyType::Dynamic || rb2d->IsStasisFrozen()) {
        return;
    }

    if (rb2d->UseGravityY) {
        const float prevY = transform->position.y;
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
        entry.pendingTransformDirty = (transform->position.y != prevY);
    }

    if (rb2d->EnableAirbornePassThrough) {
        if (!evaluateWorldY && transform->GetParent()) {
            // Defer parented world-Y evaluation safely to sequential main-thread phase
            return;
        }
        const float effectiveY = (evaluateWorldY && transform->GetParent())
            ? transform->GetWorldPosition().y
            : transform->position.y;
        const bool shouldBeAirborne = (effectiveY > rb2d->AirborneClearanceHeight);
        const bool isTouchdown = (effectiveY <= rb2d->GroundLevelY);

        if (shouldBeAirborne && !rb2d->IsAirbornePassThroughActive()) {
            entry.pendingAirborneTransition = 1;
        } else if (isTouchdown && rb2d->IsAirbornePassThroughActive()) {
            entry.pendingAirborneTransition = -1;
        }
    }
}

void PhysicsWorld2D::IntegrateVerticalElevation(float fixedDt) {
    if (m_PhysicsBodies.empty()) return;

    constexpr size_t kParallelVerticalThreshold = 64;
    const bool runParallel = (m_PhysicsBodies.size() >= kParallelVerticalThreshold) &&
                             JobSystem::IsInitialized() &&
                             (JobSystem::GetWorkerCount() > 0);

    if (runParallel) {
        JobSystem::ParallelForBatched(size_t(0), m_PhysicsBodies.size(), [this, fixedDt](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                EvaluateVerticalEntry(m_PhysicsBodies[i], fixedDt, /*evaluateWorldY=*/false);
            }
        });
    } else {
        for (auto& entry : m_PhysicsBodies) {
            EvaluateVerticalEntry(entry, fixedDt, /*evaluateWorldY=*/true);
        }
    }

    // Main-thread apply pass: dirty flags, parented world-Y elevation checks, and Box2D shape filter writes
    for (auto& entry : m_PhysicsBodies) {
        if (entry.pendingTransformDirty && entry.transform) {
            entry.transform->SetDirty();
            entry.pendingTransformDirty = false;
        }

        GameObject* go = entry.gameObject;
        Transform* transform = entry.transform;
        Rigidbody2D* rb2d = entry.rigidbody;
        if (!go || !transform || !rb2d || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) {
            continue;
        }

        if (runParallel && rb2d->EnableAirbornePassThrough && rb2d->Type == Rigidbody2D::BodyType::Dynamic &&
            !rb2d->IsStasisFrozen() && transform->GetParent()) {
            const float worldY = transform->GetWorldPosition().y;
            if (worldY > rb2d->AirborneClearanceHeight && !rb2d->IsAirbornePassThroughActive()) {
                entry.pendingAirborneTransition = 1;
            } else if (worldY <= rb2d->GroundLevelY && rb2d->IsAirbornePassThroughActive()) {
                entry.pendingAirborneTransition = -1;
            }
        }

        if (entry.pendingAirborneTransition != 0) {
            const bool airborne = (entry.pendingAirborneTransition > 0);
            entry.pendingAirborneTransition = 0;
            rb2d->SetAirbornePassThroughActive(airborne);
            ApplyAirbornePassThroughFilter(go, airborne);
        }
    }
}

void PhysicsWorld2D::ApplyAirbornePassThroughFilter(GameObject* go, bool airborne) {
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

void PhysicsWorld2D::ProcessPhysicsEvents() {
    if (!m_IsRuntimeRunning || !m_Scene) return;
    b2WorldId worldId = b2LoadWorldId(m_PhysicsWorldId);
    if (!b2World_IsValid(worldId)) return;

    b2ContactEvents contacts = b2World_GetContactEvents(worldId);
    b2SensorEvents sensors = b2World_GetSensorEvents(worldId);

    if (contacts.hitCount == 0 && contacts.beginCount == 0 && contacts.endCount == 0 &&
        sensors.beginCount == 0 && sensors.endCount == 0) {
        return;
    }

    // Guard entity lifecycle so DestroyGameObject calls during event dispatch defer to lifecycle queues
    Scene::ScopedIterationGuard guard(*m_Scene);

    m_ContactedPairsScratch.clear();

    auto makePair = [](b2ShapeId a, b2ShapeId b) noexcept -> ShapePairKey {
        uint64_t idA = b2StoreShapeId(a);
        uint64_t idB = b2StoreShapeId(b);
        if (idA > idB) std::swap(idA, idB);
        return ShapePairKey{ idA, idB };
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

        m_ContactedPairsScratch.push_back(makePair(hit.shapeIdA, hit.shapeIdB));

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

        const ShapePairKey pair = makePair(begin.shapeIdA, begin.shapeIdB);
        if (std::find(m_ContactedPairsScratch.begin(), m_ContactedPairsScratch.end(), pair) != m_ContactedPairsScratch.end()) {
            continue;
        }
        m_ContactedPairsScratch.push_back(pair);

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

void PhysicsWorld2D::UpdateCollidersAndGravity(float deltaTime) {
    if (!m_Scene) return;
    m_DynamicColliders.clear();
    m_TilemapScratch.clear();

    size_t staticCount = 0;
    for (auto& gameObject : m_Scene->GetGameObjects()) {
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

        for (auto& gameObject : m_Scene->GetGameObjects()) {
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

void PhysicsWorld2D::ResolveEntityCollisions() {
    auto resolveCollisionPair = [&](const ColliderCacheEntry& entryA, const ColliderCacheEntry& entryB) {
        auto* cA = entryA.collider;
        auto* cB = entryB.collider;
        auto* tA = entryA.transform;
        auto* tB = entryB.transform;
        if (!tA || !tB) return;

        float aLeft  = tA->position.x + cA->offset.x - (cA->size.x * tA->scale.x) / 2.0f;
        float aRight = tA->position.x + cA->offset.x + (cA->size.x * tA->scale.x) / 2.0f;
        float aBack  = tA->position.z + cA->offset.y - (cA->size.y * tA->scale.z) / 2.0f;
        float aFront = tA->position.z + cA->offset.y + (cA->size.y * tA->scale.z) / 2.0f;

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

void PhysicsWorld2D::ResolveTilemapCollisions() {
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

void PhysicsWorld2D::OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    if (m_Scene) {
        PhysicsQueries::OverlapCircleXZ(*m_Scene, centerXZ, radius, maskBits, outHits);
    }
}

void PhysicsWorld2D::OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    if (m_Scene) {
        PhysicsQueries::OverlapArcXZ(*m_Scene, originXZ, forwardXZ, radius, halfAngleDegrees, maskBits, outHits);
    }
}

bool PhysicsWorld2D::RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const {
    if (m_Scene) {
        return PhysicsQueries::RaycastClosestXZ(*m_Scene, startXZ, endXZ, maskBits, outHit);
    }
    return false;
}

} // namespace Engine
