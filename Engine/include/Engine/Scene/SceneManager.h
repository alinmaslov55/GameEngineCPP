#ifndef SCENE_SCENE_MANAGER_H
#define SCENE_SCENE_MANAGER_H
#include <memory>
#include <string>
#include "Engine/Scene/Scene.h"

namespace Engine {
class SceneManager {
public:
    static void LoadScene(std::shared_ptr<Scene> scene);
    static void Shutdown();
    static std::shared_ptr<Scene> GetActiveScene();

    static void Update(float deltaTime);
    static void UpdateRuntime(float deltaTime);
    static void OnRuntimeStart();
    static void OnRuntimeStop();
    static void Render(const OrthographicCamera* overrideCamera = nullptr);

private:
    static std::shared_ptr<Scene> s_ActiveScene;
};
} // namespace Engine

#endif // SCENE_SCENE_MANAGER_H
