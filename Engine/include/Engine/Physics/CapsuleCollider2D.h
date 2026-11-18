#ifndef PHYSICS_CAPSULECOLLIDER2D_H
#define PHYSICS_CAPSULECOLLIDER2D_H

#include "Engine/Physics/Collider2D.h"
#include <glm/glm.hpp>

namespace Engine {

class CapsuleCollider2D final : public Collider2D {
public:
    CapsuleCollider2D() {
        Restitution = 0.15f;
    }
    CapsuleCollider2D(float radius, const glm::vec2& c1 = { 0.0f, -0.25f }, const glm::vec2& c2 = { 0.0f, 0.25f }, bool isSensor = false,
                      uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          Center1(c1), Center2(c2), Radius(radius) {
        Restitution = 0.15f;
    }

    glm::vec2 Center1{ 0.0f, -0.25f };
    glm::vec2 Center2{ 0.0f, 0.25f };
    float Radius = 0.35f;

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

} // namespace Engine

#endif // PHYSICS_CAPSULECOLLIDER2D_H
