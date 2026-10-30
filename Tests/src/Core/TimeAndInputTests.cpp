#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Core/Input.h"

namespace {

TEST(TimeTest, InitializationAndUpdate) {
    Engine::Time::Init();
    EXPECT_GE(Engine::Time::GetDeltaTime(), 0.0f);
    EXPECT_GE(Engine::Time::GetTotalTime(), 0.0f);

    Engine::Time::Update();
    EXPECT_GE(Engine::Time::GetDeltaTime(), 0.0f);
    EXPECT_GE(Engine::Time::GetTotalTime(), 0.0f);
}

TEST(InputTest, DefaultStateQueries) {
    Engine::Input::Init();
    Engine::Input::Update();

    // Query keyboard, mouse, and gamepad without crashing
    bool keyState = Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE);
    (void)keyState;

    bool mouseState = Engine::Input::IsMouseButtonPressed(1);
    (void)mouseState;

    glm::vec2 mousePos = Engine::Input::GetMousePosition();
    (void)mousePos;

    bool gamepadConnected = Engine::Input::IsGamepadConnected();
    (void)gamepadConnected;

    bool btnState = Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH);
    (void)btnState;

    float axis = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
    EXPECT_GE(axis, -1.0f);
    EXPECT_LE(axis, 1.0f);

    Engine::Input::Shutdown();
    SUCCEED();
}

} // namespace

