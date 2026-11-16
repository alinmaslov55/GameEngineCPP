#ifndef UI_UIPANEL_H
#define UI_UIPANEL_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/Utils/Logger.h"
#include <glm/glm.hpp>
#include <memory>

namespace Engine {

class UIPanel final : public Component {
public:
    UIRectTransform rectTransform;
    bool useRectTransform = true;

    // Screen space coordinates (raw pixels, used when useRectTransform == false)
    glm::vec2 position = { 0.0f, 0.0f }; 
    glm::vec2 size = { 100.0f, 100.0f }; 
    
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::shared_ptr<Texture> texture = nullptr;

    UIPanel() = default;

    UIPanel(const UIRectTransform& rect, const glm::vec4& col, const std::shared_ptr<Texture>& tex = nullptr)
        : rectTransform(rect), useRectTransform(true), color(col), texture(tex) {}

    UIPanel(const glm::vec2& pos, const glm::vec2& sz, const glm::vec4& col)
        : position(pos), size(sz), color(col), useRectTransform(false) {
        rectTransform.Anchor = UIAnchor::BottomLeft;
        rectTransform.Pivot = { 0.0f, 0.0f };
        rectTransform.AnchoredOffset = pos;
        rectTransform.Size = sz;
        ENGINE_CORE_DEBUG("UIPanel created at ({:.1f}, {:.1f}) with size ({:.1f}, {:.1f})", pos.x, pos.y, sz.x, sz.y);
    }
};

} // namespace Engine

#endif // UI_UIPANEL_H
