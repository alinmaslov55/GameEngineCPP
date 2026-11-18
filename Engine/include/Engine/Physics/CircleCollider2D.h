#ifndef PHYSICS_CIRCLECOLLIDER2D_H
#define PHYSICS_CIRCLECOLLIDER2D_H

#include "Engine/Physics/Collider2D.h"
#include <glm/glm.hpp>

namespace Engine {

class CircleCollider2D final : public Collider2D {
public:
    CircleCollider2D() {
        Restitution = 0.15f;
    }
    CircleCollider2D(float radius, bool isSensor = false, const glm::vec2& offsetXZ = { 0.0f, 0.0f },
                     uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          Radius(radius), OffsetXZ(offsetXZ) {
        Restitution = 0.15f;
    }

    float Radius = 0.45f;
    glm::vec2 OffsetXZ{ 0.0f, 0.0f };

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

} // namespace Engine

#endif // PHYSICS_CIRCLECOLLIDER2D_H
