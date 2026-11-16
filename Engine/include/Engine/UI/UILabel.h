#ifndef UI_UILABEL_H
#define UI_UILABEL_H

#include "Engine/Scene/Component.h"
#include "Engine/UI/Font.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/Utils/Logger.h"
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Engine {

class UILabel final : public Component {
public:
    std::string text = "";
    std::shared_ptr<Font> font = nullptr;

    UIRectTransform rectTransform;
    bool useRectTransform = false;
    
    glm::vec2 position = { 0.0f, 0.0f };
    float scale = 1.0f;
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    TextAlignH alignment = TextAlignH::Left;
    float maxWrapWidth = 0.0f; // > 0 enables greedy word-wrapping in Reference Units
    bool dropShadow = true;
    glm::vec2 shadowOffset{ 2.0f, -2.0f };
    glm::vec4 shadowColor{ 0.04f, 0.02f, 0.06f, 0.85f };

    UILabel() = default;

    UILabel(const std::string& str, const std::shared_ptr<Font>& f, const UIRectTransform& rect, const glm::vec4& col = glm::vec4(1.0f))
        : text(str), font(f), rectTransform(rect), useRectTransform(true), color(col) {}

    UILabel(const std::string& str, const std::shared_ptr<Font>& f, const glm::vec2& pos)
        : text(str), font(f), position(pos), useRectTransform(false) {
        rectTransform.Anchor = UIAnchor::BottomLeft;
        rectTransform.Pivot = { 0.0f, 0.0f };
        rectTransform.AnchoredOffset = pos;
        ENGINE_CORE_DEBUG("UILabel created with text '{}' at ({:.1f}, {:.1f})", str, pos.x, pos.y);
    }
};

} // namespace Engine

#endif // UI_UILABEL_H

