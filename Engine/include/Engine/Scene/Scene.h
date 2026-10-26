#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "Engine/Scene/GameObject.h"
#include "Engine/Renderer/OrthographicCamera.h"

namespace Engine {
class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();

    GameObject* CreateGameObject(const std::string& name);
    GameObject* CreateGameObjectWithUUID(uint64_t uuid, const std::string& name);
    void DestroyGameObject(GameObject* gameObject);

    void Update(float deltaTime);
    void Render(const OrthographicCamera* overrideCamera = nullptr);
    void OnViewportResize(uint32_t width, uint32_t height);

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }
    OrthographicCamera& GetCamera() { return m_Camera; }
    const OrthographicCamera& GetActiveCamera() const;

    float GetOrthographicSize() const { return m_OrthographicSize; }
    void SetOrthographicSize(float size);

    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return m_GameObjects; }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    OrthographicCamera m_Camera;
    uint32_t m_ViewportWidth = 1280;
    uint32_t m_ViewportHeight = 720;
    float m_OrthographicSize = 1.8f;
};
} // namespace Engine

#endif // SCENE_SCENE_H
