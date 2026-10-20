#include "Engine/Core/Input.h"
#include <iostream>

namespace Engine {
SDL_Gamepad* Input::s_Gamepad{nullptr};

void Input::Init() {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to initialize Gamepad subsystem: " << SDL_GetError() << '\n';
    }
    ConnectFirstGamepad();
}

void Input::Shutdown() {
    if (s_Gamepad) {
        SDL_CloseGamepad(s_Gamepad);
        s_Gamepad = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void Input::Update() {
    if (!s_Gamepad) {
        ConnectFirstGamepad();
    }
}

void Input::ConnectFirstGamepad() {
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
    if (joysticks && count > 0) {
        s_Gamepad = SDL_OpenGamepad(joysticks[0]);
    }
    SDL_free(joysticks);
}

bool Input::IsKeyPressed(SDL_Scancode keycode) {
    int numKeys;
    const bool* state = SDL_GetKeyboardState(&numKeys);
    if (state && keycode < numKeys) {
        return state[keycode];
    }
    return false;
}

bool Input::IsMouseButtonPressed(int button) {
    float x, y;
    SDL_MouseButtonFlags state = SDL_GetMouseState(&x, &y);
    return (state & SDL_BUTTON_MASK(button)) != 0;
}

glm::vec2 Input::GetMousePosition() {
    float x, y;
    SDL_GetMouseState(&x, &y);
    return { x, y };
}

bool Input::IsGamepadConnected() {
    return s_Gamepad != nullptr;
}

bool Input::IsGamepadButtonPressed(SDL_GamepadButton button) {
    if (!s_Gamepad) return false;
    return SDL_GetGamepadButton(s_Gamepad, button);
}

float Input::GetGamepadAxis(SDL_GamepadAxis axis) {
    if (!s_Gamepad) return 0.0f;
    // SDL returns axis values from -32768 to 32767. Normalize to -1.0f to 1.0f.
    int16_t value = SDL_GetGamepadAxis(s_Gamepad, axis);
    
    if (value > -8000 && value < 8000) return 0.0f;
    
    return static_cast<float>(value) / 32767.0f;
}
} // namespace Engine