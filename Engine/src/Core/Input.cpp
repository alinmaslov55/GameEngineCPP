#include "Engine/Core/Input.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Utils/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Engine {

namespace {

constexpr size_t kMaxMouseButtons = 8; // SDL mouse buttons 1..5 (+ headroom)
constexpr float kTriggerPressThreshold = 0.5f;
constexpr float kDeviceSwitchAxisThreshold = 0.25f;
constexpr float kDeviceSwitchMouseMotionThreshold = 2.0f;
constexpr float kLegacySingleAxisDeadzone = 8000.0f / 32767.0f;
constexpr float kAimStickDeadzone = 0.20f;

struct TransparentStringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    size_t operator()(const char* s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct InputState {
    std::array<bool, SDL_SCANCODE_COUNT> CurrKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> PrevKeys{};

    std::array<bool, kMaxMouseButtons> CurrMouseButtons{};
    std::array<bool, kMaxMouseButtons> PrevMouseButtons{};
    glm::vec2 MousePosition{0.0f, 0.0f};
    float ScrollDeltaX = 0.0f;
    float ScrollDeltaY = 0.0f;

    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> CurrPadButtons{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> PrevPadButtons{};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> CurrPadAxes{};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> PrevPadAxes{};

    InputDeviceMode ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    bool GamepadConnectedForTesting = false;

    glm::vec2 ViewportTopLeft{ 0.0f, 0.0f };
    glm::vec2 ViewportSize{ 0.0f, 0.0f };

    float LastRumbleLow01 = 0.0f;
    float LastRumbleHigh01 = 0.0f;
    uint32_t LastRumbleDurationMs = 0;
    uint32_t RumbleCallCount = 0;

    std::unordered_map<std::string, ActionBinding, TransparentStringHash, std::equal_to<>> ActionBindings;

    void ResetBuffers() noexcept {
        CurrKeys.fill(false);
        PrevKeys.fill(false);
        CurrMouseButtons.fill(false);
        PrevMouseButtons.fill(false);
        MousePosition = { 0.0f, 0.0f };
        ScrollDeltaX = 0.0f;
        ScrollDeltaY = 0.0f;
        CurrPadButtons.fill(false);
        PrevPadButtons.fill(false);
        CurrPadAxes.fill(0.0f);
        PrevPadAxes.fill(0.0f);
        ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
        GamepadConnectedForTesting = false;
        ViewportTopLeft = { 0.0f, 0.0f };
        ViewportSize = { 0.0f, 0.0f };
        LastRumbleLow01 = 0.0f;
        LastRumbleHigh01 = 0.0f;
        LastRumbleDurationMs = 0;
        RumbleCallCount = 0;
    }
};

InputState s_State;

bool IsValidScancode(SDL_Scancode key) noexcept {
    return key > SDL_SCANCODE_UNKNOWN && static_cast<int>(key) < SDL_SCANCODE_COUNT;
}

bool IsValidMouseButton(int button) noexcept {
    return button >= 1 && static_cast<size_t>(button) < kMaxMouseButtons;
}

bool IsValidGamepadButton(SDL_GamepadButton btn) noexcept {
    return btn > SDL_GAMEPAD_BUTTON_INVALID && static_cast<int>(btn) < SDL_GAMEPAD_BUTTON_COUNT;
}

bool IsValidGamepadAxis(SDL_GamepadAxis axis) noexcept {
    return axis > SDL_GAMEPAD_AXIS_INVALID && static_cast<int>(axis) < SDL_GAMEPAD_AXIS_COUNT;
}

glm::vec2 NormalizeXZOrDefault(glm::vec2 vec, glm::vec2 defaultDir = { 0.0f, -1.0f }) noexcept {
    if (!std::isfinite(vec.x) || !std::isfinite(vec.y)) {
        return defaultDir;
    }
    const float lenSq = vec.x * vec.x + vec.y * vec.y;
    if (lenSq <= 1e-6f) {
        return defaultDir;
    }
    return vec / std::sqrt(lenSq);
}

glm::vec2 ProjectStickToCameraXZ(const PerspectiveCamera& camera, glm::vec2 stick2D) noexcept {
    const glm::vec3& rightXZ = camera.GetRightXZ();
    const glm::vec3& forwardXZ = camera.GetForwardXZ();
    return {
        rightXZ.x * stick2D.x + forwardXZ.x * stick2D.y,
        rightXZ.z * stick2D.x + forwardXZ.z * stick2D.y
    };
}

bool EvaluateBinding(const ActionBinding& binding, bool usePrevious) noexcept {
    const auto& keys = usePrevious ? s_State.PrevKeys : s_State.CurrKeys;
    const auto& mouse = usePrevious ? s_State.PrevMouseButtons : s_State.CurrMouseButtons;
    const auto& padBtns = usePrevious ? s_State.PrevPadButtons : s_State.CurrPadButtons;
    const auto& padAxes = usePrevious ? s_State.PrevPadAxes : s_State.CurrPadAxes;

    if (IsValidScancode(binding.PrimaryKey) && keys[static_cast<size_t>(binding.PrimaryKey)]) {
        return true;
    }
    if (IsValidScancode(binding.SecondaryKey) && keys[static_cast<size_t>(binding.SecondaryKey)]) {
        return true;
    }
    if (IsValidMouseButton(binding.MouseButton) && mouse[static_cast<size_t>(binding.MouseButton)]) {
        return true;
    }
    if (IsValidGamepadButton(binding.PadButton) && padBtns[static_cast<size_t>(binding.PadButton)]) {
        return true;
    }
    if (IsValidGamepadAxis(binding.PadTriggerAxis) &&
        padAxes[static_cast<size_t>(binding.PadTriggerAxis)] > kTriggerPressThreshold) {
        return true;
    }
    return false;
}

void RegisterDefaultBindings() {
    Input::BindAction("Jump", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_SPACE,
        .PadButton = SDL_GAMEPAD_BUTTON_SOUTH
    });
    Input::BindAction("Attack", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_J,
        .MouseButton = SDL_BUTTON_LEFT,
        .PadButton = SDL_GAMEPAD_BUTTON_WEST
    });
    Input::BindAction("Dodge", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_LSHIFT,
        .PadButton = SDL_GAMEPAD_BUTTON_EAST
    });
    Input::BindAction("Interact", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_E,
        .PadButton = SDL_GAMEPAD_BUTTON_NORTH
    });
}

