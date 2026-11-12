#ifndef SCENE_CAMERACOMPONENT_H
#define SCENE_CAMERACOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

class CameraComponent final : public Component {
public:
    PerspectiveCamera Camera;

    // Geometric projection properties
    float FOV = 32.0f;
    float PortraitHorizontalFOV = 22.0f;
    FOVAdaptationMode FOVMode = FOVAdaptationMode::LandscapeVert_PortraitHorz;
    float Distance = 14.0f;
    float Pitch = 40.0f;
    float Yaw = 0.0f;
    bool FixedAspectRatio = false;

    // Brawler Camera Rig Properties
    bool SmoothFollow = true;
    float FollowSmoothTime = 0.11f;       // Critical damping smoothing time constant (seconds)
    float AimLookAheadDistance = 1.15f;   // Maximum shift toward aim/movement direction (meters)
    float AimLookAheadSmoothTime = 0.18f; // Smoothing time for aim shift transitions (seconds)
    float DynamicZoomOutMax = 1.8f;       // Maximum additive camera distance expansion (meters)
    float DynamicZoomSmoothTime = 0.25f;  // Smoothing time for dynamic zoom changes (seconds)

    // Trauma Screen Shake Properties
    float TraumaDecay = 1.4f;             // Linear decay rate per unscaled second
    float MaxShakeAmplitude = 0.65f;      // Peak shake displacement in world units (meters)
    float ShakeFrequency = 28.0f;         // Perlin noise temporal frequency (Hz)
    float KickDecayRate = 8.0f;           // Exponential decay rate for directional kick

    CameraComponent();
    ~CameraComponent() override = default;

    /// @brief Applies static projection and orientation settings to the underlying PerspectiveCamera
    void ApplySettings();

    /// @brief Updates aspect ratio and viewport bounds
    void SetViewportSize(uint32_t width, uint32_t height);

    // --- Runtime Controls & Mutators ---

    /// @brief Injects trauma in [0..1] with quadratic falloff and optional directional kick vector
    void AddTrauma(float amount01, glm::vec2 directionalKickXZ = glm::vec2(0.0f)) noexcept;

    /// @brief Sets aim direction vector for combat look-ahead offset (normalized or analog magnitude <= 1.0)
    void SetAimDirectionXZ(const glm::vec2& directionXZ) noexcept;

    /// @brief Sets target dynamic zoom factor [0..1] (e.g. torch flare, boss roar, sprint)
    void SetDynamicZoomFactor(float factor01) noexcept;

    /// @brief Immediately snaps smoothed focal point and distance to target (prevents pan on teleport/spawn)
    void ResetToTarget(const glm::vec3& targetPosition) noexcept;

    // --- State Queries ---
    [[nodiscard]] float GetTrauma() const noexcept { return m_Trauma; }
    [[nodiscard]] glm::vec2 GetShakeOffsetXZ() const noexcept { return m_ShakeOffsetXZ; }
    [[nodiscard]] const glm::vec3& GetSmoothedFocalPoint() const noexcept { return m_SmoothedFocalPoint; }
    [[nodiscard]] const glm::vec3& GetLogicalTargetPoint() const noexcept { return m_LogicalTargetPoint; }
    [[nodiscard]] const glm::vec2& GetSmoothedAimOffset() const noexcept { return m_SmoothedAimOffset; }
    [[nodiscard]] const glm::vec2& GetTargetAimDirection() const noexcept { return m_TargetAimDirection; }
    [[nodiscard]] float GetCurrentDistance() const noexcept { return m_CurrentDistance; }
    [[nodiscard]] float GetTargetZoomFactor() const noexcept { return m_TargetZoomFactor; }

    /// @brief Advances camera rig dynamics explicitly using supplied scaled and unscaled delta times
    void UpdateRig(float scaledDt, float unscaledDt, const glm::vec3& targetPosition);

    /// @brief Overridden transform sync incorporating damping, lookahead, and additive trauma
    void SyncFromTransform(const Transform* transform);
    void SyncFromTransform(const Transform& transform) { SyncFromTransform(&transform); }

    // --- Mathematical Utilities ---
    [[nodiscard]] static float PerlinNoise1D(float t) noexcept;
    [[nodiscard]] static float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float deltaTime) noexcept;
    [[nodiscard]] static glm::vec2 SmoothDamp(const glm::vec2& current, const glm::vec2& target, glm::vec2& currentVelocity, float smoothTime, float deltaTime) noexcept;
    [[nodiscard]] static glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& currentVelocity, float smoothTime, float deltaTime) noexcept;

private:
    // Damping state
    glm::vec3 m_SmoothedFocalPoint = glm::vec3(0.0f);
    glm::vec3 m_FocalVelocity = glm::vec3(0.0f);
    glm::vec2 m_SmoothedAimOffset = glm::vec2(0.0f);
    glm::vec2 m_AimOffsetVelocity = glm::vec2(0.0f);
    glm::vec2 m_TargetAimDirection = glm::vec2(0.0f);
    glm::vec3 m_LogicalTargetPoint = glm::vec3(0.0f);
    float m_CurrentDistance = 14.0f;
    float m_DistanceVelocity = 0.0f;
    float m_TargetZoomFactor = 0.0f;
    bool m_FirstSync = true;

    // Trauma & shake state
    float m_Trauma = 0.0f;
    float m_TraumaTimer = 0.0f;
    glm::vec2 m_DirectionalKick = glm::vec2(0.0f);
    glm::vec2 m_ShakeOffsetXZ = glm::vec2(0.0f);
};

} // namespace Engine

#endif // SCENE_CAMERACOMPONENT_H
