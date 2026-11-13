#include "Engine/Scene/CameraComponent.h"
#include "Engine/Core/Time.h"
#include "Engine/Utils/Logger.h"

#include <cmath>

namespace Engine {

namespace {

inline float HashGradient1D(int32_t i) noexcept {
    uint32_t n = static_cast<uint32_t>(i);
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return (static_cast<float>(n & 0xFFFFu) / 32767.5f) - 1.0f; // [-1.0f, 1.0f]
}

} // namespace

CameraComponent::CameraComponent()
    : Camera(32.0f, 16.0f / 9.0f, 0.1f, 250.0f),
      m_CurrentDistance(Distance) {
    ApplySettings();
}

void CameraComponent::ApplySettings() {
    Camera.SetFOVDegrees(FOV);
    Camera.SetPortraitHorizontalFOVDegrees(PortraitHorizontalFOV);
    Camera.SetFOVAdaptationMode(FOVMode);
    if (!SmoothFollow || m_FirstSync) {
        Camera.SetDistance(Distance);
        m_CurrentDistance = Distance;
    }
    Camera.SetPitchDegrees(Pitch);
    Camera.SetYawDegrees(Yaw);
}

void CameraComponent::SetViewportSize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    Camera.SetAspectRatio(aspectRatio);
    ApplySettings();
    ENGINE_CORE_DEBUG("CameraComponent: Viewport set to {}x{} (Aspect={:.3f})", width, height, aspectRatio);
}

void CameraComponent::AddTrauma(float amount01, glm::vec2 directionalKickXZ) noexcept {
    m_Trauma = glm::clamp(m_Trauma + std::max(0.0f, amount01), 0.0f, 1.0f);
    m_DirectionalKick += directionalKickXZ;
    ENGINE_CORE_TRACE("CameraComponent::AddTrauma: +{:.2f} -> {:.2f}, Kick=({:.2f}, {:.2f})",
                      amount01, m_Trauma, m_DirectionalKick.x, m_DirectionalKick.y);
}

void CameraComponent::SetAimDirectionXZ(const glm::vec2& directionXZ) noexcept {
    const float len = glm::length(directionXZ);
    if (len > 1.0f) {
        m_TargetAimDirection = directionXZ / len;
    } else {
        m_TargetAimDirection = directionXZ;
    }
}

void CameraComponent::SetDynamicZoomFactor(float factor01) noexcept {
    m_TargetZoomFactor = glm::clamp(factor01, 0.0f, 1.0f);
}

void CameraComponent::ResetToTarget(const glm::vec3& targetPosition) noexcept {
    m_SmoothedFocalPoint = targetPosition;
    m_LogicalTargetPoint = targetPosition;
    m_FocalVelocity = glm::vec3(0.0f);
    m_SmoothedAimOffset = glm::vec2(0.0f);
    m_AimOffsetVelocity = glm::vec2(0.0f);
    m_TargetAimDirection = glm::vec2(0.0f);
    m_CurrentDistance = Distance;
    m_DistanceVelocity = 0.0f;
    m_TargetZoomFactor = 0.0f;
    m_Trauma = 0.0f;
    m_TraumaTimer = 0.0f;
    m_DirectionalKick = glm::vec2(0.0f);
    m_ShakeOffsetXZ = glm::vec2(0.0f);
    m_FirstSync = false;
    m_LastSyncedFrame = std::numeric_limits<uint64_t>::max();

    Camera.SetFocalPoint(targetPosition);
    Camera.SetDistance(Distance);
}