nlohmann::json SerializeBinding(const ActionBinding& binding) {
    nlohmann::json j;
    j["primaryKey"] = static_cast<int>(binding.PrimaryKey);
    if (IsValidScancode(binding.PrimaryKey)) {
        if (const char* name = SDL_GetScancodeName(binding.PrimaryKey); name && name[0] != '\0') {
            j["primaryKeyName"] = name;
        }
    }
    j["secondaryKey"] = static_cast<int>(binding.SecondaryKey);
    if (IsValidScancode(binding.SecondaryKey)) {
        if (const char* name = SDL_GetScancodeName(binding.SecondaryKey); name && name[0] != '\0') {
            j["secondaryKeyName"] = name;
        }
    }
    j["mouseButton"] = binding.MouseButton;

    const char* padBtnName = IsValidGamepadButton(binding.PadButton)
        ? SDL_GetGamepadStringForButton(binding.PadButton)
        : nullptr;
    j["padButton"] = (padBtnName && padBtnName[0] != '\0') ? std::string(padBtnName) : std::string();

    const char* padAxisName = IsValidGamepadAxis(binding.PadTriggerAxis)
        ? SDL_GetGamepadStringForAxis(binding.PadTriggerAxis)
        : nullptr;
    j["padTriggerAxis"] = (padAxisName && padAxisName[0] != '\0') ? std::string(padAxisName) : std::string();
    return j;
}

