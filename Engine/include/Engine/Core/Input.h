#ifndef CORE_INPUT_H
#define CORE_INPUT_H

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine {

enum class InputDeviceMode : uint8_t {
    KeyboardMouse = 0,
    Gamepad = 1
};

struct ActionBinding {
    SDL_Scancode PrimaryKey = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode SecondaryKey = SDL_SCANCODE_UNKNOWN;
    int MouseButton = 0; // 0 = none, SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, etc.
    SDL_GamepadButton PadButton = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadAxis PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID; // > 0.5f threshold for LT/RT

    bool operator==(const ActionBinding&) const = default;
};

class Input {
public:
    static void Init();
    static void Shutdown();
    static void BeginFrame(); // Copies current state -> prev state before SDL_PollEvent
    static void Update();     // Refreshes current hardware snapshots idempotently from SDL

    // Keyboard & Mouse
    static bool IsKeyPressed(SDL_Scancode keycode);
    static bool IsKeyJustPressed(SDL_Scancode key);
    static bool IsKeyJustReleased(SDL_Scancode key);

    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonJustPressed(int button);
    static bool IsMouseButtonJustReleased(int button);
    static glm::vec2 GetMousePosition();
    static float GetMouseScrollDeltaX() noexcept;
    static float GetMouseScrollDeltaY() noexcept;
    static InputDeviceMode GetActiveDeviceMode() noexcept;

    // Gamepad (one player)
    static bool IsGamepadConnected();
    static bool IsGamepadButtonPressed(SDL_GamepadButton button);
    static bool IsGamepadButtonJustPressed(SDL_GamepadButton btn);
    static bool IsGamepadButtonJustReleased(SDL_GamepadButton btn);
    static float GetGamepadAxis(SDL_GamepadAxis axis); // Returns -1.0f to 1.0f

    // Action Bindings & Radial Deadzone Locomotion
    static void BindAction(std::string_view actionName, const ActionBinding& binding);
    static void UnbindAction(std::string_view actionName);
    static void ClearActions();
    static const ActionBinding* GetActionBinding(std::string_view actionName);

    static bool IsActionPressed(std::string_view actionName);
    static bool IsActionJustPressed(std::string_view actionName);
    static bool IsActionJustReleased(std::string_view actionName);

    static glm::vec2 ApplyRadialDeadzone(glm::vec2 rawStick, float deadzone = 0.20f) noexcept;
    static glm::vec2 GetMoveVector(float deadzone = 0.20f);

    // JSON Serialization of Action Bindings
    static std::string SerializeBindingsToString();
    static bool DeserializeBindingsFromString(std::string_view jsonStr);
    static bool SaveBindingsToFile(const std::string& filepath);
    static bool LoadBindingsFromFile(const std::string& filepath);

    // SDL Event-driven Input & Gamepad Notification Callbacks
    static void OnMouseWheel(float wheelX, float wheelY);
    static void OnKeyEvent(bool pressed);
    static void OnMouseButtonEvent(bool pressed);
    static void OnMouseMotionEvent(float relX, float relY);
    static void OnGamepadButtonEvent(bool pressed);
    static void OnGamepadAxisEvent(SDL_GamepadAxis axis, float normalizedValue);
    static void OnGamepadAdded(SDL_JoystickID which);
    static void OnGamepadRemoved(SDL_JoystickID which);

    // Deterministic Headless Testing Injection Helpers
    static void SetKeyStateForTesting(SDL_Scancode key, bool down);
    static void SetMouseButtonStateForTesting(int button, bool down);
    static void SetGamepadConnectedForTesting(bool connected);
    static void SetGamepadButtonStateForTesting(SDL_GamepadButton btn, bool down);
    static void SetGamepadAxisStateForTesting(SDL_GamepadAxis axis, float normalizedValue);
    static void SetActiveDeviceModeForTesting(InputDeviceMode mode);

private:
    static void ConnectFirstGamepad();
    static SDL_Gamepad* s_Gamepad;
};
} // namespace Engine

#endif // CORE_INPUT_H
