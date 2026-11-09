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
    if (m_IsStasisFrozen) {
        m_QueuedStasisImpulseXZ += impulseXZ;
        return;
    }
    if (RuntimeBody) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ impulseXZ.x, impulseXZ.y }, wake);
        }
    } else {
        InitialLinearVelocityXZ += impulseXZ;
    }
}

void Rigidbody2D::ApplyForceXZ(const glm::vec2& forceXZ, bool wake) {
    if (m_IsStasisFrozen) {
        return;
    }
    if (RuntimeBody) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyForceToCenter(bodyId, b2Vec2{ forceXZ.x, forceXZ.y }, wake);
        }
    }
}

void Rigidbody2D::SetVelocityXZ(const glm::vec2& velocityXZ) {
    if (m_IsStasisFrozen) {
        m_StasisCachedVelocityXZ = velocityXZ;
        return;
    }
    InitialLinearVelocityXZ = velocityXZ;
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearVelocity(bodyId, b2Vec2{ velocityXZ.x, velocityXZ.y });
        }
    }
}

glm::vec2 Rigidbody2D::GetVelocityXZ() const noexcept {
    if (m_IsStasisFrozen) {
        return glm::vec2{ 0.0f, 0.0f };
    }
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
            return glm::vec2{ vel.x, vel.y };
        }
    }
    return InitialLinearVelocityXZ;
}

void Rigidbody2D::SetStasisFrozen(bool frozen) {
    if (m_IsStasisFrozen == frozen) return;
    m_IsStasisFrozen = frozen;

    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            if (m_IsStasisFrozen) {
                // Cache current dynamic velocity, angular velocity, and vertical elevation velocity
                b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
                m_StasisCachedVelocityXZ = { vel.x, vel.y };
                m_StasisCachedAngularVelocity = b2Body_GetAngularVelocity(bodyId);
                m_StasisCachedVerticalVelocity = VerticalVelocity;
                m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };

                // Switch to kinematic and zero out velocities so body stays locked in place
                b2Body_SetType(bodyId, b2_kinematicBody);
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                b2Body_SetAngularVelocity(bodyId, 0.0f);
            } else {
                // Restore original body type
                b2BodyType originalType = b2_dynamicBody;
                if (Type == BodyType::Static) originalType = b2_staticBody;
                else if (Type == BodyType::Kinematic) originalType = b2_kinematicBody;

                b2Body_SetType(bodyId, originalType);
                RefreshMassFromShapes();
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ m_StasisCachedVelocityXZ.x, m_StasisCachedVelocityXZ.y });
                b2Body_SetAngularVelocity(bodyId, m_StasisCachedAngularVelocity);
                VerticalVelocity = m_StasisCachedVerticalVelocity;

                // Release and erupt all accumulated impulses
                if (glm::length(m_QueuedStasisImpulseXZ) > 0.0f) {
                    b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ m_QueuedStasisImpulseXZ.x, m_QueuedStasisImpulseXZ.y }, true);
                    m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
                }
            }
        }
    } else {
        if (m_IsStasisFrozen) {
            m_StasisCachedVelocityXZ = InitialLinearVelocityXZ;
            m_StasisCachedVerticalVelocity = VerticalVelocity;
            m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
        } else {
            InitialLinearVelocityXZ = m_StasisCachedVelocityXZ;
            VerticalVelocity = m_StasisCachedVerticalVelocity;
            if (glm::length(m_QueuedStasisImpulseXZ) > 0.0f) {
                const float mass = std::max(GetMass(), 1e-4f);
                InitialLinearVelocityXZ += m_QueuedStasisImpulseXZ / mass;
                m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
            }
        }
    }
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
    MassMultiplier = multiplier;
    ApplyMassMultiplierToBody();
}

void Rigidbody2D::ApplyMassMultiplierToBody() {
    if (!RuntimeBody || Type != BodyType::Dynamic || m_IsStasisFrozen) {
        return;
    }
    b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
    if (!b2Body_IsValid(bodyId)) {
        return;
    }
    if (m_BaseMass <= 0.0001f) {
        const float currentMass = b2Body_GetMass(bodyId);
        m_BaseMass = (currentMass > 0.0001f) ? currentMass : 1.0f;
    }
    const float effectiveMultiplier = (MassMultiplier > 0.0001f) ? MassMultiplier : 1.0f;
    const float targetMass = m_BaseMass * effectiveMultiplier;
    b2MassData massData = b2Body_GetMassData(bodyId);
    if (std::abs(massData.mass - targetMass) > 1e-5f) {
        if (massData.mass > 0.0001f) {
            massData.rotationalInertia *= (targetMass / massData.mass);
        }
        massData.mass = targetMass;
        b2Body_SetMassData(bodyId, massData);
    }
    m_AppliedMassMultiplier = effectiveMultiplier;
}

