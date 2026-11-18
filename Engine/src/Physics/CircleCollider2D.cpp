#include "Engine/Physics/CircleCollider2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"
#include "PhysicsInternalHelpers.h"

#include <algorithm>
#include <cmath>

namespace Engine {

using namespace PhysicsDetail;

void CircleCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    const glm::vec3 worldScale = transform.GetWorldScale();
    float scaleFactor = std::max(std::abs(worldScale.x), std::abs(worldScale.z));
    float effectiveRadius = std::max(0.001f, Radius * scaleFactor);
    b2Circle circle;
    circle.center = b2Vec2{ OffsetXZ.x, OffsetXZ.y };
    circle.radius = effectiveRadius;

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        RuntimeShape = ToHandle(shapeId);
        ENGINE_CORE_DEBUG("CircleCollider2D '{}': Created circle shape (r={:.3f}, sensor={}, cat=0x{:x})",
                          go ? go->GetName() : "<unowned>", effectiveRadius, IsSensor, CategoryBits);
    } else {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        b2Circle cur = b2Shape_GetCircle(shapeId);
        if (std::abs(cur.radius - effectiveRadius) > 1e-4f ||
            std::abs(cur.center.x - circle.center.x) > 1e-4f ||
            std::abs(cur.center.y - circle.center.y) > 1e-4f) {
            b2Shape_SetCircle(shapeId, &circle);
        }
    }
}

} // namespace Engine
