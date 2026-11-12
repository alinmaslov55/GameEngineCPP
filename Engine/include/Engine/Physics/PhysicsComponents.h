#ifndef PHYSICS_PHYSICSCOMPONENTS_H
#define PHYSICS_PHYSICSCOMPONENTS_H

#include "Engine/Scene/Component.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Physics/PhysicsHandles.h"
#include <glm/glm.hpp>

namespace Engine {

class Transform;

class Rigidbody2D final : public Component {
public:
    enum class BodyType {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

    BodyType Type = BodyType::Dynamic;
    bool FixedRotation = true;    // Default: true for top-down standees
    float LinearDamping = 4.5f;   // Top-down ground stopping friction
    bool UseTerrainFriction = false; // When true, updates LinearDamping dynamically from ground surface (TER-04)
    float AngularDamping = 6.0f;  // Top-down rotational stopping friction
    float GravityScaleY = 1.0f;   // Multiplier for vertical 2.5D gravity
    float MassMultiplier = 1.0f;  // Scaled by physical roots ("Anvil", "Iron Bone")
    bool IsBullet = false;        // Continuous Collision Detection (CCD) for high-speed bowling

    // Type-safe handle to internal Box2D body (only valid during runtime / play mode)
    BodyHandle RuntimeBody;

    // Initial velocity on the XZ ground plane applied when entering runtime play mode
    glm::vec2 InitialLinearVelocityXZ{ 0.0f, 0.0f };

    // 2.5D Vertical Elevation (Y Axis) Integrator (snappy brawler jump & bounce arc)
    bool UseGravityY = true;
    float VerticalVelocity = 0.0f;
    float GravityY = -22.0f;         // Snappy brawler arc gravity (default: -22.0 m/s^2)
    float GroundRestitutionY = 0.0f; // Bounciness on landing (e.g. 0.45f for wood pickups)
    float GroundLevelY = 0.0f;       // Ground elevation level

    // Airborne Elevation Filtering
    float AirborneClearanceHeight = 0.75f; // Vault clearance height
    bool EnableAirbornePassThrough = true;  // If true, clears AirbornePassThroughMask when airborne
    uint64_t AirbornePassThroughMask = PhysicsLayer::DefaultAirborneExcluded; // Layers bypassed mid-air

    [[nodiscard]] bool IsAirbornePassThroughActive() const noexcept { return m_AirbornePassThroughActive; }
    void SetAirbornePassThroughActive(bool active) noexcept { m_AirbornePassThroughActive = active; }

    Rigidbody2D() = default;

    void LaunchVertical(float upwardImpulseVelocity) noexcept {
        VerticalVelocity = upwardImpulseVelocity;
    }

    [[nodiscard]] bool IsAirborne(float currentY) const noexcept {
        return currentY > (GroundLevelY + 1e-3f) || VerticalVelocity > 0.0f;
    }

    [[nodiscard]] bool IsAirborne() const noexcept;
    [[nodiscard]] bool IsGrounded(float currentY) const noexcept { return !IsAirborne(currentY); }
    [[nodiscard]] bool IsGrounded() const noexcept { return !IsAirborne(); }

    // Kinetic manipulation APIs
    void ApplyImpulseXZ(const glm::vec2& impulseXZ, bool wake = true);
    void ApplyImpulseXZ(float ix, float iz, bool wake = true) { ApplyImpulseXZ(glm::vec2{ ix, iz }, wake); }
    void ApplyForceXZ(const glm::vec2& forceXZ, bool wake = true);
    void ApplyForceXZ(float fx, float fz, bool wake = true) { ApplyForceXZ(glm::vec2{ fx, fz }, wake); }
    void SetVelocityXZ(const glm::vec2& velocityXZ);
    void SetVelocityXZ(float vx, float vz) { SetVelocityXZ(glm::vec2{ vx, vz }); }
    [[nodiscard]] glm::vec2 GetVelocityXZ() const noexcept;
    void SetPositionXZ(const glm::vec2& worldXZ);
    void SetPositionXZ(float x, float z) { SetPositionXZ(glm::vec2{ x, z }); }