void Rigidbody2D::RefreshMassFromShapes() {
    if (!RuntimeBody || Type != BodyType::Dynamic) {
        return;
    }
    b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
    if (!b2Body_IsValid(bodyId)) {
        return;
    }
    if (b2Body_GetShapeCount(bodyId) > 0) {
        b2Body_ApplyMassFromShapes(bodyId);
    }
    const float unscaledMass = b2Body_GetMass(bodyId);
    m_BaseMass = (unscaledMass > 0.0001f) ? unscaledMass : 1.0f;
    m_AppliedMassMultiplier = 1.0f;
    ApplyMassMultiplierToBody();
}

float Rigidbody2D::GetMass() const noexcept {
    if (RuntimeBody) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            const_cast<Rigidbody2D*>(this)->ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetMass(bodyId);
        }
    }
    return (m_BaseMass > 0.0001f) ? (m_BaseMass * MassMultiplier) : MassMultiplier;
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
    } else if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
            scene->MarkPhysicsDirty(go);
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
    ResetRuntimeState();
}

void BoxCollider2D::OnEnable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    } else if (!RuntimeFixture) {
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
                scene->MarkPhysicsDirty(go);
            }
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
            if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
                if (RuntimeFixture) {
                    b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeFixture)));
                    if (b2Shape_IsValid(shapeId)) {
                        b2DestroyShape(shapeId, true);
                    }
                    RuntimeFixture = nullptr;
                }
                rb2d->RefreshMassFromShapes();
            } else {
                scene->UnregisterRuntimeBody(go);
            }
        }
    }
}

void CircleCollider2D::OnEnable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    } else if (!RuntimeShape) {
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
                scene->MarkPhysicsDirty(go);
            }
        }
    }
}

void CircleCollider2D::OnDisable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void CircleCollider2D::OnDestroy() {
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
                if (RuntimeShape) {
                    b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeShape)));
                    if (b2Shape_IsValid(shapeId)) {
                        b2DestroyShape(shapeId, true);
                    }
                    RuntimeShape = nullptr;
                }
                rb2d->RefreshMassFromShapes();
            } else {
                scene->UnregisterRuntimeBody(go);
            }
        }
    }
}

void CapsuleCollider2D::OnEnable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    } else if (!RuntimeShape) {
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
                scene->MarkPhysicsDirty(go);
            }
        }
    }
}

void CapsuleCollider2D::OnDisable() {
    if (RuntimeBody) {
        b2BodyId bodyId = b2LoadBodyId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeBody)));
        if (b2Body_IsValid(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void CapsuleCollider2D::OnDestroy() {
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
                if (RuntimeShape) {
                    b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeShape)));
                    if (b2Shape_IsValid(shapeId)) {
                        b2DestroyShape(shapeId, true);
                    }
                    RuntimeShape = nullptr;
                }
                rb2d->RefreshMassFromShapes();
            } else {
                scene->UnregisterRuntimeBody(go);
            }
        }
    }
}

void BoxCollider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeFixture) {
        b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeFixture)));
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
}

void BoxCollider2D::SetCategoryBits(uint64_t categoryBits) {
    SetFilter(categoryBits, MaskBits, GroupIndex);
}

void BoxCollider2D::SetMaskBits(uint64_t maskBits) {
    SetFilter(CategoryBits, maskBits, GroupIndex);
}

void BoxCollider2D::SetGroupIndex(int32_t groupIndex) {
    SetFilter(CategoryBits, MaskBits, groupIndex);
}

void CircleCollider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeShape) {
        b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeShape)));
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
}

void CircleCollider2D::SetCategoryBits(uint64_t categoryBits) {
    SetFilter(categoryBits, MaskBits, GroupIndex);
}

void CircleCollider2D::SetMaskBits(uint64_t maskBits) {
    SetFilter(CategoryBits, maskBits, GroupIndex);
}

void CircleCollider2D::SetGroupIndex(int32_t groupIndex) {
    SetFilter(CategoryBits, MaskBits, groupIndex);
}

void CapsuleCollider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeShape) {
        b2ShapeId shapeId = b2LoadShapeId(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(RuntimeShape)));
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
}

void CapsuleCollider2D::SetCategoryBits(uint64_t categoryBits) {
    SetFilter(categoryBits, MaskBits, GroupIndex);
}

void CapsuleCollider2D::SetMaskBits(uint64_t maskBits) {
    SetFilter(CategoryBits, maskBits, GroupIndex);
}

void CapsuleCollider2D::SetGroupIndex(int32_t groupIndex) {
    SetFilter(CategoryBits, MaskBits, groupIndex);
}

} // namespace Engine
