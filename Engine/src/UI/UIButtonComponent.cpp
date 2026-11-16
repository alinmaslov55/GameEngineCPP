#include "Engine/UI/UIButtonComponent.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Core/Input.h"

namespace Engine {

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
    SetFocusedButton(nullptr);
}

void UIButtonComponent::Click() {
    if (!Interactable) return;
    if (OnClick) {
        OnClick();
    }
}

bool UIButtonComponent::SubmitActiveFocus() {
    if (s_FocusedButton && s_FocusedButton->Interactable) {
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
                if (btn->Interactable) {
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
    if (!targetBtn || !targetBtn->Interactable) {
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

void UIButtonComponent::Update(float /*deltaTime*/) {
    if (!Interactable) {
        SetState(ButtonState::Disabled);
        return;
    }

    auto* go = GetGameObject();

    // Directional navigation processing (delegated to focused button)
    if (s_FocusedButton == this && go && go->GetScene()) {
        const Scene& scene = *go->GetScene();
        if (Input::IsKeyJustPressed(SDL_SCANCODE_W) || Input::IsKeyJustPressed(SDL_SCANCODE_UP) ||
            Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            NavigateFocus(UIFocusDirection::Up, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_S) || Input::IsKeyJustPressed(SDL_SCANCODE_DOWN) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            NavigateFocus(UIFocusDirection::Down, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_A) || Input::IsKeyJustPressed(SDL_SCANCODE_LEFT) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            NavigateFocus(UIFocusDirection::Left, scene);
        } else if (Input::IsKeyJustPressed(SDL_SCANCODE_D) || Input::IsKeyJustPressed(SDL_SCANCODE_RIGHT) ||
                   Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            NavigateFocus(UIFocusDirection::Right, scene);
        }

        if (Input::IsKeyJustPressed(SDL_SCANCODE_RETURN) || Input::IsKeyJustPressed(SDL_SCANCODE_SPACE) ||
            Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH)) {
            Click();
        }
    }

    // Mouse point testing & interaction
    glm::vec2 mousePos = Input::GetMousePosition() - Input::GetViewportTopLeft();
    glm::vec2 vpSize = Input::GetViewportSize();
    if (vpSize.x <= 0.0f) vpSize.x = 1920.0f;
    if (vpSize.y <= 0.0f) vpSize.y = 1080.0f;
    mousePos.y = vpSize.y - mousePos.y; // Convert top-left mouse to bottom-up screen coordinates

    const bool inside = ContainsScreenPoint(mousePos, static_cast<uint32_t>(vpSize.x), static_cast<uint32_t>(vpSize.y));
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

void UIButtonComponent::OnDestroy() {
    if (s_FocusedButton == this) {
        s_FocusedButton = nullptr;
    }
}

} // namespace Engine
