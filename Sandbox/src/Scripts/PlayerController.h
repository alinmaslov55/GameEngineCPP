#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <Engine/Animation/Animator.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/Input.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Physics/PhysicsQueries.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/CameraComponent.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

class PlayerController final : public Engine::Component {
public:
    float moveSpeed = 4.2f;
    float jumpForce = 7.0f;

    // Dodge Roll parameters
    float dodgeSpeed = 11.5f;
    float dodgeDuration = 0.22f;
    float dodgeCooldown = 0.65f;
    float dodgeHopVelocity = 4.2f;
    float dodgeLinearDamping = 1.5f;

    // Socket layout offsets
    float torchSocketDistance = 0.55f;
    float torchSocketHeight = 0.85f;
    float weaponSocketDistance = 0.65f;
    float weaponSocketHeight = 0.55f;

    // Dynamic zoom pack-density range
    float packZoomRadius = 10.0f;
    float packZoomMaxEnemies = 12.0f;

    Engine::ParticleProps m_DustProps;

    void Awake() override {
        ENGINE_INFO("PlayerController initialized (GAM-01 Brawler Mode)");
        Engine::Input::BindAction("Dodge", {
            .PrimaryKey = SDL_SCANCODE_SPACE,
            .SecondaryKey = SDL_SCANCODE_LSHIFT,
            .MouseButton = 0,
            .PadButton = SDL_GAMEPAD_BUTTON_SOUTH,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID
        });

        if (auto* go = GetGameObject()) {
            if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
                rb->UseTerrainFriction = true;
            }
        }

