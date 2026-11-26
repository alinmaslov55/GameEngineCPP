#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Physics/Collider2D.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Terrain/SurfaceSample.h"
#include "Engine/Utils/Logger.h"
#include "PhysicsInternalHelpers.h"

#include <algorithm>
#include <cmath>

namespace Engine {

using namespace PhysicsDetail;

bool Rigidbody2D::IsAirborne() const noexcept {
    float currentY = GroundLevelY;
    if (auto* go = GetGameObject()) {
        if (const auto* t = go->GetTransform()) {
            currentY = t->GetParent() ? t->GetWorldPosition().y : t->position.y;
        }
    }
    return IsAirborne(currentY);
}

void Rigidbody2D::ApplyImpulseXZ(const glm::vec2& impulseXZ, bool wake) {
    AssertPhysicsOwnerThread(this, "Rigidbody2D::ApplyImpulseXZ");
    if (!std::isfinite(impulseXZ.x) || !std::isfinite(impulseXZ.y)) {
        return;
    }
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
    if (!std::isfinite(forceXZ.x) || !std::isfinite(forceXZ.y)) {
        return;
    }
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
    if (!std::isfinite(velocityXZ.x) || !std::isfinite(velocityXZ.y)) {
        return;
    }
    if (m_IsStasisFrozen) {
        m_StasisCachedVelocityXZ = velocityXZ;
        return;
    }
    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            b2Body_SetLinearVelocity(bodyId, b2Vec2{ velocityXZ.x, velocityXZ.y });
            m_InitialVelocityApplied = true;
            return;
        }
    }
    InitialLinearVelocityXZ = velocityXZ;
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
                    m_InitialVelocityApplied = true;

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
    if (!std::isfinite(worldXZ.x) || !std::isfinite(worldXZ.y)) {
        return;
    }
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
    if (!std::isfinite(damping) || damping < 0.0f) {
        damping = 0.0f;
    }
    LinearDamping = damping;
    if (std::abs(m_LastAppliedLinearDamping - damping) < 1e-4f) {
        return;
    }
    m_LastAppliedLinearDamping = damping;
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
    if (!std::isfinite(damping) || damping < 0.0f) {
        damping = 0.0f;
    }
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
    if (!std::isfinite(multiplier) || multiplier <= 0.0001f) {
        return;
    }
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
    if (m_BaseMass <= 0.0001f || !std::isfinite(m_BaseMass)) {
        const float currentMass = b2Body_GetMass(bodyId);
        m_BaseMass = (std::isfinite(currentMass) && currentMass > 0.0001f) ? currentMass : 1.0f;
    }
    const float effectiveMultiplier = (std::isfinite(MassMultiplier) && MassMultiplier > 0.0001f) ? MassMultiplier : 1.0f;
    const float targetMass = m_BaseMass * effectiveMultiplier;
    if (!std::isfinite(targetMass) || targetMass <= 0.0001f) {
        return;
    }
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
                    float angleRad = -glm::radians(transform->GetWorldRotation().y);
                    b2Body_SetTransform(bodyId, b2Vec2{ worldPos.x, worldPos.z }, b2MakeRot(angleRad));
                }
            }
            if (InitialLinearVelocityXZ != glm::vec2{ 0.0f, 0.0f }) {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ InitialLinearVelocityXZ.x, InitialLinearVelocityXZ.y });
            } else {
                b2Body_SetLinearVelocity(bodyId, b2Vec2{ 0.0f, 0.0f });
            }
            m_InitialVelocityApplied = true;
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
    m_LastAppliedLinearDamping = -1.0f;
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
    if (std::abs(m_LastAppliedLinearDamping - sample.LinearDamping) > 1e-4f) {
        SetLinearDampingRuntime(sample.LinearDamping);
    }
}

} // namespace Engine
