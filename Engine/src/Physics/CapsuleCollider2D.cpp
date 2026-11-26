#include "Engine/Physics/CapsuleCollider2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"
#include "PhysicsInternalHelpers.h"

#include <algorithm>
#include <cmath>

namespace Engine {

using namespace PhysicsDetail;

void CapsuleCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    const glm::vec3 worldScale = transform.GetWorldScale();
    const float scaleX = std::isfinite(worldScale.x) ? std::abs(worldScale.x) : 1.0f;
    const float scaleZ = std::isfinite(worldScale.z) ? std::abs(worldScale.z) : 1.0f;
    const float scaleFactor = std::max(scaleX, scaleZ);
    const float safeC1X = std::isfinite(Center1.x) ? Center1.x : 0.0f;
    const float safeC1Y = std::isfinite(Center1.y) ? Center1.y : 0.0f;
    const float safeC2X = std::isfinite(Center2.x) ? Center2.x : 0.0f;
    const float safeC2Y = std::isfinite(Center2.y) ? Center2.y : 0.0f;
    const float safeRadius = std::isfinite(Radius) ? std::abs(Radius) : 0.5f;
    b2Capsule capsule;
    capsule.center1 = b2Vec2{ safeC1X * scaleX, safeC1Y * scaleZ };
    capsule.center2 = b2Vec2{ safeC2X * scaleX, safeC2Y * scaleZ };
    capsule.radius = std::clamp(safeRadius * scaleFactor, 0.001f, 1000.0f);

    const float dx = capsule.center2.x - capsule.center1.x;
    const float dy = capsule.center2.y - capsule.center1.y;
    const float halfLength = 0.5f * std::sqrt(dx * dx + dy * dy);
    const bool isDegenerate = !std::isfinite(halfLength) || halfLength <= 1e-5f;

    b2Circle fallbackCircle;
    fallbackCircle.center = b2Vec2{ 0.5f * (capsule.center1.x + capsule.center2.x),
                                    0.5f * (capsule.center1.y + capsule.center2.y) };
    fallbackCircle.radius = capsule.radius;

    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        const b2ShapeType existingType = b2Shape_GetType(shapeId);
        if ((isDegenerate && existingType != b2_circleShape) ||
            (!isDegenerate && halfLength > 1e-3f && existingType == b2_circleShape)) {
            b2DestroyShape(shapeId, true);
            RuntimeShape.Reset();
        }
    }

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = isDegenerate
            ? b2CreateCircleShape(bodyId, &shapeDef, &fallbackCircle)
            : b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        RuntimeShape = ToHandle(shapeId);
        ENGINE_CORE_DEBUG("CapsuleCollider2D '{}': Created {} shape (r={:.3f}, sensor={}, cat=0x{:x})",
                          go ? go->GetName() : "<unowned>",
                          (b2Shape_GetType(shapeId) == b2_circleShape) ? "circle-fallback" : "capsule",
                          capsule.radius, IsSensor, CategoryBits);
    } else {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        const b2ShapeType existingType = b2Shape_GetType(shapeId);
        if (existingType == b2_circleShape) {
            b2Circle cur = b2Shape_GetCircle(shapeId);
            if (std::abs(cur.radius - fallbackCircle.radius) > 1e-4f ||
                std::abs(cur.center.x - fallbackCircle.center.x) > 1e-4f ||
                std::abs(cur.center.y - fallbackCircle.center.y) > 1e-4f) {
                b2Shape_SetCircle(shapeId, &fallbackCircle);
            }
        } else {
            b2Capsule cur = b2Shape_GetCapsule(shapeId);
            if (std::abs(cur.radius - capsule.radius) > 1e-4f ||
                std::abs(cur.center1.x - capsule.center1.x) > 1e-4f ||
                std::abs(cur.center1.y - capsule.center1.y) > 1e-4f ||
                std::abs(cur.center2.x - capsule.center2.x) > 1e-4f ||
                std::abs(cur.center2.y - capsule.center2.y) > 1e-4f) {
                b2Shape_SetCapsule(shapeId, &capsule);
            }
        }
    }
}

} // namespace Engine
