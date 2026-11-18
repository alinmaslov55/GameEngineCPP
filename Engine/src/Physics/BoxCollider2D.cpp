#include "Engine/Physics/BoxCollider2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"
#include "PhysicsInternalHelpers.h"

#include <algorithm>
#include <cmath>

namespace Engine {

using namespace PhysicsDetail;

void BoxCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    b2Vec2 center = { offset.x, offset.y };
    b2Rot rot = b2MakeRot(0.0f);
    const glm::vec3 worldScale = transform.GetWorldScale();
    float halfW = std::max(0.001f, size.x * std::abs(worldScale.x) * 0.5f);
    float halfD = std::max(0.001f, size.y * std::abs(worldScale.z) * 0.5f);
    b2Polygon box = b2MakeOffsetBox(halfW, halfD, center, rot);

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
        RuntimeShape = ToHandle(shapeId);
        ENGINE_CORE_DEBUG("BoxCollider2D '{}': Created polygon shape (halfW={:.3f}, halfD={:.3f}, sensor={}, cat=0x{:x})",
                          go ? go->GetName() : "<unowned>", halfW, halfD, IsSensor, CategoryBits);
    } else {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        b2Polygon cur = b2Shape_GetPolygon(shapeId);
        if (cur.count != box.count ||
            std::abs(cur.vertices[0].x - box.vertices[0].x) > 1e-4f ||
            std::abs(cur.vertices[0].y - box.vertices[0].y) > 1e-4f ||
            std::abs(cur.vertices[2].x - box.vertices[2].x) > 1e-4f ||
            std::abs(cur.vertices[2].y - box.vertices[2].y) > 1e-4f) {
            b2Shape_SetPolygon(shapeId, &box);
        }
    }
}

} // namespace Engine
