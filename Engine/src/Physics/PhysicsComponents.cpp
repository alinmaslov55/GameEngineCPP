#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SurfaceSample.h"
#include "Engine/Utils/Logger.h"
#include <box2d/box2d.h>
#include <algorithm>

namespace Engine {

static inline b2BodyId ToB2(BodyHandle h) { return b2LoadBodyId(h.Value); }
static inline BodyHandle ToHandle(b2BodyId id) { return BodyHandle{ b2StoreBodyId(id) }; }
static inline b2ShapeId ToB2(ShapeHandle h) { return b2LoadShapeId(h.Value); }
static inline ShapeHandle ToHandle(b2ShapeId id) { return ShapeHandle{ b2StoreShapeId(id) }; }

static inline void AssertPhysicsOwnerThread([[maybe_unused]] const Component* comp,
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::ApplyImpulseXZ");
    if (m_IsStasisFrozen) {
        m_QueuedStasisImpulseXZ += impulseXZ;
        ENGINE_CORE_TRACE("Rigidbody2D::ApplyImpulseXZ: Queued stasis impulse ({:.2f}, {:.2f}) on '{}'",
                          impulseXZ.x, impulseXZ.y, GetGameObject() ? GetGameObject()->GetName() : "<unowned>");
        return;
    }
    if (RuntimeBody.IsValid()) {
        if (std::abs(MassMultiplier - m_AppliedMassMultiplier) > 1e-5f) {
            ApplyMassMultiplierToBody();
        }
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ impulseXZ.x, impulseXZ.y }, wake);
        } else {
            ENGINE_CORE_WARN("Rigidbody2D::ApplyImpulseXZ: Invalid Box2D body handle on '{}'",
                             GetGameObject() ? GetGameObject()->GetName() : "<unowned>");
        }
    } else {
        InitialLinearVelocityXZ += impulseXZ;
    }
}

void Rigidbody2D::ApplyForceXZ(const glm::vec2& forceXZ, bool wake) {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::ApplyForceXZ");
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
        } else {
            ENGINE_CORE_WARN("Rigidbody2D::ApplyForceXZ: Invalid Box2D body handle on '{}'",
                             GetGameObject() ? GetGameObject()->GetName() : "<unowned>");
        }
    }
}

