#ifndef UI_UIPANEL_H
#define UI_UIPANEL_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/UI/UICanvas.h"
#include "Engine/Utils/Logger.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Engine {

enum class UIRenderMode : uint8_t {
    StretchQuad    = 0,
    NineSlice      = 1, // Emits 9 quads preserving unscaled corner borders (Left, Right, Top, Bottom px)
    HorizontalFill = 2, // Clips quad width & UV.x by FillAmount [0..1] (HP bar)
    VerticalFill   = 3, // Clips quad height & UV.y by FillAmount [0..1] (Liquid fire Torch Vessel!)
    RadialSweepCW  = 4  // Triangle-fan polar clip [0..2*PI * FillAmount] (Rune Cooldown Sockets!)
};

struct UIQuadVertex {
    glm::vec2 Position{ 0.0f }; // Screen pixels (bottom-left origin)
    glm::vec2 TexCoord{ 0.0f }; // Normalized UV [0..1] (bottom-left origin)
};

struct UIQuad {
    UIQuadVertex Vertices[4];   // 0: Bottom-Left, 1: Bottom-Right, 2: Top-Right, 3: Top-Left
    glm::vec4 Color{ 1.0f };
};

class UIPanel final : public Component {
public:
    UIRectTransform rectTransform;
    bool useRectTransform = true;

    // Screen space coordinates (raw pixels, used when useRectTransform == false)
    glm::vec2 position = { 0.0f, 0.0f }; 
    glm::vec2 size = { 100.0f, 100.0f }; 
    
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::shared_ptr<Texture> texture = nullptr;

    UIRenderMode renderMode = UIRenderMode::StretchQuad;
    glm::vec4 nineSliceBordersPx{ 16.0f }; // Left, Right, Top, Bottom in source texture pixels
    float fillAmount = 1.0f;               // [0.0f .. 1.0f]
    bool invertFill = false;

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

    [[nodiscard]] float BorderLeft() const noexcept { return nineSliceBordersPx.x; }
    [[nodiscard]] float BorderRight() const noexcept { return nineSliceBordersPx.y; }
    [[nodiscard]] float BorderTop() const noexcept { return nineSliceBordersPx.z; }
    [[nodiscard]] float BorderBottom() const noexcept { return nineSliceBordersPx.w; }

    [[nodiscard]] std::vector<UIQuad> GenerateQuads(const glm::vec2& resolvedPos, const glm::vec2& resolvedSize, float borderScale = 1.0f) const;
};

} // namespace Engine

#endif // UI_UIPANEL_H
