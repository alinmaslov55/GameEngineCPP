#ifndef UI_UIPANEL_H
#define UI_UIPANEL_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Utils/Logger.h"
#include <glm/glm.hpp>
#include <memory>

namespace Engine {
class UIPanel : public Component {
public:
    // Screen space coordinates (pixels)
    // By standard convention, (0,0) is often the bottom-left or top-left depending on GL setup.
    // assume bottom-left is (0,0) for standard OpenGL.
    glm::vec2 position = { 0.0f, 0.0f }; 
    glm::vec2 size = { 100.0f, 100.0f }; 
    
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::shared_ptr<Texture> texture = nullptr;

    UIPanel(const glm::vec2& pos, const glm::vec2& sz, const glm::vec4& col)
        : position(pos), size(sz), color(col) {
        ENGINE_CORE_DEBUG("UIPanel created at ({:.1f}, {:.1f}) with size ({:.1f}, {:.1f})", pos.x, pos.y, sz.x, sz.y);
    }
};
} // namespace Engine

#endif // UI_UIPANEL_H