void Rigidbody2D::SetVelocityXZ(const glm::vec2& velocityXZ) {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetVelocityXZ");
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetStasisFrozen");
    if (m_IsStasisFrozen == frozen) return;
    m_IsStasisFrozen = frozen;
    ENGINE_CORE_DEBUG("Rigidbody2D '{}': StasisFrozen -> {} (hasRuntimeBody={})",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>",
                      frozen, RuntimeBody.IsValid());

    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            if (m_IsStasisFrozen) {
                m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
                m_StasisCachedVerticalVelocity = VerticalVelocity;
                if (Type == BodyType::Static) {
                    // Static bodies must retain b2_staticBody rather than switching to kinematic
                    m_StasisCachedVelocityXZ = { 0.0f, 0.0f };
                    m_StasisCachedAngularVelocity = 0.0f;
                } else {
                    // Cache current velocity and angular velocity
                    b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
                    m_StasisCachedVelocityXZ = { vel.x, vel.y };
                    m_StasisCachedAngularVelocity = b2Body_GetAngularVelocity(bodyId);

                    // Switch to kinematic and zero out velocities so body stays locked in place
                    if (Type == BodyType::Dynamic) {
                        b2Body_SetType(bodyId, b2_kinematicBody);
                    }
                    b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                    b2Body_SetAngularVelocity(bodyId, 0.0f);
                }
            } else {
                VerticalVelocity = m_StasisCachedVerticalVelocity;
                if (Type == BodyType::Static) {
                    m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
                } else {
                    // Restore original body type
                    const b2BodyType originalType = (Type == BodyType::Dynamic) ? b2_dynamicBody : b2_kinematicBody;
                    if (b2Body_GetType(bodyId) != originalType) {
                        b2Body_SetType(bodyId, originalType);
                    }
                    RefreshMassFromShapes();
                    b2Body_SetLinearVelocity(bodyId, b2Vec2{ m_StasisCachedVelocityXZ.x, m_StasisCachedVelocityXZ.y });
                    b2Body_SetAngularVelocity(bodyId, m_StasisCachedAngularVelocity);

                    // Release and erupt all accumulated impulses
                    if (glm::length(m_QueuedStasisImpulseXZ) > 0.0f) {
                        ENGINE_CORE_DEBUG("Rigidbody2D '{}': Erupting queued stasis impulse ({:.2f}, {:.2f})",
                                          GetGameObject() ? GetGameObject()->GetName() : "<unowned>",
                                          m_QueuedStasisImpulseXZ.x, m_QueuedStasisImpulseXZ.y);
                        b2Body_ApplyLinearImpulseToCenter(bodyId, b2Vec2{ m_QueuedStasisImpulseXZ.x, m_QueuedStasisImpulseXZ.y }, true);
                        m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
                    }
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetPositionXZ");
    if (auto* go = GetGameObject()) {
        if (auto* t = go->GetTransform()) {
            if (t->GetParent()) {
                t->SetWorldPosition({ worldXZ.x, t->GetWorldPosition().y, worldXZ.y });
            } else {
                t->position.x = worldXZ.x;
                t->position.z = worldXZ.y;
                t->SetDirty();
            }
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetLinearDampingRuntime");
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetAngularDampingRuntime");
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::ApplyMassMultiplierToBody");
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::RefreshMassFromShapes");
    if (!RuntimeBody.IsValid() || Type != BodyType::Dynamic || m_IsStasisFrozen) {
        return;
    }
    b2BodyId bodyId = ToB2(RuntimeBody);
    if (!b2Body_IsValid(bodyId)) {
        return;
    }
    const int shapeCount = b2Body_GetShapeCount(bodyId);
    if (shapeCount > 0) {
        b2Body_ApplyMassFromShapes(bodyId);
    }
    const float unscaledMass = b2Body_GetMass(bodyId);
    m_BaseMass = (unscaledMass > 0.0001f) ? unscaledMass : 1.0f;
    m_AppliedMassMultiplier = 1.0f;
    ApplyMassMultiplierToBody();
    ENGINE_CORE_TRACE("Rigidbody2D '{}': RefreshMassFromShapes (shapes={}, baseMass={:.3f}, mult={:.2f})",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>",
                      shapeCount, m_BaseMass, MassMultiplier);
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::SetBullet");
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
    AssertPhysicsOwnerThread(this, "Rigidbody2D::OnEnable");
    ENGINE_CORE_TRACE("Rigidbody2D::OnEnable on '{}' (hasRuntimeBody={})",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>", RuntimeBody.IsValid());
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            if (auto* go = GetGameObject()) {
                bool removedStaleShape = false;
                for (auto* col : go->GetComponents<Collider2D>()) {
                    if (!col->IsEnabled() && col->RuntimeShape.IsValid()) {
                        b2ShapeId s = ToB2(col->RuntimeShape);
                        if (b2Shape_IsValid(s)) {
                            b2DestroyShape(s, true);
                        }
                        col->RuntimeShape.Reset();
                        removedStaleShape = true;
                    }
                }
            if (removedStaleShape) {
                RefreshMassFromShapes();
            }
            }
            if (!b2Body_IsEnabled(bodyId)) {
                b2Body_Enable(bodyId);
            }
        }
    } else if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene(); scene && scene->IsRuntimeRunning()) {
            scene->MarkPhysicsDirty(go);
        }
    }
}

void Rigidbody2D::OnDisable() {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::OnDisable");
    ENGINE_CORE_TRACE("Rigidbody2D::OnDisable on '{}' (hasRuntimeBody={})",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>", RuntimeBody.IsValid());
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId) && b2Body_IsEnabled(bodyId)) {
            b2Body_Disable(bodyId);
        }
    }
}

void Rigidbody2D::OnDestroy() {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::OnDestroy");
    ENGINE_CORE_DEBUG("Rigidbody2D::OnDestroy on '{}'",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>");
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            scene->GetPhysicsWorld().UnregisterRigidbody(go, this);
            return;
        }
    }
    ResetRuntimeState();
}

void Rigidbody2D::OnPoolAcquire() {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::OnPoolAcquire");
    ENGINE_CORE_TRACE("Rigidbody2D::OnPoolAcquire on '{}'",
                      GetGameObject() ? GetGameObject()->GetName() : "<unowned>");
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_Enable(bodyId);
            if (auto* go = GetGameObject()) {
                if (auto* transform = go->GetTransform()) {
                    glm::vec3 worldPos = transform->GetWorldPosition();
                    float angleRad = glm::radians(transform->GetWorldRotation().y);
                    b2Body_SetTransform(bodyId, b2Vec2{ worldPos.x, worldPos.z }, b2MakeRot(angleRad));
                }
            }
            if (InitialLinearVelocityXZ != glm::vec2{ 0.0f, 0.0f }) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ InitialLinearVelocityXZ.x, InitialLinearVelocityXZ.y });
            } else {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
            }
            b2Body_SetAngularVelocity(bodyId, 0.0f);
            b2Body_SetAwake(bodyId, true);
        }
    }
    VerticalVelocity = 0.0f;
    m_IsStasisFrozen = false;
    m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
    m_StasisCachedVelocityXZ = { 0.0f, 0.0f };
    m_StasisCachedAngularVelocity = 0.0f;
    m_StasisCachedVerticalVelocity = 0.0f;
    m_AirbornePassThroughActive = false;
}

