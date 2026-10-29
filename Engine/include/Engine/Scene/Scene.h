#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/PerspectiveCamera.h"

namespace Engine {

class BoxCollider2D;

class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();

    GameObject* CreateGameObject(const std::string& name);
    GameObject* CreateGameObjectWithUUID(uint64_t uuid, const std::string& name);
    void DestroyGameObject(GameObject* gameObject);

    void Update(float deltaTime);
    void Render(const PerspectiveCamera* overrideCamera = nullptr);
    void OnViewportResize(uint32_t width, uint32_t height);

    // Box2D physics simulation lifecycle
    void OnRuntimeStart();
    void OnRuntimeStop();
    void UpdateRuntime(float deltaTime);
    bool IsRuntimeRunning() const { return m_IsRuntimeRunning; }

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    PerspectiveCamera& GetCamera() { return m_Camera; }
    const PerspectiveCamera& GetCamera() const { return m_Camera; }
    const PerspectiveCamera& GetActiveCamera() const;

    float GetOrthographicSize() const { return m_OrthographicSize; }
    void SetOrthographicSize(float size);

    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return m_GameObjects; }

private:
    struct ColliderCacheEntry {
        GameObject* gameObject = nullptr;
        Transform* transform = nullptr;
        BoxCollider2D* collider = nullptr;
    };

    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    PerspectiveCamera m_Camera;
    uint32_t m_ViewportWidth = 1280;
    uint32_t m_ViewportHeight = 720;
    float m_OrthographicSize = 5.0f;

    bool m_IsRuntimeRunning = false;
    uint32_t m_PhysicsWorldId = 0;

    // Hot-path scratch buffers to prevent per-frame dynamic heap allocations
    std::vector<GameObject*> m_StandeeScratch;
    std::vector<std::pair<GameObject*, glm::vec3>> m_FoliageScratch;
    std::vector<ColliderCacheEntry> m_ColliderScratch;
    std::vector<GameObject*> m_TilemapScratch;
};
} // namespace Engine

#endif // SCENE_SCENE_H
