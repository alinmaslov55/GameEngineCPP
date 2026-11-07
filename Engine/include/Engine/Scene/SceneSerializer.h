#ifndef SCENE_SCENESERIALIZER_H
#define SCENE_SCENESERIALIZER_H

#include "Engine/Scene/Scene.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace Engine {
class SceneSerializer {
public:
    SceneSerializer(const std::shared_ptr<Scene>& scene);

    bool Serialize(const std::string& filepath);
    bool Deserialize(const std::string& filepath);

    // In-memory serialization for Play/Edit mode cloning and testing
    std::string SerializeToString() const;
    bool DeserializeFromString(const std::string& jsonString);

    // Reusable single-entity serialization & component deserialization (used by Prefab & Scene)
    static nlohmann::json SerializeEntity(const GameObject* go);
    static void DeserializeEntityComponents(GameObject* go, const nlohmann::json& componentsJson, const std::string& name = "");

private:
    std::shared_ptr<Scene> m_Scene;
};
} // namespace Engine

#endif // SCENE_SCENESERIALIZER_H