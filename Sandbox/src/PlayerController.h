#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <Engine/Animation/Animator.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/Input.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/FoliageWindComponents.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>
#include <glm/glm.hpp>

class PlayerController : public Engine::Component {
public:
    float moveSpeed = 3.8f;
    float jumpForce = 5.0f;
    Engine::ParticleProps m_DustProps;

    void Awake() override {
        ENGINE_INFO("PlayerController component initialized (2.5D Diorama mode)");
        m_DustProps.colorBegin = { 0.82f, 0.78f, 0.68f, 0.85f };
        m_DustProps.colorEnd = { 0.82f, 0.78f, 0.68f, 0.0f };
        m_DustProps.sizeBegin = 0.16f;
        m_DustProps.sizeEnd = 0.38f;
        m_DustProps.lifeTime = 0.45f;
        m_DustProps.velocity = { 0.0f, 0.55f, 0.0f };
        m_DustProps.velocityVariation = { 0.45f, 0.2f, 0.45f };
    }

    void Update(float deltaTime) override {
        auto* transform = GetGameObject()->GetTransform();
        auto* animator = GetGameObject()->GetComponent<Engine::Animator>();
        auto* audioSource = GetGameObject()->GetComponent<Engine::AudioSource>();
        auto* collider = GetGameObject()->GetComponent<Engine::BoxCollider2D>();
        if (!transform) return;

        // Resolve ground-plane camera basis so WASD always aligns with screen Up/Down/Left/Right
        glm::vec3 forwardXZ = { 0.0f, 0.0f, -1.0f };
        glm::vec3 rightXZ   = { 1.0f, 0.0f,  0.0f };
        if (auto activeScene = Engine::SceneManager::GetActiveScene()) {
            const auto& cam = activeScene->GetActiveCamera();
            forwardXZ = cam.GetForwardXZ();
            rightXZ   = cam.GetRightXZ();
        }

        float inputForward = 0.0f;
        float inputRight   = 0.0f;

        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_W)) inputForward += 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_S)) inputForward -= 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_D)) inputRight   += 1.0f;
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_A)) inputRight   -= 1.0f;

        if (Engine::Input::IsGamepadConnected()) {
            const float axisX = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
            const float axisY = -Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY);
            if (axisX != 0.0f || axisY != 0.0f) {
                inputRight   = axisX;
                inputForward = axisY;
            }
        }

        glm::vec3 movement = forwardXZ * inputForward + rightXZ * inputRight;

        // Jump impulse on the Y axis when grounded (gravity brings player back to Y = 0)
        if (Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE)) {
            if (collider && collider->isDynamic && collider->useGravity) {
                if (collider->IsGrounded(transform->position.y)) {
                    collider->verticalVelocity = jumpForce;
                    if (auto* ss = GetGameObject()->GetComponent<Engine::SquashStretchComponent>()) {
                        ss->TriggerImpulse(ss->JumpLaunchStretch);
                    }
                    if (audioSource) {
                        audioSource->Play();
                    }
                }
            } else {
                if (transform->position.y <= 1e-4f && m_FallbackVerticalVelocity <= 0.0f) {
                    m_FallbackVerticalVelocity = jumpForce;
                    if (auto* ss = GetGameObject()->GetComponent<Engine::SquashStretchComponent>()) {
                        ss->TriggerImpulse(ss->JumpLaunchStretch);
                    }
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

        const bool isMoving = (movement.x * movement.x + movement.z * movement.z) > 1e-5f;

        if (isMoving) {
            movement = glm::normalize(movement);
            ENGINE_DEBUG("PlayerController moving: ({:.2f}, {:.2f}, {:.2f})", movement.x, movement.y, movement.z);
            transform->Translate(movement * moveSpeed * deltaTime);

            if (animator) animator->Play("Walk");

            if (auto* particleSystem = GetGameObject()->GetComponent<Engine::ParticleSystem>()) {
                m_DustProps.position = transform->position;
                m_DustProps.position.y += 0.08f; // Spawn slightly above Y = 0 ground plane
                particleSystem->Emit(m_DustProps);
            }
        } else {
            if (animator) animator->Play("Idle");
        }
    }

private:
    float m_FallbackVerticalVelocity = 0.0f;
};

#endif // PLAYER_CONTROLLER_H
