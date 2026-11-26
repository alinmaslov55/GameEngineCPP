#include "Engine/UI/UIRadialWheelComponent.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Time.h"
#include <cmath>
#include <numbers>

namespace Engine {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kQuarterPi = 0.78539816339744830962f; // 45 deg
constexpr float kThreeQuarterPi = 2.35619449019234492885f; // 135 deg
} // namespace

UIRadialWheelComponent::UIRadialWheelComponent() {
    rectTransform.Anchor = UIAnchor::Center;
    rectTransform.Pivot = { 0.5f, 0.5f };
    rectTransform.AnchoredOffset = { 0.0f, 0.0f };
    rectTransform.Size = { 360.0f, 360.0f };
    rectTransform.ZOrder = 100; // Render above HUD
}

UIRadialWheelComponent::~UIRadialWheelComponent() {
    Close();
}

void UIRadialWheelComponent::OnDisable() {
    Close();
}

void UIRadialWheelComponent::OnPoolRelease() {
    Close();
}

void UIRadialWheelComponent::OnDestroy() {
    Close();
}

int UIRadialWheelComponent::ResolveWedgeFromAngle(float angleRad) noexcept {
    if (!std::isfinite(angleRad)) {
        return -1;
    }
    // Normalize to [-PI, PI] in O(1)
    angleRad = std::remainder(angleRad, 2.0f * kPi);

    // Wedge 0: Top [45 deg .. 135 deg]
    if (angleRad >= kQuarterPi && angleRad <= kThreeQuarterPi) {
        return 0;
    }
    // Wedge 1: Right [-45 deg .. 45 deg]
    if (angleRad >= -kQuarterPi && angleRad <= kQuarterPi) {
        return 1;
    }
    // Wedge 2: Bottom [-135 deg .. -45 deg]
    if (angleRad >= -kThreeQuarterPi && angleRad <= -kQuarterPi) {
        return 2;
    }
    // Wedge 3: Left (> 135 deg or < -135 deg)
    return 3;
}

int UIRadialWheelComponent::ResolveWedgeFromVector(const glm::vec2& dir, float deadzone) noexcept {
    const float len = glm::length(dir);
    if (len <= deadzone || len < 1e-5f) {
        return -1;
    }
    return ResolveWedgeFromAngle(std::atan2(dir.y, dir.x));
}

void UIRadialWheelComponent::Open() {
    if (IsOpen) return;
    PreviousTimeScale = Time::GetBaseTimeScale();
    Time::SetTimeScale(SlowMoTimeScale);
    IsOpen = true;
    ActiveWedge = -1;
    WasStickDeflected = false;
}

void UIRadialWheelComponent::Close() {
    if (!IsOpen) return;
    Time::SetTimeScale(PreviousTimeScale);
    IsOpen = false;
    ActiveWedge = -1;
    WasStickDeflected = false;
}

void UIRadialWheelComponent::Toggle() {
    if (IsOpen) {
        Close();
    } else {
        Open();
    }
}

bool UIRadialWheelComponent::ConfirmSelection(int wedgeIndex) {
    if (wedgeIndex < 0 || wedgeIndex >= static_cast<int>(Slots.size())) {
        return false;
    }

    if (AvailableWood < Slots[wedgeIndex].WoodCost) {
        return false;
    }

    const RadialWheelItem item = Slots[wedgeIndex].Item;
    auto confirmCb = OnConfirmDrop;
    // Close BEFORE invoking callback so TimeScale restore / wheel destruction cannot clobber or UAF
    Close();

    if (confirmCb) {
        confirmCb(wedgeIndex, item);
    }
    return true;
}