        m_DustProps.colorBegin = { 0.82f, 0.78f, 0.68f, 0.85f };
        m_DustProps.colorEnd = { 0.82f, 0.78f, 0.68f, 0.0f };
        m_DustProps.sizeBegin = 0.16f;
        m_DustProps.sizeEnd = 0.38f;
        m_DustProps.lifeTime = 0.45f;
        m_DustProps.velocity = { 0.0f, 0.55f, 0.0f };
        m_DustProps.velocityVariation = { 0.45f, 0.2f, 0.45f };
    }

    [[nodiscard]] glm::vec3 GetMovementInput() const {
        const glm::vec2 moveInput = Engine::Input::GetMoveVector(0.20f);
        const float inputLenSq = moveInput.x * moveInput.x + moveInput.y * moveInput.y;
        if (inputLenSq <= 1e-5f) {
            return glm::vec3(0.0f);
        }

        glm::vec3 forwardXZ = { 0.0f, 0.0f, -1.0f };
        glm::vec3 rightXZ   = { 1.0f, 0.0f,  0.0f };
        if (auto activeScene = Engine::SceneManager::GetActiveScene()) {
            const auto& cam = activeScene->GetActiveCamera();
            forwardXZ = cam.GetForwardXZ();
            rightXZ   = cam.GetRightXZ();
        } else if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene()) {
                const auto& cam = scene->GetActiveCamera();
                forwardXZ = cam.GetForwardXZ();
                rightXZ   = cam.GetRightXZ();
            }
        }

        const glm::vec3 movement = forwardXZ * moveInput.y + rightXZ * moveInput.x;
        const float dirLenSq = movement.x * movement.x + movement.z * movement.z;
        if (dirLenSq <= 1e-5f) {
            return glm::vec3(0.0f);
        }

        const float analogMagnitude = std::min(1.0f, std::sqrt(inputLenSq));
        return (movement / std::sqrt(dirLenSq)) * analogMagnitude;
    }

    bool TriggerDodgeRoll(const glm::vec2& directionXZ) {
        if (m_DodgeTimer > 0.0f || m_DodgeCooldownTimer > 0.0f) {
            return false;
        }

        glm::vec2 dir = directionXZ;
        const float len = glm::length(dir);
        if (!std::isfinite(len) || len <= 1e-4f) {
            dir = m_LastFacingXZ;
        } else {
            dir /= len;
        }

        m_DodgeDirXZ = dir;
        m_LastFacingXZ = dir;
        m_DodgeTimer = dodgeDuration;
        m_DodgeCooldownTimer = dodgeCooldown;
        m_InvulnTimer = dodgeDuration;

        auto* go = GetGameObject();
        auto* transform = go ? go->GetTransform() : nullptr;
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        if (rb && rb->Type == Engine::Rigidbody2D::BodyType::Dynamic) {
            rb->SetLinearDampingRuntime(dodgeLinearDamping);
            rb->SetVelocityXZ(m_DodgeDirXZ * dodgeSpeed);
            if (rb->UseGravityY && (!transform || rb->IsGrounded(transform->position.y))) {
                rb->LaunchVertical(dodgeHopVelocity);
            }
        } else if (transform && transform->position.y <= 1e-4f && m_FallbackVerticalVelocity <= 0.0f) {
            m_FallbackVerticalVelocity = dodgeHopVelocity;
        }

        if (go) {
            if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                ss->TriggerImpulse(ss->JumpLaunchStretch * 0.85f, (m_DodgeDirXZ.x >= 0.0f ? -10.0f : 10.0f));
            }
            if (auto* ps = go->GetComponent<Engine::ParticleSystem>()) {
                if (transform) {
                    Engine::ParticleProps burstProps = m_DustProps;
                    burstProps.position = transform->GetWorldPosition();
                    burstProps.position.y += 0.08f;
                    ps->EmitBurst(burstProps, 6u);
                }
            }
            if (auto* audio = go->GetComponent<Engine::AudioSource>()) {
                audio->Play();
            }
            if (auto* cam = go->GetComponent<Engine::CameraComponent>()) {
                cam->AddTrauma(0.12f, -m_DodgeDirXZ * 0.15f);
            }
        }
        return true;
    }

    void TriggerImpactFeedback(float trauma01, const glm::vec2& directionalKickXZ = glm::vec2(0.0f)) {
        if (auto* go = GetGameObject()) {
            if (auto* cam = go->GetComponent<Engine::CameraComponent>()) {
                cam->AddTrauma(trauma01, directionalKickXZ);
            }
        }
    }

    [[nodiscard]] bool IsDodging() const noexcept { return m_DodgeTimer > 0.0f; }
    [[nodiscard]] bool IsInvulnerable() const noexcept { return m_InvulnTimer > 0.0f; }
    [[nodiscard]] float GetDodgeCooldownRemaining() const noexcept { return std::max(0.0f, m_DodgeCooldownTimer); }
    [[nodiscard]] const glm::vec2& GetAimDirectionXZ() const noexcept { return m_AimDirectionXZ; }
    [[nodiscard]] const glm::vec2& GetLastFacingXZ() const noexcept { return m_LastFacingXZ; }
    [[nodiscard]] float GetEffectiveMoveSpeed() const noexcept { return m_EffectiveMoveSpeed; }

    void FixedUpdate(float fixedDt) override {
        auto* go = GetGameObject();
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        auto* transform = go ? go->GetTransform() : nullptr;
        if (!rb || rb->Type != Engine::Rigidbody2D::BodyType::Dynamic || !rb->RuntimeBody.IsValid()) {
            return;
        }

        // Sample underlying terrain surface for footwork speed & damping modulation
        float speedMultiplier = 1.0f;
        float surfaceDamping = rb->LinearDamping;
        if (auto* scene = go->GetScene(); scene && transform) {
            const glm::vec3 worldPos = transform->GetWorldPosition();
            const auto sample = scene->SampleSurfaceAtXZ(worldPos.x, worldPos.z);
            if (std::isfinite(sample.MaxSpeedMultiplier) && sample.MaxSpeedMultiplier > 0.0f) {
                speedMultiplier = sample.MaxSpeedMultiplier;
            }
            if (std::isfinite(sample.LinearDamping) && sample.LinearDamping >= 0.0f) {
                surfaceDamping = sample.LinearDamping;
            }
        }
        m_EffectiveMoveSpeed = moveSpeed * speedMultiplier;

        if (m_DodgeTimer > 0.0f) {
            rb->SetLinearDampingRuntime(dodgeLinearDamping);
            rb->SetVelocityXZ(m_DodgeDirXZ * dodgeSpeed);
            return;
        }

        if (transform && rb->IsGrounded(transform->position.y)) {
            rb->SetLinearDampingRuntime(surfaceDamping);
        }

        const glm::vec3 movement = GetMovementInput();
        const bool isMoving = (movement.x * movement.x + movement.z * movement.z) > 1e-5f;

        const glm::vec2 curVel = rb->GetVelocityXZ();
        const glm::vec2 targetVel = isMoving
            ? glm::vec2(movement.x * m_EffectiveMoveSpeed, movement.z * m_EffectiveMoveSpeed)
            : glm::vec2(0.0f, 0.0f);

        // Bounded acceleration preserves Box2D knockback impulses while giving crisp idle braking
        const float maxAccel = isMoving ? 28.0f : 35.0f;
        const float maxDeltaV = maxAccel * std::max(0.0f, fixedDt);
        const glm::vec2 dv = targetVel - curVel;
        const float dvLen = glm::length(dv);
        if (dvLen > 1e-5f) {
            const glm::vec2 newVel = (dvLen <= maxDeltaV) ? targetVel : (curVel + (dv / dvLen) * maxDeltaV);
            rb->SetVelocityXZ(newVel.x, newVel.y);
        }
    }

    void Update(float deltaTime) override {
        auto* go = GetGameObject();
        auto* transform = go ? go->GetTransform() : nullptr;
        if (!transform || !std::isfinite(deltaTime) || deltaTime <= 0.0f) return;

        auto* animator = go->GetComponent<Engine::Animator>();
        auto* rb = go->GetComponent<Engine::Rigidbody2D>();

        if (m_DodgeTimer > 0.0f) {
            m_DodgeTimer = std::max(0.0f, m_DodgeTimer - deltaTime);
        }
        if (m_DodgeCooldownTimer > 0.0f) {
            m_DodgeCooldownTimer = std::max(0.0f, m_DodgeCooldownTimer - deltaTime);
        }
        if (m_InvulnTimer > 0.0f) {
            m_InvulnTimer = std::max(0.0f, m_InvulnTimer - deltaTime);
        }

        const glm::vec3 movement = GetMovementInput();
        const bool isMoving = (movement.x * movement.x + movement.z * movement.z) > 1e-5f;
        if (isMoving) {
            m_LastFacingXZ = glm::normalize(glm::vec2(movement.x, movement.z));
        }

        // Dodge Roll (Space / Left Shift / Gamepad A)
        if (Engine::Input::IsActionJustPressed("Dodge")) {
            const glm::vec2 dashDir = isMoving
                ? glm::normalize(glm::vec2(movement.x, movement.z))
                : m_AimDirectionXZ;
            TriggerDodgeRoll(dashDir);
        }

        // Fallback vertical integration if no dynamic Rigidbody2D handles vertical physics
        if (!rb || rb->Type != Engine::Rigidbody2D::BodyType::Dynamic || !rb->UseGravityY) {
            if (transform->position.y > 0.0f || m_FallbackVerticalVelocity != 0.0f) {
                m_FallbackVerticalVelocity += -22.0f * deltaTime;
                transform->position.y += m_FallbackVerticalVelocity * deltaTime;
                if (transform->position.y <= 0.0f) {
                    transform->position.y = 0.0f;
                    m_FallbackVerticalVelocity = 0.0f;
                }
            }
        }

        // 360-degree Aim Resolution, Sprite FlipX & Child Socket Orientation
        UpdateAimAndSockets(transform);

        // CameraComponent look-ahead & pack-density dynamic zoom
        if (auto* camComp = go->GetComponent<Engine::CameraComponent>()) {
            camComp->SetAimDirectionXZ(m_AimDirectionXZ);
            if (auto* scene = go->GetScene()) {
                m_NearbyEnemiesScratch.clear();
                const glm::vec3 wp = transform->GetWorldPosition();
                Engine::PhysicsQueries::OverlapCircleXZ(
                    *scene,
                    { wp.x, wp.z },
                    packZoomRadius,
                    Engine::PhysicsLayer::Enemy,
                    m_NearbyEnemiesScratch
                );
                const float zoom01 = std::clamp(
                    static_cast<float>(m_NearbyEnemiesScratch.size()) / std::max(1.0f, packZoomMaxEnemies),
                    0.0f, 1.0f);
                camComp->SetDynamicZoomFactor(zoom01);
            }
        }

        const bool hasDynamicBody = (rb && rb->Type == Engine::Rigidbody2D::BodyType::Dynamic && rb->RuntimeBody.IsValid());
        const bool isGrounded = hasDynamicBody ? rb->IsGrounded(transform->position.y) : (transform->position.y <= 1e-4f);

        if (m_DodgeTimer > 0.0f) {
            if (!hasDynamicBody) {
                transform->Translate(glm::vec3(m_DodgeDirXZ.x, 0.0f, m_DodgeDirXZ.y) * dodgeSpeed * deltaTime);
            }
            if (animator) animator->PlayState("Walk", m_AimDirectionXZ);
        } else if (isMoving) {
            if (!hasDynamicBody) {
                transform->Translate(movement * m_EffectiveMoveSpeed * deltaTime);
            }
            if (animator) animator->PlayState("Walk", m_AimDirectionXZ);

            m_DustTimer += deltaTime;
            if (isGrounded && m_DustTimer >= 0.08f) {
                m_DustTimer = 0.0f;
                if (auto* particleSystem = go->GetComponent<Engine::ParticleSystem>()) {
                    m_DustProps.position = transform->GetWorldPosition();
                    m_DustProps.position.y += 0.08f;
                    particleSystem->Emit(m_DustProps);
                }
            }
        } else {
            m_DustTimer = 0.0f;
            if (animator) animator->PlayState("Idle", m_AimDirectionXZ);
        }
    }

