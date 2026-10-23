#pragma once
#include <Engine/Animation/Animator.h>
#include <Engine/Core/Input.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

class PlayerController : public Engine::Component {
public:
    float moveSpeed = 2.0f;

    void Awake() override {
        ENGINE_INFO("PlayerController component initialized");
    }

    void Update(float deltaTime) override {
        auto transform = GetGameObject()->GetComponent<Engine::Transform>();
        auto animator = GetGameObject()->GetComponent<Engine::Animator>();
        if (!transform) return;

        glm::vec3 movement(0.0f);

        // Keyboard Input (Isometric directions mapped to Screen Up/Down/Left/Right)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_W)) { movement.x += 1.0f; movement.z += 1.0f; } // Move Up (North)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_S)) { movement.x -= 1.0f; movement.z -= 1.0f; } // Move Down (South)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_A)) { movement.x -= 1.0f; movement.z += 1.0f; } // Move Left (West)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_D)) { movement.x += 1.0f; movement.z -= 1.0f; } // Move Right (East)

        // Add jumping on the Y axis
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE)) movement.y += 1.0f;

        // Gamepad Input (Left Stick)
        if (Engine::Input::IsGamepadConnected()) {
            float axisX = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
            float axisY = -Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY); // Invert so stick up is +1
            
            if (axisX != 0.0f || axisY != 0.0f) {
                movement.x = axisY + axisX;
                movement.z = axisY - axisX;
            }
        }

        bool isMoving = glm::length(movement) > 0.0f;

        // Apply movement scaled by time and speed
        if (isMoving) {
            // Normalize so diagonal movement isn't faster
            movement = glm::normalize(movement);
            ENGINE_DEBUG("PlayerController moving: ({:.2f}, {:.2f}, {:.2f})", movement.x, movement.y, movement.z);
            transform->Translate(movement * moveSpeed * deltaTime);

            if (animator) animator->Play("Walk");
        } else {
            if (animator) animator->Play("Idle");
        }
    }
};