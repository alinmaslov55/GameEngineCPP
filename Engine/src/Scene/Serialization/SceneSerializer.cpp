#include "Engine/Scene/SceneSerializer.h"
#include "JsonHelpers.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace Engine {

using namespace Serialization;

namespace {

// =========================================================================
// Component Serializer Registry
// =========================================================================

using ComponentSerializerFn = void(*)(const GameObject* go, json& compsJson);

static const ComponentSerializerFn s_ComponentSerializers[] = {
    SerializeTransform,
    SerializeSpriteRenderer,
    SerializeTilemap,
    SerializeTerrain,
    SerializeTerrainChunkManager,
    SerializeParticleSystem,
    SerializeDirectionalLight,
    SerializePointLight,
    SerializePostProcess,
    SerializeWind,
    SerializeSquashStretch,
    SerializeRigidbody2D,
    SerializeBoxCollider2D,
    SerializeCircleCollider2D,
    SerializeCapsuleCollider2D,
    SerializeCameraComponent,
    SerializeUIPanels,
    SerializeUILabels,
    SerializeUIButtonComponent,
    SerializeUIMinimapComponent,
    SerializeUIRadialWheelComponent,
    SerializeAudioSource,
    SerializeAnimator
};

json SerializeEntityInternal(const GameObject* go) {
    json entityJson;
    entityJson["UUID"] = go->GetUUID();
    entityJson["Name"] = go->GetName();
    entityJson["Tag"] = EntityTagToString(go->GetTag());
    entityJson["LayerMask"] = go->GetLayer();

    uint64_t parentUUID = 0;
    if (const auto* transform = go->GetTransform()) {
        if (const auto* parent = transform->GetParent()) {
            if (const auto* parentGo = parent->GetGameObject()) {
                parentUUID = parentGo->GetUUID();
            }
        }
    }
    entityJson["ParentUUID"] = parentUUID;

    json& compsJson = entityJson["Components"];

    for (auto serializerFn : s_ComponentSerializers) {
        serializerFn(go, compsJson);
    }

    return entityJson;
}


// =========================================================================
// Component Deserializer Registry Table
// =========================================================================

using ComponentDeserializerFn = void(*)(GameObject* go, const json& j, const std::string& name);

struct ComponentDeserializerEntry {
    std::string_view key;
    ComponentDeserializerFn deserialize;
};

static const ComponentDeserializerEntry s_ComponentDeserializers[] = {
    { "Transform",        DeserializeTransform },
    { "SpriteRenderer",   DeserializeSpriteRenderer },
    { "Tilemap",              DeserializeTilemap },
    { "Terrain",              DeserializeTerrain },
    { "TerrainChunkManager",  DeserializeTerrainChunkManager },
    { "ParticleSystem",       DeserializeParticleSystem },
    { "DirectionalLight", DeserializeDirectionalLight },
    { "PointLight",       DeserializePointLight },
    { "PostProcess",      DeserializePostProcess },
    { "Wind",             DeserializeWind },
    { "SquashStretch",    DeserializeSquashStretch },
    { "Rigidbody2D",      DeserializeRigidbody2D },
    { "BoxCollider2D",     DeserializeBoxCollider2D },
    { "CircleCollider2D",  DeserializeCircleCollider2D },
    { "CapsuleCollider2D", DeserializeCapsuleCollider2D },
    { "CameraComponent",  DeserializeCameraComponent },
    { "UIPanels",         DeserializeUIPanels },
    { "UIPanel",          DeserializeUIPanels },
    { "UILabels",         DeserializeUILabels },
    { "UILabel",          DeserializeUILabels },
    { "UIButtonComponent", DeserializeUIButtonComponent },
    { "UIButton",          DeserializeUIButtonComponent },
    { "UIMinimapComponent", DeserializeUIMinimapComponent },
    { "UIMinimap",          DeserializeUIMinimapComponent },
    { "UIRadialWheelComponent", DeserializeUIRadialWheelComponent },
    { "UIRadialWheel",          DeserializeUIRadialWheelComponent },
    { "AudioSource",      DeserializeAudioSource },
    { "Animator",         DeserializeAnimator },
};

void DeserializeEntityComponentsInternal(GameObject* go, const json& componentsJson, const std::string& name) {
    if (!componentsJson.is_object()) return;

    for (const auto& entry : s_ComponentDeserializers) {
        auto it = componentsJson.find(entry.key);
        if (it != componentsJson.end()) {
            entry.deserialize(go, *it, name);
        }
    }
}

} // namespace

