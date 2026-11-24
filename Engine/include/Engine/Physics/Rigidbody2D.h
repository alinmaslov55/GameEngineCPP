#ifndef PHYSICS_RIGIDBODY2D_H
#define PHYSICS_RIGIDBODY2D_H

#include "Engine/Scene/Component.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Physics/PhysicsHandles.h"
#include <glm/glm.hpp>

namespace Engine {

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
        m_LastAppliedLinearDamping = -1.0f;
        m_InitialVelocityApplied = false;
    }
    void SetBullet(bool bullet);
    [[nodiscard]] bool GetBullet() const noexcept;
    [[nodiscard]] float GetLastAppliedLinearDamping() const noexcept { return m_LastAppliedLinearDamping; }
    void SetLastAppliedLinearDamping(float damping) noexcept { m_LastAppliedLinearDamping = damping; }

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
    friend class PhysicsWorld2D;

    bool m_AirbornePassThroughActive = false;
    bool m_InitialVelocityApplied = false;
    float m_BaseMass = 0.0f;
    float m_AppliedMassMultiplier = 1.0f;
    float m_LastAppliedLinearDamping = -1.0f;

    // Chronos Stasis state
    bool m_IsStasisFrozen = false;
    glm::vec2 m_StasisCachedVelocityXZ{ 0.0f, 0.0f };
    float m_StasisCachedAngularVelocity = 0.0f;
    float m_StasisCachedVerticalVelocity = 0.0f;
    glm::vec2 m_QueuedStasisImpulseXZ{ 0.0f, 0.0f };
};

} // namespace Engine

#endif // PHYSICS_RIGIDBODY2D_H
