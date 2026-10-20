#pragma once
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Core/Input.h>

class PlayerController : public Engine::Component {
public:
    float moveSpeed = 2.0f;

    void Update(float deltaTime) override {
        auto transform = GetGameObject()->GetComponent<Engine::Transform>();
        if (!transform) return;

        glm::vec3 movement(0.0f);

        // Keyboard Input
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_W)) movement.y += 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_S)) movement.y -= 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_A)) movement.x -= 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_D)) movement.x += 1.0f;

        // Gamepad Input (Left Stick)
        if (Engine::Input::IsGamepadConnected()) {
            float axisX = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
            float axisY = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY);
            
            // Invert Y axis because OpenGL Y goes up, but gamepad Y goes down
            if (axisX != 0.0f) movement.x = axisX;
            if (axisY != 0.0f) movement.y = -axisY; 
        }

        // Apply movement scaled by time and speed
        if (glm::length(movement) > 0.0f) {
            // Normalize so diagonal movement isn't faster
            movement = glm::normalize(movement);
            transform->Translate(movement * moveSpeed * deltaTime);
        }
    }
};