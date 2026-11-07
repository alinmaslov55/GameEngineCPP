#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include <box2d/box2d.h>

namespace Engine {

bool Rigidbody2D::IsAirborne() const noexcept {
    float currentY = GroundLevelY;
    if (auto* go = GetGameObject()) {
        if (const auto* t = go->GetTransform()) {
            currentY = t->position.y;
        }
    }
    return IsAirborne(currentY);
}

void Rigidbody2D::SetLinearVelocityXZ(const glm::vec2& velocityXZ) noexcept {
    InitialLinearVelocityXZ = velocityXZ;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearVelocity(bodyId, b2Vec2{ velocityXZ.x, velocityXZ.y });
        }
    }
}

glm::vec2 Rigidbody2D::GetLinearVelocityXZ() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
            return glm::vec2{ vel.x, vel.y };
        }
    }
    return InitialLinearVelocityXZ;
}

void Rigidbody2D::ApplyLinearImpulseXZ(const glm::vec2& impulseXZ, bool wake) noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ impulseXZ.x, impulseXZ.y }, wake);
        }
    } else {
        InitialLinearVelocityXZ += impulseXZ;
    }
}

} // namespace Engine