void Rigidbody2D::OnPoolRelease() {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::OnPoolRelease");
    const bool wasStasisFrozen = m_IsStasisFrozen;
    m_IsStasisFrozen = false;
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            if (wasStasisFrozen) {
                b2BodyType originalType = b2_dynamicBody;
                if (Type == BodyType::Static) originalType = b2_staticBody;
                else if (Type == BodyType::Kinematic) originalType = b2_kinematicBody;
                if (b2Body_GetType(bodyId) != originalType) {
                    b2Body_SetType(bodyId, originalType);
                }
                if (Type == BodyType::Dynamic) {
                    RefreshMassFromShapes();
                }
            }
            if (Type != BodyType::Static) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
                b2Body_SetAngularVelocity(bodyId, 0.0f);
            }
            b2Body_SetAwake(bodyId, false);
            if (b2Body_IsEnabled(bodyId)) {
                b2Body_Disable(bodyId);
            }
        }
    }
    VerticalVelocity = 0.0f;
    m_QueuedStasisImpulseXZ = { 0.0f, 0.0f };
    m_StasisCachedVelocityXZ = { 0.0f, 0.0f };
    m_StasisCachedAngularVelocity = 0.0f;
    m_StasisCachedVerticalVelocity = 0.0f;
    m_AirbornePassThroughActive = false;
}

void Rigidbody2D::FixedUpdate(float fixedDeltaTime) {
    (void)fixedDeltaTime;
    if (!UseTerrainFriction || Type != BodyType::Dynamic || !RuntimeBody.IsValid() || !IsGrounded()) {
        return;
    }
    auto* go = GetGameObject();
    if (!go) return;
    auto* scene = go->GetScene();
    if (!scene) return;
    auto* transform = go->GetTransform();
    if (!transform) return;

    const glm::vec3 worldPos = transform->GetWorldPosition();
    const SurfaceSample sample = scene->SampleSurfaceAtXZ(worldPos.x, worldPos.z);
    SetLinearDampingRuntime(sample.LinearDamping);
}

// ============================================================================
// Collider2D Base Class Implementation (Template Method Pattern)
// ============================================================================

static uint64_t ResolveEffectiveMaskBits(GameObject* go, uint64_t baseMaskBits) noexcept {
    if (go) {
        if (const auto* rb2d = go->GetComponent<Rigidbody2D>(); rb2d && rb2d->IsAirbornePassThroughActive()) {
            return baseMaskBits & ~rb2d->AirbornePassThroughMask;
        }
    }
    return baseMaskBits;
}

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
    shapeDef.filter.maskBits = ResolveEffectiveMaskBits(go, col->MaskBits);
    shapeDef.filter.groupIndex = col->GroupIndex;
    return shapeDef;
}

void Collider2D::OnEnable() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnEnable");
    auto* go = GetGameObject();
    auto* scene = go ? go->GetScene() : nullptr;

    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            if (!RuntimeShape.IsValid() && go) {
                if (auto* transform = go->GetTransform()) {
                    CreateOrUpdateShape(RuntimeBody, *transform, go);
                    if (auto* rb2d = go->GetComponent<Rigidbody2D>();
                        rb2d && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                        rb2d->RefreshMassFromShapes();
                    }
                    if (scene) {
                        scene->InvalidateSpatialGrid();
                    }
                }
            }
            if (go) {
                for (auto* sibling : go->GetComponents<Collider2D>()) {
                    if (sibling != this && !sibling->IsEnabled() && sibling->RuntimeShape.IsValid()) {
                        b2ShapeId s = ToB2(sibling->RuntimeShape);
                        if (b2Shape_IsValid(s)) {
                            b2DestroyShape(s, true);
                        }
                        sibling->RuntimeShape.Reset();
                    }
                }
                auto* rb2d = go->GetComponent<Rigidbody2D>();
                if (go->IsActiveInHierarchy() && (!rb2d || rb2d->IsEnabled())) {
                    if (!b2Body_IsEnabled(bodyId)) {
                        b2Body_Enable(bodyId);
                    }
                }
            }
            return;
        }
        RuntimeBody.Reset();
        RuntimeShape.Reset();
    }

    if (!RuntimeShape.IsValid() && go && scene && scene->IsRuntimeRunning()) {
        scene->MarkPhysicsDirty(go);
    }
}

