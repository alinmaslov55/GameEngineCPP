#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include <box2d/box2d.h>
#include <algorithm>

namespace Engine {

static inline b2BodyId ToB2(BodyHandle h) { return b2LoadBodyId(h.Value); }
static inline BodyHandle ToHandle(b2BodyId id) { return BodyHandle{ b2StoreBodyId(id) }; }
static inline b2ShapeId ToB2(ShapeHandle h) { return b2LoadShapeId(h.Value); }
static inline ShapeHandle ToHandle(b2ShapeId id) { return ShapeHandle{ b2StoreShapeId(id) }; }

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
    if (RuntimeBody.IsValid()) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (RuntimeBody.IsValid()) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearVelocity(bodyId, b2Vec2{ velocityXZ.x, velocityXZ.y });
        }
    }
}

glm::vec2 Rigidbody2D::GetVelocityXZ() const noexcept {
    if (m_IsStasisFrozen) {
        return glm::vec2{ 0.0f, 0.0f };
    }
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
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

    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Rot rot = b2Body_GetRotation(bodyId);
            b2Body_SetTransform(bodyId, b2Vec2{ worldXZ.x, worldXZ.y }, rot);
        }
    }
}

void Rigidbody2D::SetLinearDampingRuntime(float damping) {
    LinearDamping = damping;
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearDamping(bodyId, damping);
        }
    }
}

float Rigidbody2D::GetLinearDamping() const noexcept {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetLinearDamping(bodyId);
        }
    }
    return LinearDamping;
}

void Rigidbody2D::SetAngularDampingRuntime(float damping) {
    AngularDamping = damping;
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetAngularDamping(bodyId, damping);
        }
    }
}

float Rigidbody2D::GetAngularDamping() const noexcept {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (!RuntimeBody.IsValid() || Type != BodyType::Dynamic || m_IsStasisFrozen) {
        return;
    }
    b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (!RuntimeBody.IsValid() || Type != BodyType::Dynamic) {
        return;
    }
    b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (RuntimeBody.IsValid()) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            const_cast<Rigidbody2D*>(this)->ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            return b2Body_GetMass(bodyId);
        }
    }
    return (m_BaseMass > 0.0001f) ? (m_BaseMass * MassMultiplier) : MassMultiplier;
}

void Rigidbody2D::SetBullet(bool bullet) {
    IsBullet = bullet;
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetBullet(bodyId, bullet);
        }
    }
}

bool Rigidbody2D::GetBullet() const noexcept {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            return b2Body_IsBullet(bodyId);
        }
    }
    return IsBullet;
}

void Rigidbody2D::OnEnable() {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
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
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
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

// ============================================================================
// Collider2D Base Class Implementation (Template Method Pattern)
// ============================================================================

static b2ShapeDef BuildDefaultColliderShapeDef(Collider2D* col, GameObject* go) {
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = col->Density;
    shapeDef.material.friction = col->Friction;
    shapeDef.material.restitution = col->Restitution;
    shapeDef.isSensor = col->IsSensor;
    shapeDef.enableSensorEvents = true;
    shapeDef.enableHitEvents = col->EnableHitEvents;
    shapeDef.enableContactEvents = true;
    shapeDef.userData = col;
    if (col->CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
        col->CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
    }
    shapeDef.filter.categoryBits = col->CategoryBits;
    shapeDef.filter.maskBits = col->MaskBits;
    shapeDef.filter.groupIndex = col->GroupIndex;
    return shapeDef;
}

void Collider2D::OnEnable() {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
        }
    } else if (!RuntimeShape.IsValid()) {
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
                scene->MarkPhysicsDirty(go);
            }
        }
    }
}

void Collider2D::OnDisable() {
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void Collider2D::OnDestroy() {
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
                if (RuntimeShape.IsValid()) {
                    b2ShapeId shapeId = ToB2(RuntimeShape);
                    if (b2Shape_IsValid(shapeId)) {
                        b2DestroyShape(shapeId, true);
                    }
                    RuntimeShape.Reset();
                }
                rb2d->RefreshMassFromShapes();
            } else {
                scene->UnregisterRuntimeBody(go);
            }
        }
    }
}

