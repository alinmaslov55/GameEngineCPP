#ifndef PHYSICS_PHYSICSCOMPONENTS_H
#define PHYSICS_PHYSICSCOMPONENTS_H

#include "Engine/Scene/Component.h"
#include <glm/glm.hpp>

namespace Engine {

class Rigidbody2D : public Component {
public:
    enum class BodyType {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

    BodyType Type = BodyType::Dynamic;
    bool FixedRotation = false;

    // Pointer or handle to internal Box2D body (only valid during runtime / play mode)
    void* RuntimeBody = nullptr;

    // Initial velocity on the XZ ground plane applied when entering runtime play mode
    glm::vec2 InitialLinearVelocityXZ{ 0.0f, 0.0f };

    // 2.5D Vertical Elevation (Y Axis) Integrator (snappy brawler jump & bounce arc)
    bool UseGravityY = true;
    float VerticalVelocity = 0.0f;
    float GravityY = -22.0f;         // Snappy brawler arc gravity (default: -22.0 m/s^2)
    float GroundRestitutionY = 0.0f; // Bounciness on landing (e.g. 0.45f for wood pickups)
    float GroundLevelY = 0.0f;       // Ground elevation level

    Rigidbody2D() = default;

    void LaunchVertical(float upwardImpulseVelocity) noexcept {
        VerticalVelocity = upwardImpulseVelocity;
    }

    [[nodiscard]] bool IsAirborne(float currentY) const noexcept {
        return currentY > (GroundLevelY + 1e-3f) || VerticalVelocity > 0.0f;
    }

    [[nodiscard]] bool IsAirborne() const noexcept;
    [[nodiscard]] bool IsGrounded(float currentY) const noexcept { return !IsAirborne(currentY); }
    [[nodiscard]] bool IsGrounded() const noexcept { return !IsAirborne(); }

    void SetLinearVelocityXZ(const glm::vec2& velocityXZ) noexcept;
    void SetLinearVelocityXZ(float vx, float vz) noexcept { SetLinearVelocityXZ(glm::vec2{ vx, vz }); }
    [[nodiscard]] glm::vec2 GetLinearVelocityXZ() const noexcept;

    void ApplyLinearImpulseXZ(const glm::vec2& impulseXZ, bool wake = true) noexcept;
    void ApplyLinearImpulseXZ(float ix, float iz, bool wake = true) noexcept { ApplyLinearImpulseXZ(glm::vec2{ ix, iz }, wake); }
};

class BoxCollider2D : public Component {
public:
    glm::vec2 size = { 1.0f, 1.0f };   // Base footprint on the XZ ground plane (size.x = Width X, size.y = Depth Z)
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from Transform center on the XZ ground plane
    bool isDynamic = false;            // True for moving entities in custom isometric solver

    // Physical material properties for Box2D
    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;          // Bounciness on the XZ plane
    float restitutionThreshold = 0.5f; // Velocity required to bounce

    // Pointer or handle to internal Box2D shape/fixture
    void* RuntimeFixture = nullptr;

    // Pointer or handle to static Box2D body if this entity has no Rigidbody2D
    void* RuntimeBody = nullptr;

    // Vertical Y-axis isometric physics (jumping & gravity)
    bool useGravity = true;
    float gravity = -9.8f;
    float verticalVelocity = 0.0f;
    float groundLevel = 0.0f;

    bool IsGrounded(float currentY) const {
        return currentY <= (groundLevel + 1e-4f) && verticalVelocity <= 0.0f;
    }
};

} // namespace Engine

#endif // PHYSICS_PHYSICSCOMPONENTS_H