    void SetLinearDampingRuntime(float damping);
    [[nodiscard]] float GetLinearDamping() const noexcept;
    void SetAngularDampingRuntime(float damping);
    [[nodiscard]] float GetAngularDamping() const noexcept;
    void SetMassMultiplier(float multiplier);
    [[nodiscard]] float GetMassMultiplier() const noexcept { return MassMultiplier; }
    [[nodiscard]] float GetMass() const noexcept;
    void SetBaseMass(float baseMass) noexcept { m_BaseMass = baseMass; }
    [[nodiscard]] float GetBaseMass() const noexcept { return m_BaseMass; }
    void ApplyMassMultiplierToBody();
    void RefreshMassFromShapes();
    void ResetRuntimeState() noexcept {
        RuntimeBody.Reset();
        m_BaseMass = 0.0f;
        m_AppliedMassMultiplier = 1.0f;
    }
    void SetBullet(bool bullet);
    [[nodiscard]] bool GetBullet() const noexcept;

    // Component lifecycle hooks
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnPoolAcquire() override;
    void OnPoolRelease() override;
    void FixedUpdate(float fixedDeltaTime) override;

    // Backward-compatible aliases for PHY-01
    void SetLinearVelocityXZ(const glm::vec2& velocityXZ) noexcept { SetVelocityXZ(velocityXZ); }
    void SetLinearVelocityXZ(float vx, float vz) noexcept { SetVelocityXZ(glm::vec2{ vx, vz }); }
    [[nodiscard]] glm::vec2 GetLinearVelocityXZ() const noexcept { return GetVelocityXZ(); }
    void ApplyLinearImpulseXZ(const glm::vec2& impulseXZ, bool wake = true) noexcept { ApplyImpulseXZ(impulseXZ, wake); }
    void ApplyLinearImpulseXZ(float ix, float iz, bool wake = true) noexcept { ApplyImpulseXZ(glm::vec2{ ix, iz }, wake); }

    // Chronos Stasis
    void SetStasisFrozen(bool frozen);
    [[nodiscard]] bool IsStasisFrozen() const noexcept { return m_IsStasisFrozen; }
    [[nodiscard]] const glm::vec2& GetQueuedStasisImpulseXZ() const noexcept { return m_QueuedStasisImpulseXZ; }
    [[nodiscard]] const glm::vec2& GetStasisCachedVelocityXZ() const noexcept { return m_StasisCachedVelocityXZ; }
    void SetStasisCachedVelocityXZ(const glm::vec2& vel) noexcept { m_StasisCachedVelocityXZ = vel; }
    [[nodiscard]] float GetStasisCachedVerticalVelocity() const noexcept { return m_StasisCachedVerticalVelocity; }
    void SetStasisCachedVerticalVelocity(float v) noexcept { m_StasisCachedVerticalVelocity = v; }

private:
    bool m_AirbornePassThroughActive = false;
    float m_BaseMass = 0.0f;
    float m_AppliedMassMultiplier = 1.0f;

    // Chronos Stasis state
    bool m_IsStasisFrozen = false;
    glm::vec2 m_StasisCachedVelocityXZ{ 0.0f, 0.0f };
    float m_StasisCachedAngularVelocity = 0.0f;
    float m_StasisCachedVerticalVelocity = 0.0f;
    glm::vec2 m_QueuedStasisImpulseXZ{ 0.0f, 0.0f };
};

/// @brief Abstract base class for all 2D physics colliders.
/// Implements Template Method Pattern for shape creation and unified filter/lifecycle management.
class Collider2D : public Component {
public:
    using ComponentBaseType = Collider2D;

    Collider2D() = default;
    Collider2D(bool isSensor, uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex)
        : IsSensor(isSensor), CategoryBits(categoryBits), MaskBits(maskBits), GroupIndex(groupIndex) {}
    ~Collider2D() override = default;

    // Physical material properties for Box2D
    float Density = 1.0f;
    float Friction = 0.5f;
    float Restitution = 0.0f;          // Bounciness on the XZ plane
    float RestitutionThreshold = 0.5f; // Velocity required to bounce
    bool IsSensor = false;             // Trigger volume without physical contact response
    bool EnableHitEvents = true;       // Enables b2ContactHitEvent generation for impact damage

