#ifndef UI_UICANVAS_H
#define UI_UICANVAS_H

#include <cstdint>
#include <glm/vec2.hpp>

namespace Engine {

enum class UIAnchor : uint8_t {
    BottomLeft = 0, BottomCenter, BottomRight,
    MiddleLeft,     Center,       MiddleRight,
    TopLeft,        TopCenter,    TopRight
};

struct UIRectTransform {
    UIAnchor Anchor = UIAnchor::TopLeft;
    UIAnchor PortraitAnchorOverride = UIAnchor::TopLeft; // Optional reflow when aspect < 1.0
    bool UsePortraitOverride = false;
    glm::vec2 Pivot{ 0.0f, 1.0f };       // Normalized [0..1] pivot (default {0, 1} = top-left)
    glm::vec2 AnchoredOffset{ 0.0f };    // Offset in 1080p Reference Units
    glm::vec2 Size{ 100.0f, 40.0f };     // Dimensions in 1080p Reference Units
    int32_t ZOrder = 0;                  // Deterministic back-to-front UI layer sorting
    bool IgnoreSafeArea = false;         // Set true to span full physical screen (e.g., vignettes)
};

class UICanvas {
public:
    inline static const glm::vec2 ReferenceLandscape = { 1920.0f, 1080.0f };
    inline static const glm::vec2 ReferencePortrait  = { 1080.0f, 1920.0f };
    static constexpr float SafeAreaMarginRatio = 0.05f; // 5% inset on all 4 edges

    [[nodiscard]] static float ComputeScaleFactor(uint32_t viewportW, uint32_t viewportH) noexcept;

    static void ResolveScreenRect(const UIRectTransform& rect, uint32_t viewportW, uint32_t viewportH,
                                  glm::vec2& outBottomLeftPx, glm::vec2& outSizePx) noexcept;
};

} // namespace Engine

#endif // UI_UICANVAS_H
