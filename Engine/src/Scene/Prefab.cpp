#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <filesystem>
#include <fstream>
#include <queue>
#include <vector>

namespace Engine {

Prefab::Prefab(nlohmann::json rootHierarchyJson) {
    if (rootHierarchyJson.contains("Entities") && rootHierarchyJson["Entities"].is_array()) {
        m_Json = std::move(rootHierarchyJson);
    } else if (rootHierarchyJson.is_array()) {
        m_Json["Format"] = "EnginePrefab";
        m_Json["Version"] = 1;
        m_Json["Entities"] = std::move(rootHierarchyJson);
    } else if (rootHierarchyJson.contains("Components")) {
        // Single entity object
        m_Json["Format"] = "EnginePrefab";
        m_Json["Version"] = 1;
        m_Json["Entities"] = nlohmann::json::array({ std::move(rootHierarchyJson) });
    } else {
        m_Json = std::move(rootHierarchyJson);
    }
}

bool Prefab::IsValid() const noexcept {
    return !m_Json.empty() && m_Json.contains("Entities") && m_Json["Entities"].is_array() && !m_Json["Entities"].empty();
}

std::shared_ptr<Prefab> Prefab::LoadFromFile(const std::string& path) {
    std::string resolvedPath = ResourceManager::ResolvePath(path);
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Prefab::LoadFromFile failed: Cannot open file '{}'", resolvedPath);
        return nullptr;
    }

    nlohmann::json parsedJson;
    try {
        file >> parsedJson;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("Prefab::LoadFromFile: JSON parse error in '{}': {}", resolvedPath, e.what());
        return nullptr;
    }

    return std::make_shared<Prefab>(std::move(parsedJson));
}

bool Prefab::SaveToFile(const std::string& path) const {
    if (!IsValid()) {
        ENGINE_CORE_ERROR("Prefab::SaveToFile failed: Prefab JSON is invalid or empty");
        return false;
    }

    std::string resolvedPath = ResourceManager::ResolvePath(path);
    std::error_code ec;
    auto parentDir = std::filesystem::path(resolvedPath).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Prefab::SaveToFile failed: Cannot open file '{}' for writing", resolvedPath);
        return false;
    }

    file << m_Json.dump(4);
    file.close();
    ENGINE_CORE_INFO("Prefab saved successfully to '{}'", resolvedPath);
    return true;
}

std::shared_ptr<Prefab> Prefab::CreateFromGameObject(const GameObject* root, bool zeroRootPosition) {
    if (!root) {
        ENGINE_CORE_ERROR("Prefab::CreateFromGameObject: root GameObject is null");
        return nullptr;
    }

    nlohmann::json prefabJson;
    prefabJson["Format"] = "EnginePrefab";
    prefabJson["Version"] = 1;
    prefabJson["RootUUID"] = root->GetUUID();

    nlohmann::json entitiesArray = nlohmann::json::array();

    // BFS traversal of root and all descendants
    std::vector<const GameObject*> entities;
    std::queue<const GameObject*> queue;
    queue.push(root);

    while (!queue.empty()) {
        const GameObject* curr = queue.front();
        queue.pop();
        entities.push_back(curr);

        if (const auto* tr = curr->GetTransform()) {
            for (const auto* childTr : tr->GetChildren()) {
                if (childTr && childTr->GetGameObject()) {
                    queue.push(childTr->GetGameObject());
                }
            }
        }
    }

    for (const auto* go : entities) {
        nlohmann::json entityJson = SceneSerializer::SerializeEntity(go);
        if (go == root) {
            // Root entity in prefab has no parent within the prefab
            entityJson["ParentUUID"] = 0;
            if (zeroRootPosition && entityJson.contains("Components") && entityJson["Components"].contains("Transform")) {
                entityJson["Components"]["Transform"]["Position"] = { 0.0f, 0.0f, 0.0f };
            }
        }
        entitiesArray.push_back(std::move(entityJson));
    }

    prefabJson["Entities"] = std::move(entitiesArray);
    return std::make_shared<Prefab>(std::move(prefabJson));
}

} // namespace Engine
