#ifndef UI_UILABEL_H
#define UI_UILABEL_H

#include "Engine/Scene/Component.h"
#include "Engine/UI/Font.h"
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Engine {
class UILabel : public Component {
public:
    std::string text = "";
    std::shared_ptr<Font> font = nullptr;
    
    glm::vec2 position = { 0.0f, 0.0f };
    float scale = 1.0f;
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    UILabel(const std::string& str, const std::shared_ptr<Font>& f, const glm::vec2& pos)
        : text(str), font(f), position(pos) {}
};
} // namespace Engine

#endif // UI_UILABEL_H

