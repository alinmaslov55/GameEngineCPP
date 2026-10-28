#include "Engine/Scene/SceneManager.h"
#include "Engine/Utils/Logger.h"

namespace Engine {
std::shared_ptr<Scene> SceneManager::s_ActiveScene{nullptr};

void SceneManager::LoadScene(std::shared_ptr<Scene> scene) {
    s_ActiveScene = scene;
    if (s_ActiveScene) {
        ENGINE_CORE_INFO("SceneManager: Loaded scene '{}'", s_ActiveScene->GetName());
    } else {
        ENGINE_CORE_WARN("SceneManager: Loaded null scene");
    }
}

void SceneManager::Shutdown() {
    if (s_ActiveScene) {
        ENGINE_CORE_INFO("SceneManager: Unloading active scene '{}'", s_ActiveScene->GetName());
        if (s_ActiveScene->IsRuntimeRunning()) {
            s_ActiveScene->OnRuntimeStop();
        }
        s_ActiveScene.reset();
    }
}

std::shared_ptr<Scene> SceneManager::GetActiveScene() {
    return s_ActiveScene;
}

void SceneManager::Update(float deltaTime) {
    ENGINE_CORE_TRACE("SceneManager::Update");
    if (s_ActiveScene) {
        s_ActiveScene->Update(deltaTime);
    }
}

void SceneManager::UpdateRuntime(float deltaTime) {
    ENGINE_CORE_TRACE("SceneManager::UpdateRuntime");
    if (s_ActiveScene) {
        s_ActiveScene->UpdateRuntime(deltaTime);
    }
}

void SceneManager::OnRuntimeStart() {
    if (s_ActiveScene) {
        s_ActiveScene->OnRuntimeStart();
    }
}

void SceneManager::OnRuntimeStop() {
    if (s_ActiveScene) {
        s_ActiveScene->OnRuntimeStop();
    }
}

void SceneManager::Render(const PerspectiveCamera* overrideCamera) {
    ENGINE_CORE_TRACE("SceneManager::Render");
    if (s_ActiveScene) {
        s_ActiveScene->Render(overrideCamera);
    }
}
} // namespace Engine
