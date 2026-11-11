#ifndef RENDERER_SCENELIGHTING_H
#define RENDERER_SCENELIGHTING_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

struct alignas(16) PointLightUniform {
    glm::vec4 PositionAndRadius{ 0.0f, 0.85f, 0.0f, 4.5f };  // xyz = world pos, w = radius
    glm::vec4 ColorAndIntensity{ 1.0f, 0.65f, 0.25f, 1.5f }; // rgb = color, a = effective intensity
    glm::vec4 Params{ 1.8f, 0.0f, 1.6f, 0.0025f };           // x = falloff, y = castsShadow (0/1), z = softness, w = bias
};
static_assert(sizeof(PointLightUniform) == 48, "PointLightUniform must match std140 stride (48 bytes)");

struct alignas(16) PointLightUBOBlock {
    static constexpr uint32_t MaxPointLights = 32;
    glm::ivec4 Header{ 0, 0, 0, 0 }; // x = active point light count, y = primary shadow light enabled
    std::array<PointLightUniform, MaxPointLights> PointLights{};
};
static_assert(sizeof(PointLightUBOBlock) == 16 + 32 * 48, "PointLightUBOBlock must match GLSL std140 layout");

struct SceneLightingData {
    static constexpr uint32_t MaxPointLights = PointLightUBOBlock::MaxPointLights;
    static constexpr uint32_t PointLightUBOBinding = 0;

    bool LightingActive = false;

    // Directional Sun & Ambient Sky/Shadow Fill
    glm::vec3 SunDirection = { 0.47f, -0.67f, -0.58f };
    glm::vec3 SunColor = { 1.0f, 0.92f, 0.76f };
    float SunIntensity = 0.0f;

    glm::vec3 AmbientColor = { 1.0f, 1.0f, 1.0f };
    float AmbientIntensity = 1.0f;

    // Directional Shadow Map Parameters
    bool ShadowsEnabled = false;
    uint32_t ShadowMapTextureID = 0;
    float ShadowMapTexelSize = 1.0f / 2048.0f;
    float ShadowSoftness = 1.35f;
    float ShadowBias = 0.0018f;
    glm::mat4 LightSpaceMatrix{ 1.0f };

    // Omnidirectional Point-Light Torch Shadow Map Parameters
    bool TorchShadowsEnabled = false;
    uint32_t TorchShadowMapTextureID = 0;
    glm::vec4 TorchShadowPosRadius{ 0.0f, 0.85f, 0.0f, 4.5f };
    glm::vec2 TorchShadowTexelSize{ 1.0f / 2048.0f, 1.0f / 512.0f };
    float TorchShadowSoftness = 1.6f;
    float TorchShadowBias = 0.0025f;
    std::array<glm::mat4, 4> TorchLightMatrices{
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)
    };

    // Dynamic 3D Point Lights
    uint32_t PointLightCount = 0;
    std::array<PointLightUniform, MaxPointLights> PointLights{};
};

struct SceneWindData {
    bool WindActive = false;
    float TimeSeconds = 0.0f;
    float Strength = 0.16f;
    float Speed = 2.15f;
    float Gustiness = 0.55f;
    float GustFrequency = 0.42f;
    glm::vec3 SwayRightAxis = { 1.0f, 0.0f, 0.0f };
};

} // namespace Engine

#endif // RENDERER_SCENELIGHTING_H

