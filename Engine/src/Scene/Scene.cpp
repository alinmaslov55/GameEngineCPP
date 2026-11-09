#include "Engine/Scene/Scene.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Component.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <utility>

namespace Engine {

// Verify cache alignment and layout identity between Scene::Box2DTask and PhysicsWorld2D::Box2DTask
static_assert(sizeof(Scene::Box2DTask) == sizeof(PhysicsWorld2D::Box2DTask), "Scene::Box2DTask and PhysicsWorld2D::Box2DTask must match in size");
static_assert(alignof(Scene::Box2DTask) == alignof(PhysicsWorld2D::Box2DTask), "Scene::Box2DTask and PhysicsWorld2D::Box2DTask must match in alignment");

Scene::Scene(const std::string& name)
    : m_Name(name),
      m_PhysicsWorld(std::make_unique<PhysicsWorld2D>(this)),
      m_PoolManager(std::make_unique<EntityPoolManager>(this)),
      m_Renderer(std::make_unique<SceneRenderer>()) {
    ENGINE_CORE_INFO("Scene created: '{}'", m_Name);
}

Scene::~Scene() {
    if (m_PhysicsWorld && m_PhysicsWorld->IsRuntimeRunning()) {
        m_PhysicsWorld->OnRuntimeStop();
    }
    if (m_PoolManager) {
        m_PoolManager->ClearAllPools();
    }
    m_PendingDestroyQueue.clear();
    m_PendingSpawnQueue.clear();
    m_EntityUUIDMap.clear();
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

Scene::Scene(Scene&&) noexcept = default;
Scene& Scene::operator=(Scene&&) noexcept = default;

// ============================================================================
// Subsystem Accessors
// ============================================================================

PhysicsWorld2D& Scene::GetPhysicsWorld() noexcept { return *m_PhysicsWorld; }
const PhysicsWorld2D& Scene::GetPhysicsWorld() const noexcept { return *m_PhysicsWorld; }
EntityPoolManager& Scene::GetPoolManager() noexcept { return *m_PoolManager; }
const EntityPoolManager& Scene::GetPoolManager() const noexcept { return *m_PoolManager; }
SceneRenderer& Scene::GetRenderer() noexcept { return *m_Renderer; }
const SceneRenderer& Scene::GetRenderer() const noexcept { return *m_Renderer; }

// ============================================================================
// Entity Lifecycle and UUID Indexing
// ============================================================================

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
        if (m_PhysicsWorld) m_PhysicsWorld->InvalidateSpatialGrid();
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
        if (m_PhysicsWorld) m_PhysicsWorld->InvalidateSpatialGrid();
    }
    return rawPtr;
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

    if (m_PhysicsWorld) {
        m_PhysicsWorld->DisableRuntimeBody(gameObject);
    }

