#ifndef SCENE_SERIALIZATION_JSONHELPERS_H
#define SCENE_SERIALIZATION_JSONHELPERS_H

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace Engine {

class GameObject;
class PerspectiveCamera;
class SubTexture2D;

namespace Serialization {

using json = nlohmann::json;

inline json ToJson(const glm::vec2& v) { return { v.x, v.y }; }
inline json ToJson(const glm::vec3& v) { return { v.x, v.y, v.z }; }
inline json ToJson(const glm::vec4& v) { return { v.r, v.g, v.b, v.a }; }

inline glm::vec2 AsVec2(const json& j, const glm::vec2& def = glm::vec2(0.0f)) {
    if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number()) {
        return { j[0].get<float>(), j[1].get<float>() };
    }
    return def;
}

inline glm::vec3 AsVec3(const json& j, const glm::vec3& def = glm::vec3(0.0f)) {
    if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number()) {
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    }
    return def;
}

inline glm::vec4 AsVec4(const json& j, const glm::vec4& def = glm::vec4(0.0f)) {
    if (j.is_array() && j.size() >= 4 && j[0].is_number() && j[1].is_number() && j[2].is_number() && j[3].is_number()) {
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
    }
    return def;
}

json SerializeSubTexture(const std::shared_ptr<SubTexture2D>& subTex);
std::shared_ptr<SubTexture2D> DeserializeSubTexture(const json& subTexJson);

void SerializeSceneCamera(const PerspectiveCamera& sceneCam, json& outJson);
void DeserializeSceneCamera(const json& camJson, PerspectiveCamera& sceneCam);

// Core components
void SerializeTransform(const GameObject* go, json& compsJson);
void DeserializeTransform(GameObject* go, const json& tJson, const std::string& name);
void SerializeDirectionalLight(const GameObject* go, json& compsJson);
void DeserializeDirectionalLight(GameObject* go, const json& dlJson, const std::string& name);
void SerializePointLight(const GameObject* go, json& compsJson);
void DeserializePointLight(GameObject* go, const json& plJson, const std::string& name);
void SerializePostProcess(const GameObject* go, json& compsJson);
void DeserializePostProcess(GameObject* go, const json& ppJson, const std::string& name);
void SerializeWind(const GameObject* go, json& compsJson);
void DeserializeWind(GameObject* go, const json& wJson, const std::string& name);
void SerializeSquashStretch(const GameObject* go, json& compsJson);
void DeserializeSquashStretch(GameObject* go, const json& ssJson, const std::string& name);
void SerializeCameraComponent(const GameObject* go, json& compsJson);
void DeserializeCameraComponent(GameObject* go, const json& camJson, const std::string& name);
void SerializeAudioSource(const GameObject* go, json& compsJson);
void DeserializeAudioSource(GameObject* go, const json& audioJson, const std::string& name);

// Render & Animation components
void SerializeSpriteRenderer(const GameObject* go, json& compsJson);
void DeserializeSpriteRenderer(GameObject* go, const json& srJson, const std::string& name);
void SerializeParticleSystem(const GameObject* go, json& compsJson);
void DeserializeParticleSystem(GameObject* go, const json& psJson, const std::string& name);
void SerializeAnimator(const GameObject* go, json& compsJson);
void DeserializeAnimator(GameObject* go, const json& animJson, const std::string& name);

// Terrain & Tilemap components
void SerializeTilemap(const GameObject* go, json& compsJson);
void DeserializeTilemap(GameObject* go, const json& tmJson, const std::string& name);
void SerializeTerrain(const GameObject* go, json& compsJson);
void DeserializeTerrain(GameObject* go, const json& trJson, const std::string& name);
void SerializeTerrainChunkManager(const GameObject* go, json& compsJson);
void DeserializeTerrainChunkManager(GameObject* go, const json& tcmJson, const std::string& name);

// Physics components
void SerializeRigidbody2D(const GameObject* go, json& compsJson);
void DeserializeRigidbody2D(GameObject* go, const json& rbJson, const std::string& name);
void SerializeBoxCollider2D(const GameObject* go, json& compsJson);
void DeserializeBoxCollider2D(GameObject* go, const json& bcJson, const std::string& name);
void SerializeCircleCollider2D(const GameObject* go, json& compsJson);
void DeserializeCircleCollider2D(GameObject* go, const json& ccJson, const std::string& name);
void SerializeCapsuleCollider2D(const GameObject* go, json& compsJson);
void DeserializeCapsuleCollider2D(GameObject* go, const json& capJson, const std::string& name);

// UI components
void SerializeUIPanels(const GameObject* go, json& compsJson);
void DeserializeUIPanels(GameObject* go, const json& j, const std::string& name);
void SerializeUILabels(const GameObject* go, json& compsJson);
void DeserializeUILabels(GameObject* go, const json& j, const std::string& name);
void SerializeUIButtonComponent(const GameObject* go, json& compsJson);
void DeserializeUIButtonComponent(GameObject* go, const json& j, const std::string& name);
void SerializeUIMinimapComponent(const GameObject* go, json& compsJson);
void DeserializeUIMinimapComponent(GameObject* go, const json& j, const std::string& name);
void SerializeUIRadialWheelComponent(const GameObject* go, json& compsJson);
void DeserializeUIRadialWheelComponent(GameObject* go, const json& j, const std::string& name);

} // namespace Serialization
} // namespace Engine

#endif // SCENE_SERIALIZATION_JSONHELPERS_H
