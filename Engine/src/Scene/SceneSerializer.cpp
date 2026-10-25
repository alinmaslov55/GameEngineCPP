#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Utils/Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace Engine {
SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
    : m_Scene(scene) {}

bool SceneSerializer::Serialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Serialize failed: Scene pointer is null");
        return false;
    }

    ENGINE_CORE_INFO("Serializing scene '{}' to '{}'", m_Scene->GetName(), filepath);

    json sceneJson;
    sceneJson["SceneName"] = m_Scene->GetName();
    sceneJson["GameObjects"] = json::array();

    for (const auto& go : m_Scene->GetGameObjects()) {
        json entityJson;
        entityJson["Name"] = go->GetName();

        // Serialize Transform
        auto transform = go->GetComponent<Transform>();
        if (transform) {
            entityJson["Components"]["Transform"] = {
                {"Position", {transform->position.x, transform->position.y, transform->position.z}},
                {"Rotation", {transform->rotation.x, transform->rotation.y, transform->rotation.z}},
                {"Scale",    {transform->scale.x, transform->scale.y, transform->scale.z}}
            };
        }

        // Serialize SpriteRenderer
        auto spriteRenderer = go->GetComponent<SpriteRenderer>();
        if (spriteRenderer) {
            // In a production engine, you would save the Asset Handle / Path here, not the raw texture data
            entityJson["Components"]["SpriteRenderer"] = {
                {"Color", {spriteRenderer->GetColor().r, spriteRenderer->GetColor().g, spriteRenderer->GetColor().b, spriteRenderer->GetColor().a}}
            };
        }

        sceneJson["GameObjects"].push_back(entityJson);
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene serialization: '{}'", filepath);
        return false;
    }

    // 4 spaces of indentation
    file << sceneJson.dump(4);
    file.close();
    ENGINE_CORE_INFO("Scene serialized successfully to '{}' ({} GameObjects)", filepath, sceneJson["GameObjects"].size());
    return true;
}

bool SceneSerializer::Deserialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Deserialize failed: Scene pointer is null");
        return false;
    }

    ENGINE_CORE_INFO("Deserializing scene from '{}'", filepath);

    std::ifstream file(filepath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene deserialization: '{}'", filepath);
        return false;
    }

    json sceneJson;
    try {
        file >> sceneJson;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("Failed to parse scene JSON from '{}': {}", filepath, e.what());
        return false;
    }

    size_t loadedCount = 0;
    if (sceneJson.contains("GameObjects")) {
        for (const auto& entityJson : sceneJson["GameObjects"]) {
            std::string name = entityJson["Name"].get<std::string>();
            GameObject* go = m_Scene->CreateGameObject(name);
            loadedCount++;

            auto components = entityJson["Components"];

            // Deserialize Transform
            if (components.contains("Transform")) {
                auto transform = go->GetComponent<Transform>(); // Transform is attached by default
                auto pos = components["Transform"]["Position"];
                auto rot = components["Transform"]["Rotation"];
                auto scale = components["Transform"]["Scale"];

                transform->position = {pos[0], pos[1], pos[2]};
                transform->rotation = {rot[0], rot[1], rot[2]};
                transform->scale    = {scale[0], scale[1], scale[2]};
            }

            // Deserialize SpriteRenderer
            if (components.contains("SpriteRenderer")) {
                auto sprite = go->AddComponent<SpriteRenderer>();
                auto color = components["SpriteRenderer"]["Color"];
                sprite->SetColor({color[0], color[1], color[2], color[3]});
            }
        }
    }

    ENGINE_CORE_INFO("Scene deserialized successfully from '{}' ({} GameObjects loaded)", filepath, loadedCount);
    return true;
}
} // namespace Engine

} // namespace Engine