#include "Engine/Renderer/ViewportAspectSimulator.h"

#include <algorithm>
#include <cmath>

namespace Engine {

float ViewportAspectSimulator::GetTargetAspectRatio(ViewportAspectMode mode, float fallbackRatio) noexcept {
    switch (mode) {
        case ViewportAspectMode::Landscape_16_9:  return kAspect_16_9;
        case ViewportAspectMode::Landscape_16_10: return kAspect_16_10;
        case ViewportAspectMode::Portrait_9_16:   return kAspect_9_16;
        case ViewportAspectMode::Portrait_10_16:  return kAspect_10_16;
        case ViewportAspectMode::Free:
        default:
            return fallbackRatio > 0.001f ? fallbackRatio : kAspect_16_9;
    }
}

std::string_view ViewportAspectSimulator::GetModeName(ViewportAspectMode mode) noexcept {
    switch (mode) {
        case ViewportAspectMode::Free:             return "Free";
        case ViewportAspectMode::Landscape_16_9:   return "16:9 Landscape";
        case ViewportAspectMode::Landscape_16_10:  return "16:10 Landscape";
        case ViewportAspectMode::Portrait_9_16:    return "9:16 Portrait";
        case ViewportAspectMode::Portrait_10_16:   return "10:16 Portrait";
        default:                                   return "Unknown";
    }
}

ViewportAspectMetrics ViewportAspectSimulator::CalculateMetrics(const glm::vec2& availableSize, ViewportAspectMode mode) noexcept {
    ViewportAspectMetrics metrics;
    const float availW = std::max(availableSize.x, 1.0f);
    const float availH = std::max(availableSize.y, 1.0f);

    if (mode == ViewportAspectMode::Free) {
        metrics.RenderSize = { std::round(availW), std::round(availH) };
        metrics.Offset = { 0.0f, 0.0f };
        metrics.AspectRatio = availW / availH;
        metrics.IsPillarboxed = false;
        metrics.IsLetterboxed = false;
        return metrics;
    }

    const float targetAspect = GetTargetAspectRatio(mode);
    const float currentAspect = availW / availH;

    if (currentAspect > targetAspect + 1e-4f) {
        // Available space is wider than target aspect ratio -> Pillarbox (bars on left/right)
        const float renderW = std::clamp(std::round(availH * targetAspect), 1.0f, availW);
        const float renderH = std::round(availH);
        metrics.RenderSize = { renderW, renderH };
        metrics.Offset = { std::max((availW - renderW) * 0.5f, 0.0f), 0.0f };
        metrics.AspectRatio = renderW / renderH;
        metrics.IsPillarboxed = true;
        metrics.IsLetterboxed = false;
    } else if (currentAspect < targetAspect - 1e-4f) {
        // Available space is taller than target aspect ratio -> Letterbox (bars on top/bottom)
        const float renderW = std::round(availW);
        const float renderH = std::clamp(std::round(availW / targetAspect), 1.0f, availH);
        metrics.RenderSize = { renderW, renderH };
        metrics.Offset = { 0.0f, std::max((availH - renderH) * 0.5f, 0.0f) };
        metrics.AspectRatio = renderW / renderH;
        metrics.IsPillarboxed = false;
        metrics.IsLetterboxed = true;
    } else {
        // Exact fit
        metrics.RenderSize = { std::round(availW), std::round(availH) };
        metrics.Offset = { 0.0f, 0.0f };
        metrics.AspectRatio = targetAspect;
        metrics.IsPillarboxed = false;
        metrics.IsLetterboxed = false;
    }

    return metrics;
}

ViewportSafeAreaRect ViewportAspectSimulator::CalculateSafeArea(const glm::vec2& imageOrigin,
                                                               const glm::vec2& renderSize,
                                                               float marginRatio) noexcept {
    ViewportSafeAreaRect rect;
    const float safeMargin = std::clamp(marginRatio, 0.0f, 0.45f);
    rect.MarginX = renderSize.x * safeMargin;
    rect.MarginY = renderSize.y * safeMargin;
    rect.Min = imageOrigin + glm::vec2(rect.MarginX, rect.MarginY);
    rect.Max = imageOrigin + renderSize - glm::vec2(rect.MarginX, rect.MarginY);
    rect.Size = rect.Max - rect.Min;
    return rect;
}

} // namespace Engine