    // 64-bit Collision Filtering
    uint64_t CategoryBits = PhysicsLayer::Default;
    uint64_t MaskBits = PhysicsLayer::All;
    int32_t GroupIndex = 0;

    void SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex = 0);
    void SetCategoryBits(uint64_t categoryBits);
    void SetMaskBits(uint64_t maskBits);
    void SetGroupIndex(int32_t groupIndex);

    // Type-safe Box2D runtime shape handle
    ShapeHandle RuntimeShape;

    // Type-safe Box2D runtime body handle (attached dynamic Rigidbody2D or standalone static body)
    BodyHandle RuntimeBody;

    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnPoolAcquire() override;
    void OnPoolRelease() override;

    // Template Method: Skeleton algorithm for shape creation and geometry/material synchronization
    void CreateOrUpdateShape(BodyHandle body, const Transform& transform, GameObject* go);

    // Template Method Hook: Pure virtual geometry construction implemented by derived colliders
    virtual void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) = 0;
};

class BoxCollider2D final : public Collider2D {
public:
    BoxCollider2D() = default;
    BoxCollider2D(const glm::vec2& boxSize, bool isSensor = false, const glm::vec2& boxOffset = { 0.0f, 0.0f },
                  uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          size(boxSize), offset(boxOffset) {}

    glm::vec2 size = { 1.0f, 1.0f };   // Base footprint on the XZ ground plane (size.x = Width X, size.y = Depth Z)
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from Transform center on the XZ ground plane

    // Backward-compatibility aliases for existing code and tests:
    ShapeHandle& RuntimeFixture = RuntimeShape;
    float& density = Density;
    float& friction = Friction;
    float& restitution = Restitution;

    BoxCollider2D(const BoxCollider2D& other)
        : Collider2D(other), size(other.size), offset(other.offset),
          RuntimeFixture(RuntimeShape), density(Density), friction(Friction), restitution(Restitution) {}
    BoxCollider2D(BoxCollider2D&& other) noexcept
        : Collider2D(std::move(other)), size(other.size), offset(other.offset),
          RuntimeFixture(RuntimeShape), density(Density), friction(Friction), restitution(Restitution) {}
    BoxCollider2D& operator=(const BoxCollider2D& other) {
        if (this != &other) {
            Collider2D::operator=(other);
            size = other.size;
            offset = other.offset;
        }
        return *this;
    }
    BoxCollider2D& operator=(BoxCollider2D&& other) noexcept {
        if (this != &other) {
            Collider2D::operator=(std::move(other));
            size = other.size;
            offset = other.offset;
        }
        return *this;
    }

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

class CircleCollider2D final : public Collider2D {
public:
    CircleCollider2D() {
        Restitution = 0.15f;
    }
    CircleCollider2D(float radius, bool isSensor = false, const glm::vec2& offsetXZ = { 0.0f, 0.0f },
                     uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          Radius(radius), OffsetXZ(offsetXZ) {
        Restitution = 0.15f;
    }

    float Radius = 0.45f;
    glm::vec2 OffsetXZ{ 0.0f, 0.0f };

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

class CapsuleCollider2D final : public Collider2D {
public:
    CapsuleCollider2D() {
        Restitution = 0.15f;
    }
    CapsuleCollider2D(float radius, const glm::vec2& c1 = { 0.0f, -0.25f }, const glm::vec2& c2 = { 0.0f, 0.25f }, bool isSensor = false,
                      uint64_t categoryBits = PhysicsLayer::Default, uint64_t maskBits = PhysicsLayer::All, int32_t groupIndex = 0)
        : Collider2D(isSensor, categoryBits, maskBits, groupIndex),
          Center1(c1), Center2(c2), Radius(radius) {
        Restitution = 0.15f;
    }

    glm::vec2 Center1{ 0.0f, -0.25f };
    glm::vec2 Center2{ 0.0f, 0.25f };
    float Radius = 0.35f;

    void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) override;
};

} // namespace Engine

#endif // PHYSICS_PHYSICSCOMPONENTS_H
