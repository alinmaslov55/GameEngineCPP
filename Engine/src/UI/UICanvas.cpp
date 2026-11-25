#include "Engine/UI/UICanvas.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include <algorithm>

namespace Engine {

float UICanvas::ComputeScaleFactor(uint32_t viewportW, uint32_t viewportH) noexcept {
    if (viewportW == 0 || viewportH == 0) {
        return 1.0f;
    }
    const bool isPortrait = (viewportW < viewportH);
    const float refW = isPortrait ? ReferencePortrait.x : ReferenceLandscape.x;
    const float refH = isPortrait ? ReferencePortrait.y : ReferenceLandscape.y;
    return std::min(static_cast<float>(viewportW) / refW,
                    static_cast<float>(viewportH) / refH);
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

bool UICanvas::WorldToScreenPixels(const glm::vec3& worldPos, const PerspectiveCamera& camera,
                                   uint32_t viewportW, uint32_t viewportH,
                                   glm::vec2& outScreenPx) noexcept {
    if (viewportW == 0 || viewportH == 0) {
        outScreenPx = glm::vec2(0.0f);
        return false;
    }

    const glm::mat4& vp = camera.GetViewProjectionMatrix();
    const glm::vec4 clipPos = vp * glm::vec4(worldPos, 1.0f);

    // Behind camera or on/behind near clip plane
    if (clipPos.w <= 0.001f) {
        outScreenPx = glm::vec2(0.0f);
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    outScreenPx.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportW);
    outScreenPx.y = (ndc.y * 0.5f + 0.5f) * static_cast<float>(viewportH);
    return true;
}

bool UICanvas::WorldToCanvasPosition(const glm::vec3& worldPos, const PerspectiveCamera& camera,
                                     uint32_t viewportW, uint32_t viewportH,
                                     glm::vec2& outCanvasPos) noexcept {
    glm::vec2 screenPx{ 0.0f };
    if (!WorldToScreenPixels(worldPos, camera, viewportW, viewportH, screenPx)) {
        outCanvasPos = glm::vec2(0.0f);
        return false;
    }

    const float scale = ComputeScaleFactor(viewportW, viewportH);
    if (scale <= 0.0001f) {
        outCanvasPos = screenPx;
        return true;
    }

    outCanvasPos = screenPx / scale;
    return true;
}

} // namespace Engine
