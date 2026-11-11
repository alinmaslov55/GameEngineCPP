#ifndef RENDERER_TORCHSHADOWMAP_H
#define RENDERER_TORCHSHADOWMAP_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

enum class TorchShadowQuadrant : uint32_t {
    North = 0, // -Z
    East  = 1, // +X
    South = 2, // +Z
    West  = 3  // -X
};

class TorchShadowMap {
public:
    static constexpr uint32_t QuadrantCount = 4;
    static constexpr uint32_t DefaultQuadrantResolution = 512;
    static constexpr float NearPlane = 0.45f;
    static constexpr float MaxShadowRadius = 18.0f;

    static void Init(uint32_t quadrantResolution = DefaultQuadrantResolution);
    static void Shutdown();

    [[nodiscard]] static bool IsValid();
    static void BeginCapture();
    static void BeginQuadrant(TorchShadowQuadrant quadrant);
    static void EndCapture();

    [[nodiscard]] static uint32_t GetDepthTextureID();
    [[nodiscard]] static uint32_t GetQuadrantResolution();
    [[nodiscard]] static uint32_t GetAtlasWidth();
    [[nodiscard]] static uint32_t GetAtlasHeight();

    // Pure math helpers for 4-quadrant 90-degree horizontal atlas and 360-degree radial billboarding
    [[nodiscard]] static glm::vec3 GetQuadrantForward(TorchShadowQuadrant quadrant) noexcept;
    [[nodiscard]] static glm::vec3 GetQuadrantRight(TorchShadowQuadrant quadrant) noexcept;
    [[nodiscard]] static glm::mat4 ComputeQuadrantLightMatrix(TorchShadowQuadrant quadrant,
                                                              const glm::vec3& torchWorldPos,
                                                              float radius) noexcept;
    [[nodiscard]] static std::array<glm::mat4, QuadrantCount> ComputeAllQuadrantMatrices(
        const glm::vec3& torchWorldPos,
        float radius) noexcept;
    [[nodiscard]] static glm::vec3 ComputeRadialBillboardRightXZ(const glm::vec3& standeePos,
                                                                 const glm::vec3& torchPos) noexcept;
};

} // namespace Engine

#endif // RENDERER_TORCHSHADOWMAP_H