    if (m_PoolManager) {
        m_PoolManager->OnGameObjectDestroyed(gameObject);
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

                    if (m_PhysicsWorld && m_PhysicsWorld->IsRuntimeRunning()) {
                        m_PhysicsWorld->UnregisterRuntimeBody(pending.get());
                    }

                    if (pending->GetComponent<BoxCollider2D>() || pending->GetComponent<CircleCollider2D>() ||
                        pending->GetComponent<CapsuleCollider2D>() || pending->GetComponent<Tilemap>() ||
                        pending->GetComponent<TerrainComponent>()) {
                        if (m_PhysicsWorld) m_PhysicsWorld->InvalidateSpatialGrid();
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
                    if (m_PhysicsWorld && m_PhysicsWorld->IsRuntimeRunning()) {
                        m_PhysicsWorld->RegisterRuntimeBody(pending.get());
                    }
                    m_GameObjects.push_back(std::move(pending));
                    if (m_PhysicsWorld) m_PhysicsWorld->InvalidateSpatialGrid();
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

                if (m_PhysicsWorld && m_PhysicsWorld->IsRuntimeRunning()) {
                    m_PhysicsWorld->UnregisterRuntimeBody(go);
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

            std::erase_if(m_GameObjects, [](const std::unique_ptr<GameObject>& ptr) {
                return ptr->IsDestroyed();
            });

            if (colliderDestroyed && m_PhysicsWorld) {
                m_PhysicsWorld->InvalidateSpatialGrid();
            }
        }
    }

    if (passes >= kMaxFlushPasses) {
        ENGINE_CORE_ERROR("Scene '{}': Reached max flush passes ({}), possible infinite lifecycle recursion", m_Name, kMaxFlushPasses);
    }
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

// ============================================================================
// Prefab Instantiation
// ============================================================================

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
    if (m_PhysicsWorld && m_PhysicsWorld->IsRuntimeRunning()) {
        for (const auto& [uuid, go] : createdEntities) {
            m_PhysicsWorld->RegisterRuntimeBody(go);
        }
    }

    return rootGameObject;
}

// ============================================================================
// Facade: Object Pooling (delegates to EntityPoolManager)
// ============================================================================

void Scene::PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count) {
    if (m_PoolManager) m_PoolManager->PrewarmPool(poolKey, prefab, count);
}

GameObject* Scene::AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos) {
    return m_PoolManager ? m_PoolManager->AcquirePooled(poolKey, prefab, worldPos) : nullptr;
}

void Scene::ReleaseToPool(const std::string& poolKey, GameObject* instance) {
    if (m_PoolManager) m_PoolManager->ReleaseToPool(poolKey, instance);
}

void Scene::ReleaseToPool(GameObject* instance) {
    if (m_PoolManager) m_PoolManager->ReleaseToPool(instance);
}

void Scene::PropagatePoolAcquire(GameObject* root) {
    if (m_PoolManager) m_PoolManager->PropagatePoolAcquire(root);
}

void Scene::PropagatePoolRelease(GameObject* root) {
    if (m_PoolManager) m_PoolManager->PropagatePoolRelease(root);
}

size_t Scene::GetPoolFreeCount(const std::string& poolKey) const noexcept {
    return m_PoolManager ? m_PoolManager->GetPoolFreeCount(poolKey) : 0;
}

size_t Scene::GetPoolTotalCount(const std::string& poolKey) const noexcept {
    return m_PoolManager ? m_PoolManager->GetPoolTotalCount(poolKey) : 0;
}

void Scene::ClearPool(const std::string& poolKey) {
    if (m_PoolManager) m_PoolManager->ClearPool(poolKey);
}

void Scene::ClearAllPools() {
    if (m_PoolManager) m_PoolManager->ClearAllPools();
}

// ============================================================================
// Lifecycle Loop & Facade: Rendering
// ============================================================================

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

    if (m_PhysicsWorld && !m_PhysicsWorld->IsRuntimeRunning()) {
        m_PhysicsWorld->UpdateNonRuntime(deltaTime);
    }
}

void Scene::Render(const PerspectiveCamera* overrideCamera) {
    if (m_Renderer) {
        m_Renderer->Render(*this, overrideCamera);
    }
}

void Scene::OnViewportResize(uint32_t width, uint32_t height) {
    if (m_Renderer) {
        m_Renderer->OnViewportResize(*this, width, height);
    }
}

PerspectiveCamera& Scene::GetCamera() noexcept {
    return m_Renderer->GetCamera();
}

const PerspectiveCamera& Scene::GetCamera() const noexcept {
    return m_Renderer->GetCamera();
}

const PerspectiveCamera& Scene::GetActiveCamera() const {
    return m_Renderer->GetActiveCamera(*this);
}

float Scene::GetOrthographicSize() const noexcept {
    return m_Renderer->GetOrthographicSize();
}

void Scene::SetOrthographicSize(float size) {
    if (m_Renderer) {
        m_Renderer->SetOrthographicSize(size);
    }
}

