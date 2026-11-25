#include "Engine/UI/UIMinimapComponent.h"
#include <algorithm>

namespace Engine {

UIMinimapComponent::UIMinimapComponent() {
    rectTransform.Anchor = UIAnchor::TopRight;
    rectTransform.Pivot = { 1.0f, 1.0f };
    rectTransform.AnchoredOffset = { -20.0f, -20.0f };
    rectTransform.Size = { 220.0f, 220.0f };
    rectTransform.ZOrder = 20;
}

std::vector<UIQuad> UIMinimapComponent::GenerateQuads(const glm::vec2& resolvedPos,
                                                      const glm::vec2& resolvedSize,
                                                      float borderScale) const {
    std::vector<UIQuad> quads;

    const glm::vec2 center = resolvedPos + resolvedSize * 0.5f;
    const float radius = std::min(resolvedSize.x, resolvedSize.y) * 0.5f;

    // 1. Exploration Fog Quad
    UIQuad fogQuad{};
    fogQuad.Vertices[0] = { { resolvedPos.x,                     resolvedPos.y },                     { 0.0f, 0.0f } };
    fogQuad.Vertices[1] = { { resolvedPos.x + resolvedSize.x,     resolvedPos.y },                     { 1.0f, 0.0f } };
    fogQuad.Vertices[2] = { { resolvedPos.x + resolvedSize.x,     resolvedPos.y + resolvedSize.y },     { 1.0f, 1.0f } };
    fogQuad.Vertices[3] = { { resolvedPos.x,                     resolvedPos.y + resolvedSize.y },     { 0.0f, 1.0f } };

    if (fogSystem && fogSystem->GetFogTexture() && fogSystem->GetFogTexture()->GetRendererID() != 0) {
        fogQuad.Color = glm::vec4(1.0f);
        fogQuad.QuadTexture = fogSystem->GetFogTexture();
    } else {
        fogQuad.Color = glm::vec4(0.08f, 0.08f, 0.12f, 0.90f);
        fogQuad.QuadTexture = nullptr;
    }
    quads.push_back(fogQuad);

    // 2. Ornate Circular Minimap Frame Overlay
    if (frameTexture && frameTexture->GetRendererID() != 0) {
        UIQuad frameQuad{};
        for (size_t i = 0; i < 4; ++i) {
            frameQuad.Vertices[i] = fogQuad.Vertices[i];
        }
        frameQuad.Color = frameColor;
        frameQuad.QuadTexture = frameTexture;
        quads.push_back(frameQuad);
    } else {
        // Fallback outer rim accent ring
        UIQuad rimQuad{};
        rimQuad.Vertices[0] = { { resolvedPos.x - 2.0f,                     resolvedPos.y - 2.0f },                     { 0.0f, 0.0f } };
        rimQuad.Vertices[1] = { { resolvedPos.x + resolvedSize.x + 2.0f,     resolvedPos.y - 2.0f },                     { 1.0f, 0.0f } };
        rimQuad.Vertices[2] = { { resolvedPos.x + resolvedSize.x + 2.0f,     resolvedPos.y + resolvedSize.y + 2.0f },     { 1.0f, 1.0f } };
        rimQuad.Vertices[3] = { { resolvedPos.x - 2.0f,                     resolvedPos.y + resolvedSize.y + 2.0f },     { 0.0f, 1.0f } };
        rimQuad.Color = glm::vec4(0.40f, 0.38f, 0.35f, 0.40f);
        quads.push_back(rimQuad);
    }

    // 3. Player Center Pin
    const float playerHalfSize = (pinSize * 1.1f) * 0.5f * borderScale;
    UIQuad playerQuad{};
    playerQuad.Vertices[0] = { { center.x - playerHalfSize, center.y - playerHalfSize }, { 0.0f, 0.0f } };
    playerQuad.Vertices[1] = { { center.x + playerHalfSize, center.y - playerHalfSize }, { 1.0f, 0.0f } };
    playerQuad.Vertices[2] = { { center.x + playerHalfSize, center.y + playerHalfSize }, { 1.0f, 1.0f } };
    playerQuad.Vertices[3] = { { center.x - playerHalfSize, center.y + playerHalfSize }, { 0.0f, 1.0f } };
    playerQuad.Color = playerPinColor;
    quads.push_back(playerQuad);

    // 4. Discovered & Rim-Clamped POI Pins
    if (fogSystem) {
        const float activeRadius = radius * 0.90f;
        for (const auto& pin : fogSystem->GetActivePins()) {
            const glm::vec2 pinCenter = center + pin.MinimapLocalPos * activeRadius;
            const float halfSz = (pin.IsClampedToRim ? pinSize * 1.25f : pinSize) * 0.5f * borderScale;

            UIQuad pinQuad{};
            pinQuad.Vertices[0] = { { pinCenter.x - halfSz, pinCenter.y - halfSz }, { 0.0f, 0.0f } };
            pinQuad.Vertices[1] = { { pinCenter.x + halfSz, pinCenter.y - halfSz }, { 1.0f, 0.0f } };
            pinQuad.Vertices[2] = { { pinCenter.x + halfSz, pinCenter.y + halfSz }, { 1.0f, 1.0f } };
            pinQuad.Vertices[3] = { { pinCenter.x - halfSz, pinCenter.y + halfSz }, { 0.0f, 1.0f } };
            pinQuad.Color = pin.IconColor;
            quads.push_back(pinQuad);
        }
    }

    return quads;
}

} // namespace Engine
