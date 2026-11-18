#ifndef SCENE_PROCEDURALPROPFACTORY_H
#define SCENE_PROCEDURALPROPFACTORY_H

#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/WorldBiomeGenerator.h"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>

namespace Engine {

class Prefab;

struct PropTemplateSpec {
    const char* PoolKey;
    const char* EntityName;
    EntityTag Tag;
    uint32_t Layer;
    const char* TexturePath;
    glm::vec3 BaseScale;
    glm::vec4 Color;
    float WindSway;
    float Emissive;
    bool CastShadows;
    bool HasCollider;
    glm::vec2 ColliderSize;
    uint64_t ColliderCategory;
    bool HasLight;
    float LightRadius;
    float LightIntensity;
    glm::vec3 LightColor;
};

/// @brief Factory for built-in procedural world prop prefabs and systemic TarFirewall prefabs.
class ProceduralPropFactory {
public:
    static constexpr size_t kPropKindCount = 9;

    [[nodiscard]] static const PropTemplateSpec& GetPropSpec(ProceduralPropKind kind) noexcept;
    [[nodiscard]] static std::shared_ptr<Prefab> BuildBuiltinPropPrefab(ProceduralPropKind kind);
    [[nodiscard]] static std::shared_ptr<Prefab> BuildTarFirewallPrefab();
};

} // namespace Engine

#endif // SCENE_PROCEDURALPROPFACTORY_H