// ============================================================================
// Facade: Physics Simulation Lifecycle & Queries (delegates to PhysicsWorld2D)
// ============================================================================

void Scene::OnRuntimeStart() {
    if (m_PhysicsWorld) m_PhysicsWorld->OnRuntimeStart();
}

void Scene::OnRuntimeStop() {
    if (m_PhysicsWorld) m_PhysicsWorld->OnRuntimeStop();
}

void Scene::UpdateRuntime(float deltaTime) {
    if (m_PhysicsWorld) {
        m_PhysicsWorld->UpdateRuntime(deltaTime);
    }
    Update(deltaTime);
}

bool Scene::IsRuntimeRunning() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->IsRuntimeRunning() : false;
}

uint32_t Scene::GetPhysicsWorldId() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetPhysicsWorldId() : 0;
}

float Scene::GetPhysicsAlpha() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetPhysicsAlpha() : 0.0f;
}

void Scene::ResetPhysicsAccumulator() noexcept {
    if (m_PhysicsWorld) m_PhysicsWorld->ResetPhysicsAccumulator();
}

void Scene::RegisterRuntimeBody(GameObject* go) {
    if (m_PhysicsWorld) m_PhysicsWorld->RegisterRuntimeBody(go);
}

void Scene::UnregisterRuntimeBody(GameObject* go) {
    if (m_PhysicsWorld) m_PhysicsWorld->UnregisterRuntimeBody(go);
}

void Scene::MarkPhysicsDirty(GameObject* go) {
    if (m_PhysicsWorld) m_PhysicsWorld->MarkPhysicsDirty(go);
}

size_t Scene::GetPendingPhysicsSyncCount() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetPendingPhysicsSyncCount() : 0;
}

void Scene::OverlapCircleXZ(glm::vec2 centerXZ, float radius, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    if (m_PhysicsWorld) m_PhysicsWorld->OverlapCircleXZ(centerXZ, radius, maskBits, outHits);
}

void Scene::OverlapArcXZ(glm::vec2 originXZ, glm::vec2 forwardXZ, float radius, float halfAngleDegrees, uint64_t maskBits, std::vector<GameObject*>& outHits) const {
    if (m_PhysicsWorld) m_PhysicsWorld->OverlapArcXZ(originXZ, forwardXZ, radius, halfAngleDegrees, maskBits, outHits);
}

bool Scene::RaycastClosestXZ(glm::vec2 startXZ, glm::vec2 endXZ, uint64_t maskBits, RaycastHitXZ& outHit) const {
    return m_PhysicsWorld ? m_PhysicsWorld->RaycastClosestXZ(startXZ, endXZ, maskBits, outHit) : false;
}

void Scene::InvalidateSpatialGrid() noexcept {
    if (m_PhysicsWorld) m_PhysicsWorld->InvalidateSpatialGrid();
}

bool Scene::IsSpatialGridDirty() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->IsSpatialGridDirty() : false;
}

size_t Scene::GetStaticGridCellCount() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetStaticGridCellCount() : 0;
}

size_t Scene::GetStaticColliderCount() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetStaticColliderCount() : 0;
}

uint64_t Scene::GetBox2DParallelTasksDispatched() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetBox2DParallelTasksDispatched() : 0;
}

uint64_t Scene::GetBox2DSerialTasksExecuted() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetBox2DSerialTasksExecuted() : 0;
}

uint64_t Scene::GetBox2DTotalTasks() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetBox2DTotalTasks() : 0;
}

const Scene::Box2DTask* Scene::GetBox2DTaskSlots() const noexcept {
    return m_PhysicsWorld ? reinterpret_cast<const Scene::Box2DTask*>(m_PhysicsWorld->GetBox2DTaskSlots()) : nullptr;
}

size_t Scene::GetBox2DTaskSlotCount() const noexcept {
    return m_PhysicsWorld ? m_PhysicsWorld->GetBox2DTaskSlotCount() : 0;
}

} // namespace Engine
