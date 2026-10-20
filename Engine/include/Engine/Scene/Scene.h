#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

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
    void DestroyGameObject(GameObject* gameObject);

    void Update(float deltaTime);
    void Render();

    const std::string& GetName() const { return m_Name; }
    OrthographicCamera& GetCamera() { return m_Camera; }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
    OrthographicCamera m_Camera;
};
} // namespace Engine

#endif // SCENE_SCENE_H