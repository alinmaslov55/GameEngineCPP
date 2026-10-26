#pragma once
#include <Engine/Animation/Animator.h>
#include <Engine/Core/Input.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Scene/BoxCollider2D.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

class PlayerController : public Engine::Component {
public:
    float moveSpeed = 2.5f;
    float jumpForce = 4.5f;
    Engine::ParticleProps m_DustProps;

    void Awake() override {
        ENGINE_INFO("PlayerController component initialized");
        m_DustProps.colorBegin = { 0.8f, 0.8f, 0.7f, 1.0f }; // Dust color
        m_DustProps.colorEnd = { 0.8f, 0.8f, 0.7f, 0.0f };   // Fade to transparent
        m_DustProps.sizeBegin = 0.2f;
        m_DustProps.sizeEnd = 0.5f; // Expands as it dissipates
        m_DustProps.lifeTime = 0.5f;
        m_DustProps.velocity = { 0.0f, 0.5f, 0.0f }; // Drifts slightly up (on Y axis)
        m_DustProps.velocityVariation = { 0.5f, 0.2f, 0.5f };
    }

    void Update(float deltaTime) override {
        auto* transform = GetGameObject()->GetTransform();
        auto* animator = GetGameObject()->GetComponent<Engine::Animator>();
        auto* audioSource = GetGameObject()->GetComponent<Engine::AudioSource>();
        auto* collider = GetGameObject()->GetComponent<Engine::BoxCollider2D>();
        if (!transform) return;

        glm::vec3 movement(0.0f);

        // Keyboard Input (Isometric directions mapped to Screen Up/Down/Left/Right on XZ plane)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_W)) { movement.x += 1.0f; movement.z += 1.0f; } // Move Up (North)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_S)) { movement.x -= 1.0f; movement.z -= 1.0f; } // Move Down (South)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_A)) { movement.x -= 1.0f; movement.z += 1.0f; } // Move Left (West)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_D)) { movement.x += 1.0f; movement.z -= 1.0f; } // Move Right (East)

        // Gamepad Input (Left Stick)
        if (Engine::Input::IsGamepadConnected()) {
            float axisX = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
            float axisY = -Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY); // Invert so stick up is +1

            if (axisX != 0.0f || axisY != 0.0f) {
                movement.x = axisY + axisX;
                movement.z = axisY - axisX;
            }
        }

        // Jump impulse on the Y axis when grounded (gravity brings player back to Y = 0)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE)) {
            if (collider && collider->isDynamic && collider->useGravity) {
                if (collider->IsGrounded(transform->position.y)) {
                    collider->verticalVelocity = jumpForce;
                    if (audioSource) {
                        audioSource->Play();
                    }
                }
            } else {
                // Fallback self-contained vertical jump if no dynamic BoxCollider2D is attached
                if (transform->position.y <= 1e-4f && m_FallbackVerticalVelocity <= 0.0f) {
                    m_FallbackVerticalVelocity = jumpForce;
                    if (audioSource) {
                        audioSource->Play();
                    }
                }
            }
        }

        // Apply fallback gravity if no dynamic BoxCollider2D handles vertical physics
        if (!collider || !collider->isDynamic || !collider->useGravity) {
            if (transform->position.y > 0.0f || m_FallbackVerticalVelocity != 0.0f) {
                m_FallbackVerticalVelocity += -9.8f * deltaTime;
                transform->position.y += m_FallbackVerticalVelocity * deltaTime;
                if (transform->position.y <= 0.0f) {
                    transform->position.y = 0.0f;
                    m_FallbackVerticalVelocity = 0.0f;
                }
            }
        }

        bool isMoving = (movement.x * movement.x + movement.z * movement.z) > 0.0f;

        // Apply horizontal movement scaled by time and speed
        if (isMoving) {
            movement = glm::normalize(movement);
            ENGINE_DEBUG("PlayerController moving: ({:.2f}, {:.2f}, {:.2f})", movement.x, movement.y, movement.z);
            transform->Translate(movement * moveSpeed * deltaTime);

            if (animator) animator->Play("Walk");

            auto* particleSystem = GetGameObject()->GetComponent<Engine::ParticleSystem>();
            if (particleSystem) {
                m_DustProps.position = transform->position;
                m_DustProps.position.y -= 0.3f;

                particleSystem->Emit(m_DustProps);
                particleSystem->Emit(m_DustProps);
            }
        } else {
            if (animator) animator->Play("Idle");
        }
    }

private:
    float m_FallbackVerticalVelocity = 0.0f;
};
