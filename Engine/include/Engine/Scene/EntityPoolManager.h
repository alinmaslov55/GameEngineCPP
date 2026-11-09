#ifndef SCENE_ENTITYPOOLMANAGER_H
#define SCENE_ENTITYPOOLMANAGER_H

#include "Engine/Scene/GameObject.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/vec3.hpp>

namespace Engine {

class Scene;
class Prefab;

/// @brief Dedicated object pool manager for high-frequency GameObject prewarming,
/// acquisition, and release with deep subtree lifecycle reset propagation and O(1) tracking.
class EntityPoolManager {
public:
    explicit EntityPoolManager(Scene* scene) noexcept : m_Scene(scene) {}
    ~EntityPoolManager() = default;

    EntityPoolManager(const EntityPoolManager&) = delete;
    EntityPoolManager& operator=(const EntityPoolManager&) = delete;
    EntityPoolManager(EntityPoolManager&&) noexcept = default;
    EntityPoolManager& operator=(EntityPoolManager&&) noexcept = default;

    void PrewarmPool(const std::string& poolKey, const Prefab& prefab, uint32_t count);
    GameObject* AcquirePooled(const std::string& poolKey, const Prefab& prefab, const glm::vec3& worldPos = glm::vec3(0.0f));
    void ReleaseToPool(const std::string& poolKey, GameObject* instance);
    void ReleaseToPool(GameObject* instance);
    void PropagatePoolAcquire(GameObject* root);
    void PropagatePoolRelease(GameObject* root);

    [[nodiscard]] size_t GetPoolFreeCount(const std::string& poolKey) const noexcept;
    [[nodiscard]] size_t GetPoolTotalCount(const std::string& poolKey) const noexcept;
    void ClearPool(const std::string& poolKey);
    void ClearAllPools();
    void OnGameObjectDestroyed(GameObject* instance);

private:
    struct EntityPool {
        std::vector<GameObject*> freeList;
        std::vector<GameObject*> allInstances;
    };

    Scene* m_Scene{ nullptr };
    std::unordered_map<std::string, EntityPool> m_Pools;
};

} // namespace Engine

#endif // SCENE_ENTITYPOOLMANAGER_H