void CameraComponent::UpdateRig(float scaledDt, float unscaledDt, const glm::vec3& targetPosition) {
    if (m_FirstSync) {
        ResetToTarget(targetPosition);
        return;
    }

    scaledDt = std::clamp(scaledDt, 0.0f, 0.10f);
    unscaledDt = std::clamp(unscaledDt, 0.0f, 0.10f);

    // 1. Aim Look-Ahead Offset (XZ)
    const glm::vec2 targetAimOffset = m_TargetAimDirection * AimLookAheadDistance;
    if (AimLookAheadSmoothTime > 1e-4f && scaledDt > 1e-5f) {
        m_SmoothedAimOffset = SmoothDamp(m_SmoothedAimOffset, targetAimOffset, m_AimOffsetVelocity, AimLookAheadSmoothTime, scaledDt);
    } else {
        m_SmoothedAimOffset = targetAimOffset;
        m_AimOffsetVelocity = glm::vec2(0.0f);
    }

    // 2. Logical Target Focal Point
    m_LogicalTargetPoint = targetPosition + glm::vec3(m_SmoothedAimOffset.x, 0.0f, m_SmoothedAimOffset.y);

    // 3. Focal Point Tracking (Smooth Follow)
    if (SmoothFollow && FollowSmoothTime > 1e-4f && scaledDt > 1e-5f) {
        m_SmoothedFocalPoint = SmoothDamp(m_SmoothedFocalPoint, m_LogicalTargetPoint, m_FocalVelocity, FollowSmoothTime, scaledDt);
    } else {
        m_SmoothedFocalPoint = m_LogicalTargetPoint;
        m_FocalVelocity = glm::vec3(0.0f);
    }

    // 4. Dynamic Zoom Expansion
    const float targetDist = Distance + glm::clamp(m_TargetZoomFactor, 0.0f, 1.0f) * DynamicZoomOutMax;
    if (DynamicZoomSmoothTime > 1e-4f && scaledDt > 1e-5f) {
        m_CurrentDistance = SmoothDamp(m_CurrentDistance, targetDist, m_DistanceVelocity, DynamicZoomSmoothTime, scaledDt);
    } else {
        m_CurrentDistance = targetDist;
        m_DistanceVelocity = 0.0f;
    }
    Camera.SetDistance(m_CurrentDistance);

    // 5. Trauma & Screen Shake Decay (Evaluated over UNscaled delta time for hit-stop immunity!)
    if (unscaledDt > 0.0f) {
        if (m_Trauma > 0.0f) {
            m_Trauma = std::max(0.0f, m_Trauma - TraumaDecay * unscaledDt);
            m_TraumaTimer += unscaledDt * ShakeFrequency;
        }

        if (glm::length(m_DirectionalKick) > 1e-5f) {
            m_DirectionalKick *= std::exp(-KickDecayRate * unscaledDt);
            if (glm::length(m_DirectionalKick) < 1e-4f) {
                m_DirectionalKick = glm::vec2(0.0f);
            }
        }
    }

    // 6. Compute Additive Shake Ground-Plane Offset
    if (m_Trauma > 1e-4f || glm::length(m_DirectionalKick) > 1e-4f) {
        const float shake = m_Trauma * m_Trauma; // Quadratic falloff
        const float noiseX = PerlinNoise1D(m_TraumaTimer);
        const float noiseZ = PerlinNoise1D(m_TraumaTimer + 37.0f);
        m_ShakeOffsetXZ = m_DirectionalKick + glm::vec2(noiseX, noiseZ) * (MaxShakeAmplitude * shake);
    } else {
        m_ShakeOffsetXZ = glm::vec2(0.0f);
    }

    // 7. Synchronize Camera Focal Point (Additive non-destructive)
    Camera.SetFocalPoint(m_SmoothedFocalPoint + glm::vec3(m_ShakeOffsetXZ.x, 0.0f, m_ShakeOffsetXZ.y));
}

void CameraComponent::SyncFromTransform(const Transform* transform) {
    ApplySettings();
    if (!transform) {
        return;
    }

    // Guard render-sync stepping per frame so multi-pass rendering never double-decays trauma or damping
    const uint64_t currentFrame = Time::GetFrameCount();
    if (!m_FirstSync && m_LastSyncedFrame == currentFrame) {
        return;
    }

    const glm::vec3 targetPos = transform->GetWorldPosition();
    const float dt = Time::GetDeltaTime();
    const float unscaledDt = Time::GetUnscaledDeltaTime();

    if (m_FirstSync || (!SmoothFollow && dt <= 0.0f)) {
        ResetToTarget(targetPos);
    } else {
        UpdateRig(dt, unscaledDt, targetPos);
    }
    m_LastSyncedFrame = currentFrame;
}

float CameraComponent::PerlinNoise1D(float t) noexcept {
    const float floorVal = std::floor(t);
    const auto i0 = static_cast<int32_t>(floorVal);
    const int32_t i1 = i0 + 1;
    const float f = t - floorVal;

    // Ken Perlin's quintic fade curve: 6f^5 - 15f^4 + 10f^3
    const float u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    const float g0 = HashGradient1D(i0);
    const float g1 = HashGradient1D(i1);

    const float v0 = g0 * f;
    const float v1 = g1 * (f - 1.0f);

    return (v0 + u * (v1 - v0)) * 2.0f;
}

float CameraComponent::SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float deltaTime) noexcept {
    if (deltaTime <= 0.0f) {
        return current;
    }
    smoothTime = std::max(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * deltaTime;
    const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float originalTo = target;

    target = current - change;

    const float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;

    if ((originalTo - current > 0.0f) == (output > originalTo)) {
        output = originalTo;
        currentVelocity = (output - originalTo) / std::max(1e-5f, deltaTime);
    }
    return output;
}

glm::vec2 CameraComponent::SmoothDamp(const glm::vec2& current, const glm::vec2& target, glm::vec2& currentVelocity, float smoothTime, float deltaTime) noexcept {
    return glm::vec2(
        SmoothDamp(current.x, target.x, currentVelocity.x, smoothTime, deltaTime),
        SmoothDamp(current.y, target.y, currentVelocity.y, smoothTime, deltaTime)
    );
}

glm::vec3 CameraComponent::SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& currentVelocity, float smoothTime, float deltaTime) noexcept {
    return glm::vec3(
        SmoothDamp(current.x, target.x, currentVelocity.x, smoothTime, deltaTime),
        SmoothDamp(current.y, target.y, currentVelocity.y, smoothTime, deltaTime),
        SmoothDamp(current.z, target.z, currentVelocity.z, smoothTime, deltaTime)
    );
}

} // namespace Engine