SDL_Scancode ParseScancodeField(const nlohmann::json& j, const char* idKey, const char* nameKey) {
    if (j.contains(idKey)) {
        const auto& val = j.at(idKey);
        if (val.is_number_integer()) {
            const int code = val.get<int>();
            if (code >= 0 && code < SDL_SCANCODE_COUNT) {
                return static_cast<SDL_Scancode>(code);
            }
        } else if (val.is_string()) {
            const std::string& name = val.get_ref<const std::string&>();
            if (!name.empty()) {
                return SDL_GetScancodeFromName(name.c_str());
            }
        }
    }
    if (nameKey && j.contains(nameKey) && j.at(nameKey).is_string()) {
        const std::string& name = j.at(nameKey).get_ref<const std::string&>();
        if (!name.empty()) {
            return SDL_GetScancodeFromName(name.c_str());
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

SDL_GamepadButton ParseGamepadButtonField(const nlohmann::json& j) {
    if (!j.contains("padButton")) {
        return SDL_GAMEPAD_BUTTON_INVALID;
    }
    const auto& val = j.at("padButton");
    if (val.is_string()) {
        const std::string& str = val.get_ref<const std::string&>();
        return str.empty() ? SDL_GAMEPAD_BUTTON_INVALID : SDL_GetGamepadButtonFromString(str.c_str());
    }
    if (val.is_number_integer()) {
        const int id = val.get<int>();
        if (id >= 0 && id < SDL_GAMEPAD_BUTTON_COUNT) {
            return static_cast<SDL_GamepadButton>(id);
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

SDL_GamepadAxis ParseGamepadAxisField(const nlohmann::json& j) {
    if (!j.contains("padTriggerAxis")) {
        return SDL_GAMEPAD_AXIS_INVALID;
    }
    const auto& val = j.at("padTriggerAxis");
    if (val.is_string()) {
        const std::string& str = val.get_ref<const std::string&>();
        return str.empty() ? SDL_GAMEPAD_AXIS_INVALID : SDL_GetGamepadAxisFromString(str.c_str());
    }
    if (val.is_number_integer()) {
        const int id = val.get<int>();
        if (id >= 0 && id < SDL_GAMEPAD_AXIS_COUNT) {
            return static_cast<SDL_GamepadAxis>(id);
        }
    }
    return SDL_GAMEPAD_AXIS_INVALID;
}

} // namespace

SDL_Gamepad* Input::s_Gamepad{nullptr};

void Input::Init() {
    ENGINE_CORE_INFO("Initializing Input system...");
    s_State.ResetBuffers();
    s_State.ActionBindings.clear();
    RegisterDefaultBindings();

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        ENGINE_CORE_ERROR("Failed to initialize Gamepad subsystem: {}", SDL_GetError());
    }
    ConnectFirstGamepad();
    ENGINE_CORE_INFO("Input system initialized");
}

void Input::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Input system...");
    if (s_Gamepad) {
        SDL_CloseGamepad(s_Gamepad);
        s_Gamepad = nullptr;
    }
    s_State.ResetBuffers();
    s_State.ActionBindings.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    ENGINE_CORE_INFO("Input system shut down");
}

void Input::BeginFrame() {
    s_State.PrevKeys = s_State.CurrKeys;
    s_State.PrevMouseButtons = s_State.CurrMouseButtons;
    s_State.PrevPadButtons = s_State.CurrPadButtons;
    s_State.PrevPadAxes = s_State.CurrPadAxes;
    s_State.ScrollDeltaX = 0.0f;
    s_State.ScrollDeltaY = 0.0f;
}

void Input::Update() {
    ENGINE_CORE_TRACE("Input::Update");

    int numKeys = 0;
    const bool* keyState = SDL_GetKeyboardState(&numKeys);
    if (keyState && numKeys > 0) {
        const size_t count = std::min(static_cast<size_t>(numKeys), s_State.CurrKeys.size());
        std::copy_n(keyState, count, s_State.CurrKeys.begin());
        if (count < s_State.CurrKeys.size()) {
            std::fill(s_State.CurrKeys.begin() + static_cast<ptrdiff_t>(count), s_State.CurrKeys.end(), false);
        }
    }

    float mx = 0.0f;
    float my = 0.0f;
    const SDL_MouseButtonFlags mouseMask = SDL_GetMouseState(&mx, &my);
    s_State.MousePosition = { mx, my };
    for (size_t b = 1; b < kMaxMouseButtons; ++b) {
        s_State.CurrMouseButtons[b] = (mouseMask & SDL_BUTTON_MASK(static_cast<int>(b))) != 0;
    }

    if (s_Gamepad) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            s_State.CurrPadButtons[static_cast<size_t>(b)] =
                SDL_GetGamepadButton(s_Gamepad, static_cast<SDL_GamepadButton>(b));
        }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            const Sint16 raw = SDL_GetGamepadAxis(s_Gamepad, static_cast<SDL_GamepadAxis>(a));
            const float normalized = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
            s_State.CurrPadAxes[static_cast<size_t>(a)] = normalized;
        }
    } else if (!s_State.GamepadConnectedForTesting) {
        s_State.CurrPadButtons.fill(false);
        s_State.CurrPadAxes.fill(0.0f);
    }
}

