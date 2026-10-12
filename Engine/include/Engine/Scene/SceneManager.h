#ifndef SCENE_SCENE_MANAGER_H
#define SCENE_SCENE_MANAGER_H
#include <memory>
#include <string>
#include "Engine/Scene/Scene.h"

namespace Engine {
class SceneManager {
public:
    static void LoadScene(std::shared_ptr<Scene> scene);
    static std::shared_ptr<Scene> GetActiveScene();

    static void Update(float deltaTime);
    static void Render();

private:
    static std::shared_ptr<Scene> s_ActiveScene;
};
} // namespace Engine

#endif // SCENE_SCENE_MANAGER_H
