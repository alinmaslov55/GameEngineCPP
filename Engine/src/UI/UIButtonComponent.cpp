#include "Engine/UI/UIButtonComponent.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Time.h"

namespace Engine {

UIButtonComponent::~UIButtonComponent() {
    ClearFocusIfSelf();
}

void UIButtonComponent::ClearFocusIfSelf() noexcept {
    if (s_FocusedButton == this) {
        s_FocusedButton = nullptr;
        m_IsFocused = false;
    }
    if (s_TopmostHoveredButton == this) {
        s_TopmostHoveredButton = nullptr;
    }
}

bool UIButtonComponent::CanInteract() const noexcept {
    if (!Interactable || !IsEnabled()) return false;
    const auto* go = GetGameObject();
    return go && go->IsActiveInHierarchy() && !go->IsPendingDestroy();
}

void UIButtonComponent::SetState(ButtonState state) noexcept {
    m_State = state;
    UpdateVisualTint();
}

void UIButtonComponent::SetFocused(bool focused) noexcept {
    if (m_IsFocused != focused) {
        m_IsFocused = focused;
        UpdateVisualTint();
    }
}

void UIButtonComponent::SetFocusedButton(UIButtonComponent* button) noexcept {
    if (button && !button->CanInteract()) return;
    if (s_FocusedButton == button) return;
    if (s_FocusedButton) {
        s_FocusedButton->SetFocused(false);
    }
    s_FocusedButton = button;
    if (s_FocusedButton) {
        s_FocusedButton->SetFocused(true);
    }
}

void UIButtonComponent::ClearFocus() noexcept {
    if (s_FocusedButton) {
        s_FocusedButton->SetFocused(false);
        s_FocusedButton = nullptr;
    }
}

void UIButtonComponent::Click() {
    if (!CanInteract()) return;
    if (OnClick) {
        OnClick();
    }
}

bool UIButtonComponent::SubmitActiveFocus() {
    const uint64_t currentFrame = Time::GetFrameCount();
    if (s_FocusedButton && s_FocusedButton->CanInteract() && s_LastSubmitFrame != currentFrame) {
        s_LastSubmitFrame = currentFrame;
        s_FocusedButton->Click();
        return true;
    }
    return false;
}

bool UIButtonComponent::NavigateFocus(UIFocusDirection direction, const Scene& scene) {
    if (!s_FocusedButton) {
        for (const auto& go : scene.GetGameObjects()) {
            if (!go || !go->IsActiveInHierarchy()) continue;
            if (auto* btn = go->GetComponent<UIButtonComponent>()) {
                if (btn->CanInteract()) {
                    SetFocusedButton(btn);
                    return true;
                }
            }
        }
        return false;
    }

    uint64_t targetUUID = 0;
    switch (direction) {
        case UIFocusDirection::Up:    targetUUID = s_FocusedButton->UpNeighborUUID; break;
        case UIFocusDirection::Down:  targetUUID = s_FocusedButton->DownNeighborUUID; break;
        case UIFocusDirection::Left:  targetUUID = s_FocusedButton->LeftNeighborUUID; break;
        case UIFocusDirection::Right: targetUUID = s_FocusedButton->RightNeighborUUID; break;
    }

    if (targetUUID == 0) {
        return false;
    }

    GameObject* targetObj = scene.FindGameObjectByUUID(targetUUID);
    if (!targetObj || !targetObj->IsActiveInHierarchy()) {
        return false;
    }

    auto* targetBtn = targetObj->GetComponent<UIButtonComponent>();
    if (!targetBtn || !targetBtn->CanInteract()) {
        return false;
    }

    SetFocusedButton(targetBtn);
    return true;
}

bool UIButtonComponent::ContainsScreenPoint(const glm::vec2& screenPt, uint32_t viewportW, uint32_t viewportH) const {
    auto* go = GetGameObject();
    if (!go) return false;
    glm::vec2 pos{ 0.0f }, sz{ 0.0f };
    if (auto* panel = go->GetComponent<UIPanel>()) {
        if (panel->useRectTransform) {
            UICanvas::ResolveScreenRect(panel->rectTransform, viewportW, viewportH, pos, sz);
        } else {
            pos = panel->position;
            sz = panel->size;
        }
    } else {
        return false;
    }
    return screenPt.x >= pos.x && screenPt.x <= (pos.x + sz.x) &&
           screenPt.y >= pos.y && screenPt.y <= (pos.y + sz.y);
}

void UIButtonComponent::UpdateVisualTint() {
    auto* go = GetGameObject();
    if (!AutoTintPanel || !go) return;
    if (auto* panel = go->GetComponent<UIPanel>()) {
        if (!Interactable) {
            panel->color = DisabledTint;
        } else if (m_State == ButtonState::Pressed) {
            panel->color = PressedTint;
        } else if (m_IsFocused) {
            panel->color = FocusedTint;
        } else if (m_State == ButtonState::Hovered) {
            panel->color = HoverTint;
        } else {
            panel->color = NormalTint;
        }
    }
}

UIButtonComponent* UIButtonComponent::ResolveTopmostHoveredButton(
    const Scene& scene, const glm::vec2& mousePos, uint32_t vpW, uint32_t vpH) {
    const uint64_t currentFrame = Time::GetFrameCount();
    if (s_TopmostEvalFrame == currentFrame &&
        s_TopmostEvalScene == &scene &&
        s_TopmostEvalVpW == vpW &&
        s_TopmostEvalVpH == vpH &&
        s_TopmostEvalMousePos == mousePos) {
        return s_TopmostHoveredButton;
    }

    s_TopmostEvalFrame = currentFrame;
    s_TopmostEvalScene = &scene;
    s_TopmostEvalMousePos = mousePos;
    s_TopmostEvalVpW = vpW;
    s_TopmostEvalVpH = vpH;
    s_TopmostHoveredButton = nullptr;

    int32_t bestZOrder = std::numeric_limits<int32_t>::min();
    for (const auto& go : scene.GetGameObjects()) {
        if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) continue;
        auto* btn = go->GetComponent<UIButtonComponent>();
        if (!btn || !btn->CanInteract()) continue;
        if (!btn->ContainsScreenPoint(mousePos, vpW, vpH)) continue;

        int32_t zOrder = 0;
        if (const auto* panel = go->GetComponent<UIPanel>()) {
            zOrder = panel->rectTransform.ZOrder;
        }
        // >= tie-breaks in favor of later draw order (on top)
        if (!s_TopmostHoveredButton || zOrder >= bestZOrder) {
            bestZOrder = zOrder;
            s_TopmostHoveredButton = btn;
        }
    }
    return s_TopmostHoveredButton;
}

