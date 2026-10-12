#include "Engine/Scene/Scene.h"
#include <algorithm>

namespace Engine {
Scene::Scene(const std::string& name) : m_Name(name) {}

GameObject* Scene::CreateGameObject(const std::string& name) {
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

void Scene::DestroyGameObject(GameObject* gameObject) {
    m_GameObjects.erase(
        std::remove_if(m_GameObjects.begin(), m_GameObjects.end(),
            [gameObject](const std::unique_ptr<GameObject>& ptr) {
                return ptr.get() == gameObject;
            }),
        m_GameObjects.end()
    );
}

void Scene::Update(float deltaTime) {
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }
}

// @todo Rendering logic will iterate over GameObjects
void Scene::Render() {
}
} // namespace Engine