void UIRadialWheelComponent::Update(float deltaTime) {
    (void)deltaTime;

    // Toggle check (Tab or Gamepad Left Shoulder)
    if (Input::IsKeyJustPressed(SDL_SCANCODE_TAB) ||
        Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        Toggle();
        return;
    }

    if (!IsOpen) return;

    // Close on Escape or Gamepad B
    if (Input::IsKeyJustPressed(SDL_SCANCODE_ESCAPE) ||
        Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_EAST)) {
        Close();
        return;
    }

    // Instant numeric shortcuts [1..4]
    if (Input::IsKeyJustPressed(SDL_SCANCODE_1)) { ConfirmSelection(0); return; }
    if (Input::IsKeyJustPressed(SDL_SCANCODE_2)) { ConfirmSelection(1); return; }
    if (Input::IsKeyJustPressed(SDL_SCANCODE_3)) { ConfirmSelection(2); return; }
    if (Input::IsKeyJustPressed(SDL_SCANCODE_4)) { ConfirmSelection(3); return; }

    // Instant D-pad shortcuts
    if (Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_UP))    { ConfirmSelection(0); return; }
    if (Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) { ConfirmSelection(1); return; }
    if (Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN))  { ConfirmSelection(2); return; }
    if (Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT))  { ConfirmSelection(3); return; }

    // Stick resolution (Right stick prioritized, Left stick fallback)
    const glm::vec2 rStick(Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTX), -Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTY));
    const glm::vec2 lStick(Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX),  -Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY));
    const glm::vec2 stick = (glm::length(rStick) >= glm::length(lStick)) ? rStick : lStick;
    const float stickMag = glm::length(stick);

    if (stickMag > 0.35f) {
        ActiveWedge = ResolveWedgeFromAngle(std::atan2(stick.y, stick.x));
        WasStickDeflected = true;
    } else if (WasStickDeflected && stickMag < 0.15f) {
        // Stick release confirmation!
        if (ActiveWedge >= 0 && AvailableWood >= Slots[ActiveWedge].WoodCost) {
            ConfirmSelection(ActiveWedge);
            return;
        }
        WasStickDeflected = false;
    }

    // Mouse delta resolution (only when KeyboardMouse is active so parked mouse does not override gamepad)
    if (Input::GetActiveDeviceMode() == InputDeviceMode::KeyboardMouse && !WasStickDeflected) {
        const glm::vec2 mousePos = Input::GetMousePosition() - Input::GetViewportTopLeft();
        const glm::vec2 vpSize = Input::GetViewportSize();
        if (vpSize.x > 0.0f && vpSize.y > 0.0f) {
            const float uiScale = UICanvas::ComputeScaleFactor(static_cast<uint32_t>(vpSize.x), static_cast<uint32_t>(vpSize.y));
            const float scaledDeadzone = 35.0f * uiScale;
            const glm::vec2 mouseBottomUp(mousePos.x, vpSize.y - mousePos.y);
            glm::vec2 screenCenter = vpSize * 0.5f;
            if (useRectTransform) {
                glm::vec2 resolvedPos{ 0.0f }, resolvedSize{ 360.0f, 360.0f };
                UICanvas::ResolveScreenRect(rectTransform,
                                            static_cast<uint32_t>(vpSize.x),
                                            static_cast<uint32_t>(vpSize.y),
                                            resolvedPos,
                                            resolvedSize);
                screenCenter = resolvedPos + resolvedSize * 0.5f;
            }
            const glm::vec2 mouseDelta = mouseBottomUp - screenCenter;
            if (glm::length(mouseDelta) > scaledDeadzone) {
                ActiveWedge = ResolveWedgeFromVector(mouseDelta, 0.0f);
            } else {
                ActiveWedge = -1;
            }
        }
    }

    // Mouse click or Gamepad South confirm
    if (Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT) ||
        Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH)) {
        if (ActiveWedge >= 0) {
            ConfirmSelection(ActiveWedge);
        }
    }
}