void Collider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
}

void Collider2D::SetCategoryBits(uint64_t categoryBits) {
    SetFilter(categoryBits, MaskBits, GroupIndex);
}

void Collider2D::SetMaskBits(uint64_t maskBits) {
    SetFilter(CategoryBits, maskBits, GroupIndex);
}

void Collider2D::SetGroupIndex(int32_t groupIndex) {
    SetFilter(CategoryBits, MaskBits, groupIndex);
}

void Collider2D::CreateOrUpdateShape(BodyHandle body, const Transform& transform, GameObject* go) {
    if (!body.IsValid()) return;
    b2BodyId bodyId = ToB2(body);
    if (!b2Body_IsValid(bodyId)) return;

    // Check if sensor state changed on existing shape
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            if (b2Shape_IsSensor(shapeId) != IsSensor) {
                b2DestroyShape(shapeId, true);
                RuntimeShape.Reset();
            }
        } else {
            RuntimeShape.Reset();
        }
    }

    // Call subclass hook to build or update shape geometry
    BuildShapeDefAndCreate(body, transform, go);

    // Synchronize material properties and hit events on shape
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            if (std::abs(b2Shape_GetFriction(shapeId) - Friction) > 1e-4f) {
                b2Shape_SetFriction(shapeId, Friction);
            }
            if (std::abs(b2Shape_GetRestitution(shapeId) - Restitution) > 1e-4f) {
                b2Shape_SetRestitution(shapeId, Restitution);
            }
            if (b2Shape_AreHitEventsEnabled(shapeId) != EnableHitEvents) {
                b2Shape_EnableHitEvents(shapeId, EnableHitEvents);
            }
        }
    }
}

// ============================================================================
// BoxCollider2D Implementation
// ============================================================================

void BoxCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    b2Vec2 center = { offset.x, offset.y };
    b2Rot rot = b2MakeRot(0.0f);
    float halfW = std::max(0.001f, size.x * std::abs(transform.scale.x) * 0.5f);
    float halfD = std::max(0.001f, size.y * std::abs(transform.scale.z) * 0.5f);
    b2Polygon box = b2MakeOffsetBox(halfW, halfD, center, rot);

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
        RuntimeShape = ToHandle(shapeId);
    } else {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        b2Polygon cur = b2Shape_GetPolygon(shapeId);
        if (cur.count != box.count ||
            std::abs(cur.vertices[0].x - box.vertices[0].x) > 1e-4f ||
            std::abs(cur.vertices[0].y - box.vertices[0].y) > 1e-4f) {
            b2Shape_SetPolygon(shapeId, &box);
        }
    }
}

// ============================================================================
// CircleCollider2D Implementation
// ============================================================================

void CircleCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    float scaleFactor = std::max(std::abs(transform.scale.x), std::abs(transform.scale.z));
    float effectiveRadius = std::max(0.001f, Radius * scaleFactor);
    b2Circle circle;
    circle.center = b2Vec2{ OffsetXZ.x, OffsetXZ.y };
    circle.radius = effectiveRadius;

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
        RuntimeShape = ToHandle(shapeId);
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

// ============================================================================
// CapsuleCollider2D Implementation
// ============================================================================

void CapsuleCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    float scaleX = std::abs(transform.scale.x);
    float scaleZ = std::abs(transform.scale.z);
    float scaleFactor = std::max(scaleX, scaleZ);
    b2Capsule capsule;
    capsule.center1 = b2Vec2{ Center1.x * scaleX, Center1.y * scaleZ };
    capsule.center2 = b2Vec2{ Center2.x * scaleX, Center2.y * scaleZ };
    capsule.radius = std::max(0.001f, Radius * scaleFactor);

    if (!RuntimeShape.IsValid()) {
        b2ShapeDef shapeDef = BuildDefaultColliderShapeDef(this, go);
        b2ShapeId shapeId = b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        RuntimeShape = ToHandle(shapeId);
    } else {
        b2ShapeId shapeId = ToB2(RuntimeShape);
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

} // namespace Engine
