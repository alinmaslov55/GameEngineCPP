#ifndef SCENE_DIRECTIONALLIGHTCOMPONENT_H
#define SCENE_DIRECTIONALLIGHTCOMPONENT_H

#include "Engine/Scene/Component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

class DirectionalLightComponent final : public Component {
public:
    float ElevationDegrees = 42.0f; // [10, 85] degrees above horizon
    float AzimuthDegrees = -38.0f;  // [-180, 180] horizontal sun direction

    glm::vec3 SunColor = { 1.0f, 0.92f, 0.76f };
    float SunIntensity = 1.05f;

    // Cool indigo-violet ambient sky/shadow fill (signature Cult of the Lamb shade)
    glm::vec3 AmbientColor = { 0.38f, 0.43f, 0.62f };
    float AmbientIntensity = 0.62f;

    bool CastShadows = true;
    float ShadowSoftness = 1.35f;
    float ShadowBias = 0.0018f;

    // Unit vector pointing FROM the sun TOWARD the ground scene
    glm::vec3 GetLightDirection() const {
        const float elevRad = glm::radians(std::clamp(ElevationDegrees, 5.0f, 89.0f));
        const float azimRad = glm::radians(AzimuthDegrees);
        const float cosElev = std::cos(elevRad);

        const glm::vec3 dir = {
            -std::sin(azimRad) * cosElev,
            -std::sin(elevRad),
            -std::cos(azimRad) * cosElev
        };
        return glm::normalize(dir);
    }

    // Unit horizontal vector perpendicular to the sun's ground direction so 2.5D cutout
    // standees face the sun un-foreshortened during Pass 0 across all 360° azimuths
    glm::vec3 GetSunRightXZ() const {
        const glm::vec3 dir = GetLightDirection();
        const glm::vec3 horiz = { dir.x, 0.0f, dir.z };
        const float lenSq = glm::dot(horiz, horiz);
        if (lenSq < 1e-6f) {
            return { 1.0f, 0.0f, 0.0f };
        }
        const glm::vec3 forwardXZ = horiz / std::sqrt(lenSq);
        return glm::normalize(glm::cross(forwardXZ, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    // Builds an orthographic Light ViewProjection matrix snapped to shadow-map texel increments
    // in light space so panning or following the player never shimmers shadow silhouette edges
    glm::mat4 ComputeLightSpaceMatrix(const glm::vec3& focusPoint,
                                      float orthoRadius,
                                      uint32_t shadowMapResolution) const {
        const float safeRadius = std::max(2.0f, orthoRadius);
        const uint32_t safeRes = std::max(64u, shadowMapResolution);
        const glm::vec3 lightDir = GetLightDirection();

        const glm::vec3 upHint = (std::abs(lightDir.y) > 0.98f)
            ? glm::vec3(0.0f, 0.0f, -1.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);

        // Construct stabilized light basis around the origin first, then snap focusPoint in light XY
        const glm::mat4 basisView = glm::lookAt(-lightDir * 40.0f, glm::vec3(0.0f), upHint);
        glm::vec4 lightSpaceFocus = basisView * glm::vec4(focusPoint, 1.0f);

        const float worldUnitsPerTexel = (2.0f * safeRadius) / static_cast<float>(safeRes);
        lightSpaceFocus.x = std::floor(lightSpaceFocus.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        lightSpaceFocus.y = std::floor(lightSpaceFocus.y / worldUnitsPerTexel) * worldUnitsPerTexel;

        const glm::vec3 snappedWorldFocus = glm::vec3(glm::inverse(basisView) * lightSpaceFocus);
        const glm::mat4 lightView = glm::lookAt(snappedWorldFocus - lightDir * 40.0f, snappedWorldFocus, upHint);
        const glm::mat4 lightProj = glm::ortho(-safeRadius, safeRadius, -safeRadius, safeRadius, 1.0f, 90.0f);
        return lightProj * lightView;
    }
};

} // namespace Engine

#endif // SCENE_DIRECTIONALLIGHTCOMPONENT_H