std::vector<UIQuad> UIRadialWheelComponent::GenerateQuads(const glm::vec2& resolvedPos,
                                                          const glm::vec2& resolvedSize,
                                                          float borderScale) const {
    std::vector<UIQuad> quads;
    if (!IsOpen) return quads;

    const glm::vec2 center = resolvedPos + resolvedSize * 0.5f;
    const float radius = std::min(resolvedSize.x, resolvedSize.y) * 0.5f;

    // 1. Background disc quad
    UIQuad bgQuad{};
    bgQuad.Vertices[0] = { { resolvedPos.x,                     resolvedPos.y },                     { 0.0f, 0.0f } };
    bgQuad.Vertices[1] = { { resolvedPos.x + resolvedSize.x,     resolvedPos.y },                     { 1.0f, 0.0f } };
    bgQuad.Vertices[2] = { { resolvedPos.x + resolvedSize.x,     resolvedPos.y + resolvedSize.y },     { 1.0f, 1.0f } };
    bgQuad.Vertices[3] = { { resolvedPos.x,                     resolvedPos.y + resolvedSize.y },     { 0.0f, 1.0f } };
    if (WheelTexture && WheelTexture->GetRendererID() != 0) {
        bgQuad.Color = glm::vec4(1.0f);
        bgQuad.QuadTexture = WheelTexture;
    } else {
        bgQuad.Color = { 0.08f, 0.08f, 0.12f, 0.90f };
        bgQuad.QuadTexture = nullptr;
    }
    quads.push_back(bgQuad);

    // 2. 4 Quadrant Wedge highlights & item icons
    const glm::vec2 quadrantOffsets[4] = {
        {  0.0f,          0.48f * radius }, // Top
        {  0.48f * radius, 0.0f          }, // Right
        {  0.0f,         -0.48f * radius }, // Bottom
        { -0.48f * radius, 0.0f          }  // Left
    };

    const float itemSize = 48.0f * borderScale;
    const float halfItem = itemSize * 0.5f;

    for (size_t i = 0; i < Slots.size(); ++i) {
        const bool affordable = (AvailableWood >= Slots[i].WoodCost);
        const bool isActive = (static_cast<int>(i) == ActiveWedge);

        // Highlight quad
        UIQuad sectorQuad{};
        glm::vec4 sectorColor;
        if (isActive) {
            sectorColor = affordable ? glm::vec4(1.25f, 1.05f, 0.40f, 0.85f)
                                     : glm::vec4(0.80f, 0.25f, 0.25f, 0.85f);
        } else {
            sectorColor = affordable ? glm::vec4(0.20f, 0.24f, 0.32f, 0.55f)
                                     : glm::vec4(0.28f, 0.14f, 0.14f, 0.45f);
        }

        const glm::vec2 sectorCenter = center + quadrantOffsets[i];
        const float sectorRadius = itemSize * 0.75f;

        sectorQuad.Vertices[0] = { { sectorCenter.x - sectorRadius, sectorCenter.y - sectorRadius }, { 0.0f, 0.0f } };
        sectorQuad.Vertices[1] = { { sectorCenter.x + sectorRadius, sectorCenter.y - sectorRadius }, { 1.0f, 0.0f } };
        sectorQuad.Vertices[2] = { { sectorCenter.x + sectorRadius, sectorCenter.y + sectorRadius }, { 1.0f, 1.0f } };
        sectorQuad.Vertices[3] = { { sectorCenter.x - sectorRadius, sectorCenter.y + sectorRadius }, { 0.0f, 1.0f } };
        sectorQuad.Color = sectorColor;
        sectorQuad.QuadTexture = (HighlightTexture && HighlightTexture->GetRendererID() != 0) ? HighlightTexture : nullptr;
        quads.push_back(sectorQuad);

        // Item icon quad (prefer IconSubTex when provided)
        UIQuad iconQuad{};
        glm::vec2 uvMin{ 0.0f, 0.0f };
        glm::vec2 uvMax{ 1.0f, 1.0f };
        if (Slots[i].IconSubTex && Slots[i].IconSubTex->GetTexture()) {
            uvMin = Slots[i].IconSubTex->GetMin();
            uvMax = Slots[i].IconSubTex->GetMax();
            iconQuad.QuadTexture = Slots[i].IconSubTex->GetTexture();
        } else {
            iconQuad.QuadTexture = Slots[i].Icon;
        }
        iconQuad.Vertices[0] = { { sectorCenter.x - halfItem, sectorCenter.y - halfItem }, { uvMin.x, uvMin.y } };
        iconQuad.Vertices[1] = { { sectorCenter.x + halfItem, sectorCenter.y - halfItem }, { uvMax.x, uvMin.y } };
        iconQuad.Vertices[2] = { { sectorCenter.x + halfItem, sectorCenter.y + halfItem }, { uvMax.x, uvMax.y } };
        iconQuad.Vertices[3] = { { sectorCenter.x - halfItem, sectorCenter.y + halfItem }, { uvMin.x, uvMax.y } };

        if (affordable) {
            iconQuad.Color = isActive ? glm::vec4(1.30f, 1.20f, 0.80f, 1.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.95f);
        } else {
            iconQuad.Color = glm::vec4(0.55f, 0.25f, 0.25f, 0.55f);
        }
        quads.push_back(iconQuad);
    }

    return quads;
}

} // namespace Engine
