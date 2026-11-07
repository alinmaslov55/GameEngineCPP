#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include <box2d/box2d.h>
#include <algorithm>

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

void Rigidbody2D::ApplyImpulseXZ(const glm::vec2& impulseXZ, bool wake) {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ impulseXZ.x, impulseXZ.y }, wake);
        }
    } else {
        InitialLinearVelocityXZ += impulseXZ;
    }
}

void Rigidbody2D::ApplyForceXZ(const glm::vec2& forceXZ, bool wake) {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyForceToCenter(bodyId, b2Vec2{ forceXZ.x, forceXZ.y }, wake);
        }
    }
}

void Rigidbody2D::SetVelocityXZ(const glm::vec2& velocityXZ) {
    InitialLinearVelocityXZ = velocityXZ;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearVelocity(bodyId, b2Vec2{ velocityXZ.x, velocityXZ.y });
        }
    }
}

glm::vec2 Rigidbody2D::GetVelocityXZ() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
            return glm::vec2{ vel.x, vel.y };
        }
    }
    return InitialLinearVelocityXZ;
}

void Rigidbody2D::SetPositionXZ(const glm::vec2& worldXZ) {
    if (auto* go = GetGameObject()) {
        if (auto* t = go->GetTransform()) {
            t->position.x = worldXZ.x;
            t->position.z = worldXZ.y;
        }
    }
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Rot rot = b2Body_GetRotation(bodyId);
            b2Body_SetTransform(bodyId, b2Vec2{ worldXZ.x, worldXZ.y }, rot);
        }
    }
}

void Rigidbody2D::SetLinearDampingRuntime(float damping) {
    LinearDamping = damping;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearDamping(bodyId, damping);
        }
    }
}

float Rigidbody2D::GetLinearDamping() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetLinearDamping(bodyId);
        }
    }
    return LinearDamping;
}

void Rigidbody2D::SetAngularDampingRuntime(float damping) {
    AngularDamping = damping;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetAngularDamping(bodyId, damping);
        }
    }
}

float Rigidbody2D::GetAngularDamping() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetAngularDamping(bodyId);
        }
    }
    return AngularDamping;
}

void Rigidbody2D::SetMassMultiplier(float multiplier) {
    const float prev = (MassMultiplier > 0.0001f) ? MassMultiplier : 1.0f;
    MassMultiplier = multiplier;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2MassData massData = b2Body_GetMassData(bodyId);
            const float ratio = (prev > 0.0001f) ? (multiplier / prev) : multiplier;
            massData.mass *= ratio;
            b2Body_SetMassData(bodyId, massData);
        }
    }
}

float Rigidbody2D::GetMass() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetMass(bodyId);
        }
    }
    return MassMultiplier;
}

void Rigidbody2D::SetBullet(bool bullet) {
    IsBullet = bullet;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetBullet(bodyId, bullet);
        }
    }
}

bool Rigidbody2D::GetBullet() const noexcept {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            return b2Body_IsBullet(bodyId);
        }
    }
    return IsBullet;
}

void Rigidbody2D::OnEnable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    }
}

void Rigidbody2D::OnDisable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void Rigidbody2D::OnDestroy() {
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            scene->UnregisterRuntimeBody(go);
        }
    }
    RuntimeBody = nullptr;
}

void BoxCollider2D::OnEnable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    }
}

void BoxCollider2D::OnDisable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void BoxCollider2D::OnDestroy() {
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            if (!go->GetComponent<Rigidbody2D>()) {
                scene->UnregisterRuntimeBody(go);
            } else if (RuntimeFixture) {
                b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeFixture)));
                if (b2Shape_IsValid(shapeId)) {
                    b2DestroyShape(shapeId, true);
                }
                RuntimeFixture = nullptr;
            }
        }
    }
}

} // namespace Engine
