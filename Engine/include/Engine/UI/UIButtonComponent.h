#ifndef UI_UIBUTTONCOMPONENT_H
#define UI_UIBUTTONCOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/UI/UICanvas.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <limits>

namespace Engine {

class Scene;

enum class ButtonState : uint8_t {
    Normal = 0,
    Hovered,
    Pressed,
    Disabled
};

enum class UIFocusDirection : uint8_t {
    Up = 0,
    Down,
    Left,
    Right
};

class UIButtonComponent final : public Component {
public:
    glm::vec4 NormalTint{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 HoverTint{ 1.15f, 1.15f, 1.15f, 1.0f };
    glm::vec4 PressedTint{ 0.85f, 0.85f, 0.85f, 1.0f };
    glm::vec4 DisabledTint{ 0.5f, 0.5f, 0.5f, 0.6f };
    glm::vec4 FocusedTint{ 1.25f, 1.20f, 0.85f, 1.0f }; // Warm amber glow for keyboard/controller focus

    bool Interactable = true;
    bool AutoTintPanel = true; // Automatically modulates sibling UIPanel::color based on button state

    // Neighbor links for directional focus graph
    uint64_t UpNeighborUUID = 0;
    uint64_t DownNeighborUUID = 0;
    uint64_t LeftNeighborUUID = 0;
    uint64_t RightNeighborUUID = 0;

    std::function<void()> OnClick = nullptr;

    UIButtonComponent() = default;
    ~UIButtonComponent() override;

    void SetNeighbors(uint64_t up, uint64_t down, uint64_t left, uint64_t right) noexcept {
        UpNeighborUUID = up;
        DownNeighborUUID = down;
        LeftNeighborUUID = left;
        RightNeighborUUID = right;
    }

    [[nodiscard]] ButtonState GetState() const noexcept { return m_State; }
    void SetState(ButtonState state) noexcept;

    [[nodiscard]] bool IsFocused() const noexcept { return m_IsFocused; }
    void SetFocused(bool focused) noexcept;

    void Click();

    // Focus graph navigation & active focus registry
    [[nodiscard]] static UIButtonComponent* GetFocusedButton() noexcept { return s_FocusedButton; }
    static void SetFocusedButton(UIButtonComponent* button) noexcept;
    static void ClearFocus() noexcept;

    static bool NavigateFocus(UIFocusDirection direction, const Scene& scene);
    static bool SubmitActiveFocus(); // Triggers Click() on s_FocusedButton if valid & interactable

    // Point test against this button's resolved screen rect (using sibling UIPanel's rectTransform)
    [[nodiscard]] bool ContainsScreenPoint(const glm::vec2& screenPt, uint32_t viewportW, uint32_t viewportH) const;

    [[nodiscard]] bool CanInteract() const noexcept;

    // Component lifecycle
    void Update(float deltaTime) override;
    void OnDisable() override;
    void OnPoolRelease() override;
    void OnDestroy() override;

private:
    void ClearFocusIfSelf() noexcept;
    void UpdateVisualTint();
    [[nodiscard]] static UIButtonComponent* ResolveTopmostHoveredButton(
        const Scene& scene, const glm::vec2& mousePos, uint32_t vpW, uint32_t vpH);

    ButtonState m_State = ButtonState::Normal;
    bool m_IsFocused = false;

    inline static UIButtonComponent* s_FocusedButton = nullptr;
    inline static uint64_t s_LastFocusStepFrame = std::numeric_limits<uint64_t>::max();
    inline static uint64_t s_LastSubmitFrame = std::numeric_limits<uint64_t>::max();
    inline static uint64_t s_TopmostEvalFrame = std::numeric_limits<uint64_t>::max();
    inline static const Scene* s_TopmostEvalScene = nullptr;
    inline static UIButtonComponent* s_TopmostHoveredButton = nullptr;
    inline static glm::vec2 s_TopmostEvalMousePos{ -1.0f, -1.0f };
    inline static uint32_t s_TopmostEvalVpW = 0;
    inline static uint32_t s_TopmostEvalVpH = 0;
};

} // namespace Engine

#endif // UI_UIBUTTONCOMPONENT_H
