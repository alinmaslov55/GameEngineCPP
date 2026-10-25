#ifndef SCENE_SCENESERIALIZER_H
#define SCENE_SCENESERIALIZER_H
#include "Engine/Scene/Scene.h"
#include <memory>
#include <string>

namespace Engine {
class SceneSerializer {
public:
    SceneSerializer(const std::shared_ptr<Scene>& scene);

    bool Serialize(const std::string& filepath);
    
    bool Deserialize(const std::string& filepath);

private:
    std::shared_ptr<Scene> m_Scene;
};
} // namespace Engine
#endif // SCENE_SCENESERIALIZER_H