private:
    Engine::Transform* FindChildByName(Engine::Transform* parent, std::string_view name) const noexcept {
        if (!parent) return nullptr;
        for (Engine::Transform* child : parent->GetChildren()) {
            if (child && child->GetGameObject() && child->GetGameObject()->GetName() == name) {
                return child;
            }
        }
        return nullptr;
    }

    void UpdateAimAndSockets(Engine::Transform* transform) {
        auto* go = GetGameObject();
        const glm::vec3 worldPos = transform->GetWorldPosition();
        const glm::vec2 posXZ{ worldPos.x, worldPos.z };

        if (auto* scene = go ? go->GetScene() : nullptr) {
            m_AimDirectionXZ = Engine::Input::ResolveAimDirectionXZ(
                scene->GetActiveCamera(), posXZ, m_LastFacingXZ);
        } else if (auto activeScene = Engine::SceneManager::GetActiveScene()) {
            m_AimDirectionXZ = Engine::Input::ResolveAimDirectionXZ(
                activeScene->GetActiveCamera(), posXZ, m_LastFacingXZ);
        } else {
            m_AimDirectionXZ = m_LastFacingXZ;
        }

        const float aimLen = glm::length(m_AimDirectionXZ);
        if (std::isfinite(aimLen) && aimLen > 1e-4f) {
            m_AimDirectionXZ /= aimLen;
        } else {
            m_AimDirectionXZ = m_LastFacingXZ;
        }

        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            if (m_AimDirectionXZ.x < -0.05f) {
                sr->SetFlipX(true);
            } else if (m_AimDirectionXZ.x > 0.05f) {
                sr->SetFlipX(false);
            }
        }

        const float aimYawDeg = glm::degrees(std::atan2(m_AimDirectionXZ.x, m_AimDirectionXZ.y));
        if (auto* torchSocket = FindChildByName(transform, "TorchSocket")) {
            torchSocket->position = {
                m_AimDirectionXZ.x * torchSocketDistance,
                torchSocketHeight,
                m_AimDirectionXZ.y * torchSocketDistance
            };
            torchSocket->rotation.y = aimYawDeg;
            torchSocket->SetDirty();
        }
        if (auto* weaponSocket = FindChildByName(transform, "WeaponSocket")) {
            weaponSocket->position = {
                m_AimDirectionXZ.x * weaponSocketDistance,
                weaponSocketHeight,
                m_AimDirectionXZ.y * weaponSocketDistance
            };
            weaponSocket->rotation.y = aimYawDeg;
            weaponSocket->SetDirty();
        }
    }

    float m_FallbackVerticalVelocity = 0.0f;
    float m_DustTimer = 0.0f;
    float m_DodgeTimer = 0.0f;
    float m_DodgeCooldownTimer = 0.0f;
    float m_InvulnTimer = 0.0f;
    float m_EffectiveMoveSpeed = 4.2f;
    glm::vec2 m_LastFacingXZ{ 0.0f, 1.0f };
    glm::vec2 m_AimDirectionXZ{ 0.0f, 1.0f };
    glm::vec2 m_DodgeDirXZ{ 0.0f, 1.0f };
    std::vector<Engine::GameObject*> m_NearbyEnemiesScratch;
};

#endif // PLAYER_CONTROLLER_H