void Input::OnMouseWheel(float wheelX, float wheelY) {
    s_State.ScrollDeltaX += wheelX;
    s_State.ScrollDeltaY += wheelY;
    if (wheelX != 0.0f || wheelY != 0.0f) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::OnKeyEvent(bool pressed) {
    if (pressed) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::OnMouseButtonEvent(bool pressed) {
    if (pressed) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::OnMouseMotionEvent(float relX, float relY) {
    if (std::abs(relX) + std::abs(relY) > kDeviceSwitchMouseMotionThreshold) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::OnGamepadButtonEvent(bool pressed) {
    if (pressed) {
        s_State.ActiveDeviceMode = InputDeviceMode::Gamepad;
    }
}

void Input::OnGamepadAxisEvent(SDL_GamepadAxis axis, float normalizedValue) {
    if (!IsValidGamepadAxis(axis)) {
        return;
    }
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        if (normalizedValue > kDeviceSwitchAxisThreshold) {
            s_State.ActiveDeviceMode = InputDeviceMode::Gamepad;
        }
    } else if (std::abs(normalizedValue) > kDeviceSwitchAxisThreshold) {
        s_State.ActiveDeviceMode = InputDeviceMode::Gamepad;
    }
}

void Input::OnGamepadAdded(SDL_JoystickID which) {
    if (!s_Gamepad) {
        s_Gamepad = SDL_OpenGamepad(which);
        if (s_Gamepad) {
            ENGINE_CORE_INFO("Connected gamepad: {}", SDL_GetGamepadName(s_Gamepad));
        }
    }
}

void Input::OnGamepadRemoved(SDL_JoystickID which) {
    if (s_Gamepad && SDL_GetGamepadID(s_Gamepad) == which) {
        ENGINE_CORE_INFO("Disconnected gamepad: {}", SDL_GetGamepadName(s_Gamepad));
        SDL_CloseGamepad(s_Gamepad);
        s_Gamepad = nullptr;
        s_State.CurrPadButtons.fill(false);
        s_State.CurrPadAxes.fill(0.0f);
        ConnectFirstGamepad();
    }
}

void Input::ConnectFirstGamepad() {
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
    if (joysticks && count > 0) {
        s_Gamepad = SDL_OpenGamepad(joysticks[0]);
        if (s_Gamepad) {
            ENGINE_CORE_INFO("Connected gamepad: {}", SDL_GetGamepadName(s_Gamepad));
        }
    }
    SDL_free(joysticks);
}

bool Input::IsKeyPressed(SDL_Scancode keycode) {
    if (!IsValidScancode(keycode)) {
        return false;
    }
    return s_State.CurrKeys[static_cast<size_t>(keycode)];
}

bool Input::IsKeyJustPressed(SDL_Scancode key) {
    if (!IsValidScancode(key)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(key);
    return !s_State.PrevKeys[idx] && s_State.CurrKeys[idx];
}

bool Input::IsKeyJustReleased(SDL_Scancode key) {
    if (!IsValidScancode(key)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(key);
    return s_State.PrevKeys[idx] && !s_State.CurrKeys[idx];
}

bool Input::IsMouseButtonPressed(int button) {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    return s_State.CurrMouseButtons[static_cast<size_t>(button)];
}

bool Input::IsMouseButtonJustPressed(int button) {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(button);
    return !s_State.PrevMouseButtons[idx] && s_State.CurrMouseButtons[idx];
}

bool Input::IsMouseButtonJustReleased(int button) {
    if (!IsValidMouseButton(button)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(button);
    return s_State.PrevMouseButtons[idx] && !s_State.CurrMouseButtons[idx];
}

glm::vec2 Input::GetMousePosition() {
    return s_State.MousePosition;
}

float Input::GetMouseScrollDeltaX() noexcept {
    return s_State.ScrollDeltaX;
}

float Input::GetMouseScrollDeltaY() noexcept {
    return s_State.ScrollDeltaY;
}

InputDeviceMode Input::GetActiveDeviceMode() noexcept {
    return s_State.ActiveDeviceMode;
}

bool Input::IsGamepadConnected() {
    return s_Gamepad != nullptr || s_State.GamepadConnectedForTesting;
}

bool Input::IsGamepadButtonPressed(SDL_GamepadButton button) {
    if (!IsGamepadConnected() || !IsValidGamepadButton(button)) {
        return false;
    }
    return s_State.CurrPadButtons[static_cast<size_t>(button)];
}

bool Input::IsGamepadButtonJustPressed(SDL_GamepadButton btn) {
    if (!IsGamepadConnected() || !IsValidGamepadButton(btn)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(btn);
    return !s_State.PrevPadButtons[idx] && s_State.CurrPadButtons[idx];
}

bool Input::IsGamepadButtonJustReleased(SDL_GamepadButton btn) {
    if (!IsGamepadConnected() || !IsValidGamepadButton(btn)) {
        return false;
    }
    const size_t idx = static_cast<size_t>(btn);
    return s_State.PrevPadButtons[idx] && !s_State.CurrPadButtons[idx];
}

float Input::GetGamepadAxis(SDL_GamepadAxis axis) {
    if (!IsGamepadConnected() || !IsValidGamepadAxis(axis)) {
        return 0.0f;
    }
    const float value = s_State.CurrPadAxes[static_cast<size_t>(axis)];
    if (std::abs(value) < kLegacySingleAxisDeadzone) {
        return 0.0f;
    }
    return value;
}

void Input::BindAction(std::string_view actionName, const ActionBinding& binding) {
    if (actionName.empty()) {
        return;
    }
    auto it = s_State.ActionBindings.find(actionName);
    if (it != s_State.ActionBindings.end()) {
        it->second = binding;
    } else {
        s_State.ActionBindings.emplace(std::string(actionName), binding);
    }
}

void Input::UnbindAction(std::string_view actionName) {
    if (auto it = s_State.ActionBindings.find(actionName); it != s_State.ActionBindings.end()) {
        s_State.ActionBindings.erase(it);
    }
}

void Input::ClearActions() {
    s_State.ActionBindings.clear();
}

const ActionBinding* Input::GetActionBinding(std::string_view actionName) {
    auto it = s_State.ActionBindings.find(actionName);
    return (it != s_State.ActionBindings.end()) ? &it->second : nullptr;
}

bool Input::IsActionPressed(std::string_view actionName) {
    const auto it = s_State.ActionBindings.find(actionName);
    if (it == s_State.ActionBindings.end()) {
        return false;
    }
    return EvaluateBinding(it->second, false);
}

bool Input::IsActionJustPressed(std::string_view actionName) {
    const auto it = s_State.ActionBindings.find(actionName);
    if (it == s_State.ActionBindings.end()) {
        return false;
    }
    return !EvaluateBinding(it->second, true) && EvaluateBinding(it->second, false);
}

bool Input::IsActionJustReleased(std::string_view actionName) {
    const auto it = s_State.ActionBindings.find(actionName);
    if (it == s_State.ActionBindings.end()) {
        return false;
    }
    return EvaluateBinding(it->second, true) && !EvaluateBinding(it->second, false);
}

glm::vec2 Input::ApplyRadialDeadzone(glm::vec2 rawStick, float deadzone) noexcept {
    if (!std::isfinite(rawStick.x) || !std::isfinite(rawStick.y) || !std::isfinite(deadzone)) {
        return glm::vec2(0.0f);
    }
    if (deadzone >= 1.0f) {
        return glm::vec2(0.0f);
    }

    const float d = std::max(0.0f, deadzone);
    const float m = std::sqrt(rawStick.x * rawStick.x + rawStick.y * rawStick.y);
    if (m < d || m <= 1e-6f) {
        return glm::vec2(0.0f);
    }

    const float remappedMagnitude = std::min(1.0f, (m - d) / (1.0f - d));
    return (rawStick / m) * remappedMagnitude;
}

glm::vec2 Input::GetMoveVector(float deadzone) {
    glm::vec2 keyMove(0.0f);
    if (IsKeyPressed(SDL_SCANCODE_D)) keyMove.x += 1.0f;
    if (IsKeyPressed(SDL_SCANCODE_A)) keyMove.x -= 1.0f;
    if (IsKeyPressed(SDL_SCANCODE_W)) keyMove.y += 1.0f;
    if (IsKeyPressed(SDL_SCANCODE_S)) keyMove.y -= 1.0f;

    const float keyLenSq = keyMove.x * keyMove.x + keyMove.y * keyMove.y;
    if (keyLenSq > 1.0f) {
        keyMove /= std::sqrt(keyLenSq);
    }

    glm::vec2 stickMove(0.0f);
    if (IsGamepadConnected()) {
        const float rawX = s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_LEFTX];
        const float rawY = -s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_LEFTY];
        stickMove = ApplyRadialDeadzone({ rawX, rawY }, deadzone);
    }

    const float stickLenSq = stickMove.x * stickMove.x + stickMove.y * stickMove.y;
    return (stickLenSq >= keyLenSq) ? stickMove : keyMove;
}

void Input::SetViewportRect(const glm::vec2& topLeftPx, const glm::vec2& sizePx) noexcept {
    s_State.ViewportTopLeft = topLeftPx;
    s_State.ViewportSize = { std::max(0.0f, sizePx.x), std::max(0.0f, sizePx.y) };
}

glm::vec2 Input::GetViewportTopLeft() noexcept {
    return s_State.ViewportTopLeft;
}

glm::vec2 Input::GetViewportSize() noexcept {
    return s_State.ViewportSize;
}

bool Input::ScreenToViewportNDC(const glm::vec2& screenPx, glm::vec2& outNDC) noexcept {
    if (s_State.ViewportSize.x <= 0.0f || s_State.ViewportSize.y <= 0.0f ||
        !std::isfinite(screenPx.x) || !std::isfinite(screenPx.y)) {
        return false;
    }

    const glm::vec2 rel = screenPx - s_State.ViewportTopLeft;
    outNDC.x = (rel.x / s_State.ViewportSize.x) * 2.0f - 1.0f;
    outNDC.y = 1.0f - (rel.y / s_State.ViewportSize.y) * 2.0f; // OpenGL bottom-up NDC [-1, 1]^2
    return true;
}

bool Input::GetMouseGroundHitXZ(const PerspectiveCamera& camera, float planeY, glm::vec2& outWorldXZ) {
    glm::vec2 ndc{ 0.0f };
    if (!ScreenToViewportNDC(s_State.MousePosition, ndc)) {
        return false;
    }

    glm::vec3 hitPoint{ 0.0f };
    if (!camera.RaycastGroundPlane(ndc, planeY, hitPoint)) {
        return false;
    }

    outWorldXZ = { hitPoint.x, hitPoint.z };
    return true;
}

glm::vec2 Input::ResolveAimDirectionXZ(const PerspectiveCamera& camera,
                                       const glm::vec2& characterWorldXZ,
                                       const glm::vec2& fallbackFacingXZ,
                                       float planeY) {
    const glm::vec2 safeFallback = NormalizeXZOrDefault(fallbackFacingXZ);

    if (s_State.ActiveDeviceMode == InputDeviceMode::Gamepad) {
        if (!IsGamepadConnected()) {
            return safeFallback;
        }

        const glm::vec2 rightStick = ApplyRadialDeadzone(
            { s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_RIGHTX], -s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_RIGHTY] },
            kAimStickDeadzone);
        if ((rightStick.x * rightStick.x + rightStick.y * rightStick.y) > 1e-6f) {
            return NormalizeXZOrDefault(ProjectStickToCameraXZ(camera, rightStick), safeFallback);
        }

        const glm::vec2 leftStick = ApplyRadialDeadzone(
            { s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_LEFTX], -s_State.CurrPadAxes[SDL_GAMEPAD_AXIS_LEFTY] },
            kAimStickDeadzone);
        if ((leftStick.x * leftStick.x + leftStick.y * leftStick.y) > 1e-6f) {
            return NormalizeXZOrDefault(ProjectStickToCameraXZ(camera, leftStick), safeFallback);
        }

        return safeFallback;
    }

    glm::vec2 hitXZ{ 0.0f };
    if (!GetMouseGroundHitXZ(camera, planeY, hitXZ) ||
        !std::isfinite(characterWorldXZ.x) || !std::isfinite(characterWorldXZ.y)) {
        return safeFallback;
    }

    return NormalizeXZOrDefault(hitXZ - characterWorldXZ, safeFallback);
}

void Input::TriggerGamepadRumble(float lowFreq01, float highFreq01, uint32_t durationMs) {
    if (!IsGamepadConnected()) {
        return;
    }

    const float clampedLow = std::isfinite(lowFreq01) ? std::clamp(lowFreq01, 0.0f, 1.0f) : 0.0f;
    const float clampedHigh = std::isfinite(highFreq01) ? std::clamp(highFreq01, 0.0f, 1.0f) : 0.0f;

    s_State.LastRumbleLow01 = clampedLow;
    s_State.LastRumbleHigh01 = clampedHigh;
    s_State.LastRumbleDurationMs = durationMs;
    ++s_State.RumbleCallCount;

    if (s_Gamepad) {
        const Uint16 lowU16 = static_cast<Uint16>(std::lround(clampedLow * 65535.0f));
        const Uint16 highU16 = static_cast<Uint16>(std::lround(clampedHigh * 65535.0f));
        if (!SDL_RumbleGamepad(s_Gamepad, lowU16, highU16, durationMs)) {
            ENGINE_CORE_TRACE("Input::TriggerGamepadRumble ignored or unsupported: {}", SDL_GetError());
        }
    }
}

std::string Input::SerializeBindingsToString() {
    nlohmann::json root;
    nlohmann::json bindingsJson = nlohmann::json::object();
    for (const auto& [name, binding] : s_State.ActionBindings) {
        bindingsJson[name] = SerializeBinding(binding);
    }
    root["bindings"] = std::move(bindingsJson);
    return root.dump(2);
}

bool Input::DeserializeBindingsFromString(std::string_view jsonStr) {
    if (jsonStr.empty()) {
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr.begin(), jsonStr.end());
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("Failed to parse input bindings JSON: {}", e.what());
        return false;
    }

    if (!root.is_object() || !root.contains("bindings") || !root.at("bindings").is_object()) {
        ENGINE_CORE_ERROR("Input bindings JSON missing 'bindings' object");
        return false;
    }

    std::unordered_map<std::string, ActionBinding, TransparentStringHash, std::equal_to<>> parsed;
    for (const auto& [actionName, entry] : root.at("bindings").items()) {
        if (!entry.is_object() || actionName.empty()) {
            continue;
        }
        ActionBinding binding;
        binding.PrimaryKey = ParseScancodeField(entry, "primaryKey", "primaryKeyName");
        binding.SecondaryKey = ParseScancodeField(entry, "secondaryKey", "secondaryKeyName");
        if (entry.contains("mouseButton") && entry.at("mouseButton").is_number_integer()) {
            const int btn = entry.at("mouseButton").get<int>();
            binding.MouseButton = IsValidMouseButton(btn) ? btn : 0;
        }
        binding.PadButton = ParseGamepadButtonField(entry);
        binding.PadTriggerAxis = ParseGamepadAxisField(entry);
        parsed.emplace(actionName, binding);
    }

    s_State.ActionBindings = std::move(parsed);
    return true;
}

bool Input::SaveBindingsToFile(const std::string& filepath) {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        ENGINE_CORE_ERROR("Failed to open input bindings file for writing: {}", filepath);
        return false;
    }
    const std::string data = SerializeBindingsToString();
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool Input::LoadBindingsFromFile(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        ENGINE_CORE_ERROR("Failed to open input bindings file for reading: {}", filepath);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return DeserializeBindingsFromString(ss.str());
}

void Input::SetKeyStateForTesting(SDL_Scancode key, bool down) {
    if (!IsValidScancode(key)) {
        return;
    }
    s_State.CurrKeys[static_cast<size_t>(key)] = down;
    if (down) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::SetMouseButtonStateForTesting(int button, bool down) {
    if (!IsValidMouseButton(button)) {
        return;
    }
    s_State.CurrMouseButtons[static_cast<size_t>(button)] = down;
    if (down) {
        s_State.ActiveDeviceMode = InputDeviceMode::KeyboardMouse;
    }
}

void Input::SetMousePositionForTesting(const glm::vec2& position) {
    s_State.MousePosition = position;
}

void Input::SetGamepadConnectedForTesting(bool connected) {
    s_State.GamepadConnectedForTesting = connected;
    if (!connected && !s_Gamepad) {
        s_State.CurrPadButtons.fill(false);
        s_State.CurrPadAxes.fill(0.0f);
    }
}

void Input::SetGamepadButtonStateForTesting(SDL_GamepadButton btn, bool down) {
    if (!IsValidGamepadButton(btn)) {
        return;
    }
    s_State.CurrPadButtons[static_cast<size_t>(btn)] = down;
    if (down) {
        s_State.ActiveDeviceMode = InputDeviceMode::Gamepad;
    }
}

void Input::SetGamepadAxisStateForTesting(SDL_GamepadAxis axis, float normalizedValue) {
    if (!IsValidGamepadAxis(axis)) {
        return;
    }
    const float clamped = std::clamp(normalizedValue, -1.0f, 1.0f);
    s_State.CurrPadAxes[static_cast<size_t>(axis)] = clamped;
    OnGamepadAxisEvent(axis, clamped);
}

void Input::SetActiveDeviceModeForTesting(InputDeviceMode mode) {
    s_State.ActiveDeviceMode = mode;
}

void Input::GetLastRumbleForTesting(float& outLowFreq01,
                                    float& outHighFreq01,
                                    uint32_t& outDurationMs,
                                    uint32_t& outCallCount) noexcept {
    outLowFreq01 = s_State.LastRumbleLow01;
    outHighFreq01 = s_State.LastRumbleHigh01;
    outDurationMs = s_State.LastRumbleDurationMs;
    outCallCount = s_State.RumbleCallCount;
}

} // namespace Engine