#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include <string>
#include <vector>
#include <memory>
#include "Engine/Scene/GameObject.h"

namespace Engine {
class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene() = default;

    GameObject* CreateGameObject(const std::string& name);
    void DestroyGameObject(GameObject* gameObject);

    void Update(float deltaTime);
    void Render();

    const std::string& GetName() const { return m_Name; }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<GameObject>> m_GameObjects;
};
} // namespace Engine

#endif // SCENE_SCENE_H