// =========================================================================
// SceneSerializer Class Implementation
// =========================================================================

nlohmann::json SceneSerializer::SerializeEntity(const GameObject* go) {
    return SerializeEntityInternal(go);
}

void SceneSerializer::DeserializeEntityComponents(GameObject* go, const json& componentsJson, const std::string& name) {
    DeserializeEntityComponentsInternal(go, componentsJson, name);
}

SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
    : m_Scene(scene) {}

std::string SceneSerializer::SerializeToString() const {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::SerializeToString failed: Scene pointer is null");
        return {};
    }

    json sceneJson;
    sceneJson["SceneName"] = m_Scene->GetName();
    sceneJson["OrthographicSize"] = m_Scene->GetOrthographicSize();
    SerializeSceneCamera(m_Scene->GetCamera(), sceneJson["Camera"]);

    json gameObjectsJson = json::array();
    for (const auto& go : m_Scene->GetGameObjects()) {
        gameObjectsJson.push_back(SerializeEntity(go.get()));
    }
    sceneJson["GameObjects"] = std::move(gameObjectsJson);

    return sceneJson.dump(4);
}

bool SceneSerializer::Serialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Serialize failed: Scene pointer is null");
        return false;
    }

    std::string resolvedPath = ResourceManager::ResolvePath(filepath);
    ENGINE_CORE_INFO("Serializing scene '{}' to '{}'", m_Scene->GetName(), resolvedPath);

    std::error_code ec;
    auto parentDir = std::filesystem::path(resolvedPath).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    // Flush any dirty TerrainComponent splatmaps to external PNG assets before writing JSON
    const std::string sceneStem = std::filesystem::path(filepath).stem().string();
    uint32_t terrainCounter = 0;
    for (const auto& go : m_Scene->GetGameObjects()) {
        if (auto* terrain = go->GetComponent<TerrainComponent>()) {
            if (terrain->GetSplatmapPath().empty()) {
                const std::string baseName = sceneStem.empty() ? "terrain" : sceneStem;
                const std::string suffix = (terrainCounter == 0) ? "_splatmap.png" : ("_splatmap_" + std::to_string(terrainCounter) + ".png");
                terrain->SetSplatmapPath("assets/diorama/" + baseName + suffix);
            }
            ++terrainCounter;

            const std::string resolvedSplatPath = ResourceManager::ResolvePath(terrain->GetSplatmapPath());
            if (terrain->HasUnsavedMemoryEdits() || !std::filesystem::exists(resolvedSplatPath, ec)) {
                terrain->SaveSplatmapToPNG(resolvedSplatPath);
            }
        }
    }

    std::string serializedData = SerializeToString();
    if (serializedData.empty()) {
        return false;
    }

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene serialization: '{}'", resolvedPath);
        return false;
    }

    file << serializedData;
    file.close();
    ENGINE_CORE_INFO("Scene serialized successfully to '{}' ({} GameObjects)", resolvedPath, m_Scene->GetGameObjects().size());
    return true;
}

