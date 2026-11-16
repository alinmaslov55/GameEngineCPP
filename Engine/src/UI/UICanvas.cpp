#include "Engine/UI/UICanvas.h"
#include <algorithm>

namespace Engine {

float UICanvas::ComputeScaleFactor(uint32_t viewportW, uint32_t viewportH) noexcept {
    if (viewportH == 0) {
        return 1.0f;
    }
    const float refH = (viewportW < viewportH) ? ReferencePortrait.y : ReferenceLandscape.y;
    return static_cast<float>(viewportH) / refH;
}

void UICanvas::ResolveScreenRect(const UIRectTransform& rect, uint32_t viewportW, uint32_t viewportH,
                                glm::vec2& outBottomLeftPx, glm::vec2& outSizePx) noexcept {
    if (viewportW == 0 || viewportH == 0) {
        outBottomLeftPx = glm::vec2(0.0f);
        outSizePx = glm::vec2(0.0f);
        return;
    }

    const float scale = ComputeScaleFactor(viewportW, viewportH);
    outSizePx = rect.Size * scale;

    const bool isPortrait = (viewportW < viewportH);
    const UIAnchor activeAnchor = (isPortrait && rect.UsePortraitOverride) ? rect.PortraitAnchorOverride : rect.Anchor;

    const float fW = static_cast<float>(viewportW);
    const float fH = static_cast<float>(viewportH);

    float minX = 0.0f;
    float maxX = fW;
    float minY = 0.0f;
    float maxY = fH;

    if (!rect.IgnoreSafeArea) {
        const float marginX = fW * SafeAreaMarginRatio;
        const float marginY = fH * SafeAreaMarginRatio;
        minX = marginX;
        maxX = fW - marginX;
        minY = marginY;
        maxY = fH - marginY;
    }

    float anchorX = 0.0f;
    float anchorY = 0.0f;

    switch (activeAnchor) {
        case UIAnchor::BottomLeft:
            anchorX = minX;
            anchorY = minY;
            break;
        case UIAnchor::BottomCenter:
            anchorX = (minX + maxX) * 0.5f;
            anchorY = minY;
            break;
        case UIAnchor::BottomRight:
            anchorX = maxX;
            anchorY = minY;
            break;
        case UIAnchor::MiddleLeft:
            anchorX = minX;
            anchorY = (minY + maxY) * 0.5f;
            break;
        case UIAnchor::Center:
            anchorX = (minX + maxX) * 0.5f;
            anchorY = (minY + maxY) * 0.5f;
            break;
        case UIAnchor::MiddleRight:
            anchorX = maxX;
            anchorY = (minY + maxY) * 0.5f;
            break;
        case UIAnchor::TopLeft:
            anchorX = minX;
            anchorY = maxY;
            break;
        case UIAnchor::TopCenter:
            anchorX = (minX + maxX) * 0.5f;
            anchorY = maxY;
            break;
        case UIAnchor::TopRight:
            anchorX = maxX;
            anchorY = maxY;
            break;
    }

    const glm::vec2 scaledOffset = rect.AnchoredOffset * scale;
    outBottomLeftPx.x = anchorX + scaledOffset.x - (rect.Pivot.x * outSizePx.x);
    outBottomLeftPx.y = anchorY + scaledOffset.y - (rect.Pivot.y * outSizePx.y);
}

} // namespace Engine