void UIButtonComponent::Update(float /*deltaTime*/) {
    if (!CanInteract()) {
        ClearFocusIfSelf();
        SetState(ButtonState::Disabled);
        return;
    }

    auto* go = GetGameObject();
    const uint64_t currentFrame = Time::GetFrameCount();

    // Directional navigation processing (at most one focus step per frame to prevent cascading)
    if (s_FocusedButton == this && s_LastFocusStepFrame != currentFrame && go && go->GetScene()) {
        const Scene& scene = *go->GetScene();
        bool navigated = false;
        if (Input::IsKeyJustPressed(SDL_SCANCODE_W) || Input::IsKeyJustPressed(SDL_SCANCODE_UP) ||
            Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            navigated = NavigateFocus(UIFocusDirection::Up, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_S) || Input::IsKeyJustPressed(SDL_SCANCODE_DOWN) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            navigated = NavigateFocus(UIFocusDirection::Down, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_A) || Input::IsKeyJustPressed(SDL_SCANCODE_LEFT) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            navigated = NavigateFocus(UIFocusDirection::Left, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_D) || Input::IsKeyJustPressed(SDL_SCANCODE_RIGHT) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            navigated = NavigateFocus(UIFocusDirection::Right, scene);
        }

        if (navigated) {
            s_LastFocusStepFrame = currentFrame;
        } else if (s_LastSubmitFrame != currentFrame &&
                   (Input::IsKeyJustPressed(SDL_SCANCODE_RETURN) || Input::IsKeyJustPressed(SDL_SCANCODE_SPACE) ||
                    Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH))) {
            s_LastSubmitFrame = currentFrame;
            Click();
        }
    }

    // Mouse point testing & interaction (arbitrated by ZOrder so occluded buttons do not fire)
    glm::vec2 mousePos = Input::GetMousePosition() - Input::GetViewportTopLeft();
    glm::vec2 vpSize = Input::GetViewportSize();
    if (vpSize.x <= 0.0f) vpSize.x = 1920.0f;
    if (vpSize.y <= 0.0f) vpSize.y = 1080.0f;
    mousePos.y = vpSize.y - mousePos.y; // Convert top-left mouse to bottom-up screen coordinates

    const uint32_t vpW = static_cast<uint32_t>(vpSize.x);
    const uint32_t vpH = static_cast<uint32_t>(vpSize.y);
    const bool inside = (go && go->GetScene())
        ? (ResolveTopmostHoveredButton(*go->GetScene(), mousePos, vpW, vpH) == this)
        : ContainsScreenPoint(mousePos, vpW, vpH);
    if (inside) {
        if (Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
            SetFocusedButton(this);
            SetState(ButtonState::Pressed);
        } else if (Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT)) {
            if (m_State == ButtonState::Pressed) {
                Click();
            }
            SetState(ButtonState::Hovered);
        } else if (m_State != ButtonState::Pressed) {
            SetState(ButtonState::Hovered);
        }
    } else {
        if (m_State == ButtonState::Pressed && Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT)) {
            SetState(ButtonState::Normal);
        } else if (m_State == ButtonState::Hovered) {
            SetState(ButtonState::Normal);
        }
    }

    UpdateVisualTint();
}

void UIButtonComponent::OnDisable() {
    ClearFocusIfSelf();
}

void UIButtonComponent::OnPoolRelease() {
    ClearFocusIfSelf();
    m_State = ButtonState::Normal;
}

void UIButtonComponent::OnDestroy() {
    ClearFocusIfSelf();
}

} // namespace Engine
