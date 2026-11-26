#ifndef PHYSICS_INTERNAL_HELPERS_H
#define PHYSICS_INTERNAL_HELPERS_H

#include "Engine/Physics/Collider2D.h"
#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"

#include <box2d/box2d.h>

namespace Engine::PhysicsDetail {

inline b2BodyId ToB2(BodyHandle h) { return b2LoadBodyId(h.Value); }
inline BodyHandle ToHandle(b2BodyId id) { return BodyHandle{ b2StoreBodyId(id) }; }
inline b2ShapeId ToB2(ShapeHandle h) { return b2LoadShapeId(h.Value); }
inline ShapeHandle ToHandle(b2ShapeId id) { return ShapeHandle{ b2StoreShapeId(id) }; }

inline void AssertPhysicsOwnerThread([[maybe_unused]] const Component* comp,
                                     [[maybe_unused]] const char* operation) noexcept {
#ifndef NDEBUG
    if (comp) {
        if (const auto* go = comp->GetGameObject()) {
            if (const auto* scene = go->GetScene()) {
                scene->AssertMainThreadAffinity(operation);
            }
        }
    }
#endif
}

inline uint64_t ResolveEffectiveMaskBits(GameObject* go, uint64_t baseMaskBits) noexcept {
    if (go) {
        if (const auto* rb2d = go->GetComponent<Rigidbody2D>(); rb2d && rb2d->IsAirbornePassThroughActive()) {
            return baseMaskBits & ~rb2d->AirbornePassThroughMask;
        }
    }
    return baseMaskBits;
}

inline b2ShapeDef BuildDefaultColliderShapeDef(Collider2D* col, GameObject* go) {
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = (std::isfinite(col->Density) && col->Density >= 0.0f) ? col->Density : 0.0f;
    shapeDef.material.friction = (std::isfinite(col->Friction) && col->Friction >= 0.0f) ? col->Friction : 0.0f;
    shapeDef.material.restitution = (std::isfinite(col->Restitution) && col->Restitution >= 0.0f) ? col->Restitution : 0.0f;
    shapeDef.isSensor = col->IsSensor;
    shapeDef.enableSensorEvents = true;
    shapeDef.enableHitEvents = col->EnableHitEvents;
    shapeDef.enableContactEvents = true;
    shapeDef.userData = col;
    if (col->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
        col->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
    }
    shapeDef.filter.categoryBits = col->CategoryBits;
    shapeDef.filter.maskBits = ResolveEffectiveMaskBits(go, col->MaskBits);
    shapeDef.filter.groupIndex = col->GroupIndex;
    return shapeDef;
}

} // namespace Engine::PhysicsDetail

#endif // PHYSICS_INTERNAL_HELPERS_H
