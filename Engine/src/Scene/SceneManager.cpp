#include "Engine/Scene/SceneManager.h"

namespace Engine {
std::shared_ptr<Scene> SceneManager::s_ActiveScene{nullptr};

void SceneManager::LoadScene(std::shared_ptr<Scene> scene) {
    s_ActiveScene = scene;
}

std::shared_ptr<Scene> SceneManager::GetActiveScene() {
    return s_ActiveScene;
}

void SceneManager::Update(float deltaTime) {
    if (s_ActiveScene) {
        s_ActiveScene->Update(deltaTime);
    }
}

void SceneManager::Render() {
    if (s_ActiveScene) {
        s_ActiveScene->Render();
    }
}
} // namespace Engine
