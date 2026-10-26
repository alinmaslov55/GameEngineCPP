#ifndef CORE_INPUT_H
#define CORE_INPUT_H

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

namespace Engine {
class Input {
public:
    static void Init();
    static void Shutdown();
    static void Update();

    // Keyboard & Mouse
    static bool IsKeyPressed(SDL_Scancode keycode);
    static bool IsMouseButtonPressed(int button);
    static glm::vec2 GetMousePosition();

    // Gamepad (one player)
    static bool IsGamepadConnected();
    static bool IsGamepadButtonPressed(SDL_GamepadButton button);
    static float GetGamepadAxis(SDL_GamepadAxis axis); // Returns -1.0f to 1.0f

private:
    static void ConnectFirstGamepad();
    static SDL_Gamepad* s_Gamepad;
};
} // namespace Engine

#endif // CORE_INPUT_H
