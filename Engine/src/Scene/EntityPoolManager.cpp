#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

namespace Engine {

EntityPoolManager::EntityPoolManager(Scene* scene)
    : m_Scene(scene) {
    m_TempChildrenScratch.reserve(64);
}

void EntityPoolManager::PropagatePoolTree(GameObject* root, bool isAcquire) {
    if (!root) return;
    if (isAcquire) {
        root->OnPoolAcquire();
    } else {
        root->OnPoolRelease();
    }
    if (auto* transform = root->GetTransform()) {
        if (isAcquire) {
            transform->SnapshotPrevious();
        }
        transform->ForEachChildBuffered(m_TempChildrenScratch, [this, isAcquire](Transform* child) {
            if (auto* childGo = child->GetGameObject()) {
                PropagatePoolTree(childGo, isAcquire);
            }
        });
    }
}

void EntityPoolManager::PropagatePoolAcquire(GameObject* root) {
    PropagatePoolTree(root, true);
}

void EntityPoolManager::PropagatePoolRelease(GameObject* root) {
    PropagatePoolTree(root, false);
}

void EntityPoolManager::PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count) {
    if (!m_Scene) {
        ENGINE_CORE_WARN("EntityPoolManager::PrewarmPool('{}'): Scene is null", poolKey);
        return;
    }
    m_Scene->AssertMainThreadAffinity("EntityPoolManager::PrewarmPool");
    auto& pool = m_Pools[poolKey];
    pool.freeList.reserve(pool.freeList.size() + count);
    pool.allInstances.reserve(pool.allInstances.size() + count);

    uint32_t spawned = 0;
    for (uint32_t i = 0; i < count; ++i) {
        GameObject* instance = m_Scene->InstantiatePrefab(prefab, glm::vec3(0.0f));
        if (instance) {
            instance->SetPoolKey(poolKey);
            instance->SetInFreeList(true);
            PropagatePoolRelease(instance);
            instance->SetActive(false);
            pool.freeList.push_back(instance);
            pool.allInstances.push_back(instance);
            ++spawned;
        }
    }
    ENGINE_CORE_INFO("EntityPoolManager::PrewarmPool('{}'): Prewarmed {}/{} instance(s) (total={})",
                     poolKey, spawned, count, pool.allInstances.size());
}

GameObject* EntityPoolManager::AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("EntityPoolManager::AcquirePooled('{}'): Scene is null", poolKey);
        return nullptr;
    }
    m_Scene->AssertMainThreadAffinity("EntityPoolManager::AcquirePooled");
    auto& pool = m_Pools[poolKey];
    GameObject* instance = nullptr;

    while (!pool.freeList.empty()) {
        GameObject* candidate = pool.freeList.back();
        pool.freeList.pop_back();
        if (candidate) {
            candidate->SetInFreeList(false);
            if (!candidate->IsPendingDestroy()) {
                instance = candidate;
                break;
            }
        }
    }

    const bool recycled = (instance != nullptr);
    if (!instance) {
        instance = m_Scene->InstantiatePrefab(prefab, worldPos);
        if (instance) {
            instance->SetPoolKey(poolKey);
            instance->SetInFreeList(false);
            pool.allInstances.push_back(instance);
        } else {
            ENGINE_CORE_ERROR("EntityPoolManager::AcquirePooled('{}'): Prefab instantiation failed", poolKey);
        }
    } else {
        if (auto* transform = instance->GetTransform()) {
            transform->SetWorldPosition(worldPos);
        }
    }

    if (instance) {
        PropagatePoolAcquire(instance);
        instance->SetActive(true);
        ENGINE_CORE_TRACE("EntityPoolManager::AcquirePooled('{}'): {} UUID {} (free={}, total={})",
                          poolKey, recycled ? "Reused" : "Allocated", instance->GetUUID(),
                          pool.freeList.size(), pool.allInstances.size());
    }

    return instance;
}

void EntityPoolManager::ReleaseToPool(const std::string& poolKey, GameObject* instance) {
    if (m_Scene) {
        m_Scene->AssertMainThreadAffinity("EntityPoolManager::ReleaseToPool");
    }
    if (!instance) {
        ENGINE_CORE_WARN("EntityPoolManager::ReleaseToPool('{}'): Attempted to release null instance", poolKey);
        return;
    }

    PropagatePoolRelease(instance);
    instance->SetActive(false);

    if (!instance->IsInFreeList()) {
        instance->SetInFreeList(true);
        auto& pool = m_Pools[poolKey];
        pool.freeList.push_back(instance);
        ENGINE_CORE_TRACE("EntityPoolManager::ReleaseToPool('{}'): Released UUID {} (free={}, total={})",
                          poolKey, instance->GetUUID(), pool.freeList.size(), pool.allInstances.size());
    }
}

void EntityPoolManager::ReleaseToPool(GameObject* instance) {
    if (instance && instance->IsPooled()) {
        ReleaseToPool(instance->GetPoolKey(), instance);
    }
}

size_t EntityPoolManager::GetPoolFreeCount(const std::string& poolKey) const noexcept {
    auto it = m_Pools.find(poolKey);
    if (it == m_Pools.end()) return 0;
    return it->second.freeList.size();
}

size_t EntityPoolManager::GetPoolTotalCount(const std::string& poolKey) const noexcept {
    auto it = m_Pools.find(poolKey);
    if (it == m_Pools.end()) return 0;
    return it->second.allInstances.size();
}

void EntityPoolManager::ClearPool(const std::string& poolKey) {
    if (!m_Scene) return;
    m_Scene->AssertMainThreadAffinity("EntityPoolManager::ClearPool");
    auto it = m_Pools.find(poolKey);
    if (it == m_Pools.end()) return;

    ENGINE_CORE_INFO("EntityPoolManager::ClearPool('{}'): Destroying {} pooled instance(s)",
                     poolKey, it->second.allInstances.size());
    auto instances = it->second.allInstances;
    for (auto* instance : instances) {
        if (instance && !instance->IsPendingDestroy()) {
            m_Scene->DestroyGameObject(instance);
        }
    }
    m_Pools.erase(poolKey);
}

void EntityPoolManager::ClearAllPools() {
    if (!m_Scene) return;
    m_Scene->AssertMainThreadAffinity("EntityPoolManager::ClearAllPools");
    if (!m_Pools.empty()) {
        ENGINE_CORE_INFO("EntityPoolManager::ClearAllPools: Clearing {} pool(s)", m_Pools.size());
    }
    auto pools = m_Pools;
    for (auto& [key, pool] : pools) {
        for (auto* instance : pool.allInstances) {
            if (instance && !instance->IsPendingDestroy()) {
                m_Scene->DestroyGameObject(instance);
            }
        }
    }
    m_Pools.clear();
}

void EntityPoolManager::OnGameObjectDestroyed(GameObject* instance) {
    if (!instance || !instance->IsPooled()) return;
    auto poolIt = m_Pools.find(instance->GetPoolKey());
    if (poolIt != m_Pools.end()) {
        std::erase(poolIt->second.freeList, instance);
        std::erase(poolIt->second.allInstances, instance);
    }
    instance->SetInFreeList(false);
}

} // namespace Engine
