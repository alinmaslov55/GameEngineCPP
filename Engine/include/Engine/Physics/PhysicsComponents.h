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

    Rigidbody2D() = default;
};

class BoxCollider2D : public Component {
public:
    glm::vec2 size = { 1.0f, 1.0f };   // Base size of bounding box in world space / XZ ground plane
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from Transform center
    bool isDynamic = false;            // True for moving entities in custom isometric solver

    // Physical material properties for Box2D
    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;          // Bounciness
    float restitutionThreshold = 0.5f; // Velocity required to bounce

    // Pointer or handle to internal Box2D shape/fixture
    void* RuntimeFixture = nullptr;

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

