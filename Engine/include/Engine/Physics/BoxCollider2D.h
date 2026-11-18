#ifndef PHYSICS_BOXCOLLIDER2D_H
#define PHYSICS_BOXCOLLIDER2D_H

#include "Engine/Physics/Collider2D.h"
#include <glm/glm.hpp>

namespace Engine {

class BoxCollider2D final : public Collider2D {
public:
    BoxCollider2D() = default;
    BoxCollider2D(const glm::vec2& boxSize, bool isSensor = false, const glm::vec2& boxOffset = { 0.0f, 0.0f },
                  uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          size(boxSize), offset(boxOffset) {}

    glm::vec2 size = { 1.0f, 1.0f };   // Base footprint on the XZ ground plane (size.x = Width X, size.y = Depth Z)
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from Transform center on the XZ ground plane

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

} // namespace Engine

#endif // PHYSICS_BOXCOLLIDER2D_H
