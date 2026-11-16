#ifndef UI_UIMINIMAPCOMPONENT_H
#define UI_UIMINIMAPCOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/UI/MinimapFogSystem.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/UI/UIPanel.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Engine {

class UIMinimapComponent final : public Component {
public:
    UIRectTransform rectTransform;
    bool useRectTransform = true;

    std::shared_ptr<MinimapFogSystem> fogSystem{ nullptr };
    std::shared_ptr<Texture> frameTexture{ nullptr };

    glm::vec4 frameColor{ 0.75f, 0.70f, 0.60f, 1.0f };
    glm::vec4 playerPinColor{ 0.95f, 0.95f, 1.0f, 1.0f };
    float pinSize = 8.0f;

    UIMinimapComponent();

    // Generates UI quads: fog exploration map, circular frame overlay, and rim-clamped POI pins
    [[nodiscard]] std::vector<UIQuad> GenerateQuads(const glm::vec2& resolvedPos,
                                                    const glm::vec2& resolvedSize,
                                                    float borderScale = 1.0f) const;
};

} // namespace Engine

#endif // UI_UIMINIMAPCOMPONENT_H
