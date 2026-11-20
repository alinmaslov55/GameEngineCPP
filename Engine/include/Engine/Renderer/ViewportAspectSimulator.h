#ifndef RENDERER_VIEWPORTASPECTSIMULATOR_H
#define RENDERER_VIEWPORTASPECTSIMULATOR_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string_view>

namespace Engine {

enum class ViewportAspectMode : uint8_t {
    Free = 0,
    Landscape_16_9,   // 16:9  (~1.7778)
    Landscape_16_10,  // 16:10 (1.6000)
    Portrait_9_16,    // 9:16  (~0.5625)
    Portrait_10_16    // 10:16 (0.6250)
};

struct ViewportAspectMetrics {
    glm::vec2 RenderSize{ 0.0f };   // Constrained dimensions for scene FBO
    glm::vec2 Offset{ 0.0f };       // Centering offset inside available panel
    float AspectRatio{ 1.0f };      // Width / Height
    bool IsPillarboxed{ false };
    bool IsLetterboxed{ false };
};

struct ViewportSafeAreaRect {
    glm::vec2 Min{ 0.0f };          // Top-left corner of safe area
    glm::vec2 Max{ 0.0f };          // Bottom-right corner of safe area
    glm::vec2 Size{ 0.0f };
    float MarginX{ 0.0f };
    float MarginY{ 0.0f };
};

class ViewportAspectSimulator {
public:
    static constexpr float kAspect_16_9  = 16.0f / 9.0f;
    static constexpr float kAspect_16_10 = 16.0f / 10.0f;
    static constexpr float kAspect_9_16  = 9.0f / 16.0f;
    static constexpr float kAspect_10_16 = 10.0f / 16.0f;
    static constexpr float kDefaultSafeAreaMargin = 0.05f; // 5% Safe Area

    [[nodiscard]] static float GetTargetAspectRatio(ViewportAspectMode mode, float fallbackRatio = kAspect_16_9) noexcept;
    [[nodiscard]] static std::string_view GetModeName(ViewportAspectMode mode) noexcept;
    [[nodiscard]] static ViewportAspectMetrics CalculateMetrics(const glm::vec2& availableSize, ViewportAspectMode mode) noexcept;
    [[nodiscard]] static ViewportSafeAreaRect CalculateSafeArea(const glm::vec2& imageOrigin,
                                                               const glm::vec2& renderSize,
                                                               float marginRatio = kDefaultSafeAreaMargin) noexcept;
};

} // namespace Engine

#endif // RENDERER_VIEWPORTASPECTSIMULATOR_H