void Collider2D::OnDisable() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnDisable");
    auto* go = GetGameObject();
    auto* scene = go ? go->GetScene() : nullptr;

    // Box2D v3 has no b2Shape_Disable(): when this Collider2D itself is disabled or removed
    // (as opposed to whole-GO SetActive(false) where IsEnabled() remains true for pooling),
    // destroy only this Collider2D's RuntimeShape so sibling colliders and shared Rigidbody2D remain active.
    if (!IsEnabled() && RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2DestroyShape(shapeId, true);
        }
        RuntimeShape.Reset();

        if (go) {
            if (auto* rb2d = go->GetComponent<Rigidbody2D>();
                rb2d && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                rb2d->RefreshMassFromShapes();
            }
            if (scene) {
                scene->InvalidateSpatialGrid();
            }
        }
    }

    if (RuntimeBody.IsValid()) {
        bool keepBodyEnabled = false;
        if (go && go->IsActiveInHierarchy() && !go->IsPendingDestroy()) {
            if (const auto* rb2d = go->GetComponent<Rigidbody2D>();
                rb2d && rb2d->IsEnabled() && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                keepBodyEnabled = true;
            }
            if (!keepBodyEnabled) {
                for (const auto* other : go->GetComponents<Collider2D>()) {
                    if (other != this && other->IsEnabled() && !other->IsPendingRemoval() &&
                        other->RuntimeBody == RuntimeBody) {
                        keepBodyEnabled = true;
                        break;
                    }
                }
            }
        }
        if (!keepBodyEnabled) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId) && b2Body_IsEnabled(bodyId)) {
                b2Body_Disable(bodyId);
            }
        }
    }
}

void Collider2D::OnDestroy() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnDestroy");
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            scene->GetPhysicsWorld().UnregisterCollider(go, this);
            if (go->IsAlive()) {
                scene->InvalidateSpatialGrid();
            }
            return;
        }
    }
    RuntimeShape.Reset();
    RuntimeBody.Reset();
}

void Collider2D::OnPoolAcquire() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnPoolAcquire");
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
    if (RuntimeBody.IsValid()) {
        auto* go = GetGameObject();
        if (go && !go->GetComponent<Rigidbody2D>()) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId)) {
                b2Body_Enable(bodyId);
                if (auto* transform = go->GetTransform()) {
                    glm::vec3 worldPos = transform->GetWorldPosition();
                    float angleRad = glm::radians(transform->GetWorldRotation().y);
                    b2Body_SetTransform(bodyId, b2Vec2{ worldPos.x, worldPos.z }, b2MakeRot(angleRad));
                }
            }
        }
    }
}

void Collider2D::OnPoolRelease() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnPoolRelease");
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
    if (RuntimeBody.IsValid()) {
        auto* go = GetGameObject();
        if (go && !go->GetComponent<Rigidbody2D>()) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
    }
}

void Collider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    AssertPhysicsOwnerThread(this, "Collider2D::SetFilter");
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = ResolveEffectiveMaskBits(GetGameObject(), MaskBits);
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
    AssertPhysicsOwnerThread(this, "Collider2D::CreateOrUpdateShape");
    if (!body.IsValid()) {
        ENGINE_CORE_WARN("Collider2D::CreateOrUpdateShape: Invalid BodyHandle on '{}'", go ? go->GetName() : "<unowned>");
        return;
    }
    b2BodyId bodyId = ToB2(body);
    if (!b2Body_IsValid(bodyId)) {
        ENGINE_CORE_WARN("Collider2D::CreateOrUpdateShape: Invalid b2BodyId on '{}'", go ? go->GetName() : "<unowned>");
        return;
    }

    // Check if sensor state changed on existing shape
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            if (b2Shape_IsSensor(shapeId) != IsSensor) {
                ENGINE_CORE_DEBUG("Collider2D '{}': Recreating shape due to IsSensor toggle ({})",
                                  go ? go->GetName() : "<unowned>", IsSensor);
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

// ============================================================================
// CircleCollider2D Implementation
// ============================================================================

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

// ============================================================================
// CapsuleCollider2D Implementation
// ============================================================================

void CapsuleCollider2D::BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) {
    b2BodyId bodyId = ToB2(body);
    const glm::vec3 worldScale = transform.GetWorldScale();
    float scaleX = std::abs(worldScale.x);
    float scaleZ = std::abs(worldScale.z);
    float scaleFactor = std::max(scaleX, scaleZ);
    b2Capsule capsule;
    capsule.center1 = b2Vec2{ Center1.x * scaleX, Center1.y * scaleZ };
    capsule.center2 = b2Vec2{ Center2.x * scaleX, Center2.y * scaleZ };
    capsule.radius = std::max(0.001f, Radius * scaleFactor);

    const float dx = capsule.center2.x - capsule.center1.x;
    const float dy = capsule.center2.y - capsule.center1.y;
    const float halfLength = 0.5f * std::sqrt(dx * dx + dy * dy);
    const bool isDegenerate = halfLength <= 1e-5f;

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