bool SceneSerializer::DeserializeFromString(const std::string& jsonString) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::DeserializeFromString failed: Scene pointer is null");
        return false;
    }

    try {
        json sceneJson = json::parse(jsonString);
        if (!sceneJson.is_object()) {
            ENGINE_CORE_ERROR("SceneSerializer::DeserializeFromString: Root JSON must be an object");
            return false;
        }

        if (sceneJson.contains("SceneName") && sceneJson["SceneName"].is_string()) {
            m_Scene->SetName(sceneJson["SceneName"].get<std::string>());
        }
        if (sceneJson.contains("OrthographicSize") && sceneJson["OrthographicSize"].is_number()) {
            m_Scene->SetOrthographicSize(sceneJson["OrthographicSize"].get<float>());
        }
        if (sceneJson.contains("Camera") && sceneJson["Camera"].is_object()) {
            DeserializeSceneCamera(sceneJson["Camera"], m_Scene->GetCamera());
        }

        size_t loadedCount = 0;
        std::vector<std::pair<GameObject*, uint64_t>> parentLinks;
        if (sceneJson.contains("GameObjects") && sceneJson["GameObjects"].is_array()) {
            for (const auto& entityJson : sceneJson["GameObjects"]) {
                std::string name = entityJson.value("Name", "GameObject");
                GameObject* go = entityJson.contains("UUID")
                    ? m_Scene->CreateGameObjectWithUUID(entityJson["UUID"].get<uint64_t>(), name)
                    : m_Scene->CreateGameObject(name);
                loadedCount++;

                uint64_t parentUUID = entityJson.value("ParentUUID", 0ULL);
                if (parentUUID != 0) {
                    parentLinks.emplace_back(go, parentUUID);
                }

                if (entityJson.contains("Tag")) {
                    if (entityJson["Tag"].is_string()) {
                        go->SetTag(StringToEntityTag(entityJson["Tag"].get<std::string>()));
                    } else if (entityJson["Tag"].is_number()) {
                        go->SetTag(static_cast<EntityTag>(entityJson["Tag"].get<uint16_t>()));
                    }
                }
                if (entityJson.contains("LayerMask") && entityJson["LayerMask"].is_number()) {
                    go->SetLayer(entityJson["LayerMask"].get<uint32_t>());
                }

                if (entityJson.contains("Components")) {
                    DeserializeEntityComponents(go, entityJson["Components"], name);
                }
            }

            // Pass 2: Connect parent-child hierarchy
            for (const auto& [childGo, parentUUID] : parentLinks) {
                if (auto* parentGo = m_Scene->FindGameObjectByUUID(parentUUID)) {
                    if (auto* childTransform = childGo->GetTransform()) {
                        childTransform->SetParent(parentGo->GetTransform(), /*keepWorldTransform=*/false);
                    }
                } else {
                    ENGINE_CORE_WARN("SceneSerializer: Entity '{}' ({}) references missing parent UUID {}",
                                     childGo->GetName(), childGo->GetUUID(), parentUUID);
                }
            }
        }

        ENGINE_CORE_DEBUG("Scene deserialized from JSON ({} GameObjects loaded)", loadedCount);
        return true;
    } catch (const nlohmann::json::exception& e) {
        ENGINE_CORE_ERROR("SceneSerializer::DeserializeFromString JSON exception: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("SceneSerializer::DeserializeFromString exception: {}", e.what());
        return false;
    }
}

bool SceneSerializer::Deserialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Deserialize failed: Scene pointer is null");
        return false;
    }

    std::string resolvedPath = ResourceManager::ResolvePath(filepath);
    ENGINE_CORE_INFO("Deserializing scene from '{}'", resolvedPath);

    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene deserialization: '{}'", resolvedPath);
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (!DeserializeFromString(content)) {
        ENGINE_CORE_ERROR("Failed to deserialize scene from '{}'", resolvedPath);
        return false;
    }

    ENGINE_CORE_INFO("Scene deserialized successfully from '{}' ({} GameObjects loaded)",
                     resolvedPath, m_Scene->GetGameObjects().size());
    return true;
}

} // namespace Engine
