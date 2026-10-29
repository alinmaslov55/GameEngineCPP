#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

PerspectiveCamera::PerspectiveCamera(float fovDegrees, float aspectRatio, float nearClip, float farClip)
    : m_FOVDegrees(std::clamp(fovDegrees, 10.0f, 110.0f)),
      m_AspectRatio(aspectRatio > 0.001f ? aspectRatio : (16.0f / 9.0f)),
      m_NearClip(std::max(0.01f, nearClip)),
      m_FarClip(std::max(nearClip + 1.0f, farClip)) {
    RecalculateProjection();
    RecalculateView();
    ENGINE_CORE_DEBUG("PerspectiveCamera created: FOV={:.1f} deg, Aspect={:.3f}, Near={:.2f}, Far={:.2f}",
                      m_FOVDegrees, m_AspectRatio, m_NearClip, m_FarClip);
}

void PerspectiveCamera::SetPerspective(float fovDegrees, float aspectRatio, float nearClip, float farClip) {
    m_FOVDegrees = std::clamp(fovDegrees, 10.0f, 110.0f);
    if (aspectRatio > 0.001f) {
        m_AspectRatio = aspectRatio;
    }
    m_NearClip = std::max(0.01f, nearClip);
    m_FarClip = std::max(m_NearClip + 1.0f, farClip);
    RecalculateProjection();
    ENGINE_CORE_DEBUG("PerspectiveCamera::SetPerspective: FOV={:.1f} deg, Aspect={:.3f}, Near={:.2f}, Far={:.2f}",
                      m_FOVDegrees, m_AspectRatio, m_NearClip, m_FarClip);
}

void PerspectiveCamera::SetAspectRatio(float aspectRatio) {
    if (aspectRatio <= 0.001f || std::abs(m_AspectRatio - aspectRatio) < 1e-6f) {
        return;
    }
    m_AspectRatio = aspectRatio;
    RecalculateProjection();
}

void PerspectiveCamera::SetFOVDegrees(float fovDegrees) {
    m_FOVDegrees = std::clamp(fovDegrees, 10.0f, 110.0f);
    RecalculateProjection();
}

void PerspectiveCamera::SetFocalPoint(const glm::vec3& focalPoint) {
    m_FocalPoint = focalPoint;
    RecalculateView();
}

void PerspectiveCamera::SetDistance(float distance) {
    m_Distance = std::clamp(distance, 1.5f, 120.0f);
    RecalculateView();
}

void PerspectiveCamera::SetPitchDegrees(float pitchDegrees) {
    m_PitchDegrees = std::clamp(pitchDegrees, 10.0f, 85.0f);
    RecalculateView();
}

void PerspectiveCamera::SetYawDegrees(float yawDegrees) {
    m_YawDegrees = std::fmod(yawDegrees, 360.0f);
    RecalculateView();
}

void PerspectiveCamera::RecalculateProjection() {
    m_ProjectionMatrix = glm::perspective(glm::radians(m_FOVDegrees), m_AspectRatio, m_NearClip, m_FarClip);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

void PerspectiveCamera::RecalculateView() {
    const float yawRad = glm::radians(m_YawDegrees);
    const float pitchRad = glm::radians(m_PitchDegrees);

    const float sinYaw = std::sin(yawRad);
    const float cosYaw = std::cos(yawRad);
    const float sinPitch = std::sin(pitchRad);
    const float cosPitch = std::cos(pitchRad);

    // At Yaw = 0°, ForwardXZ points North (-Z) into the diorama and RightXZ points East (+X)
    m_ForwardXZ = glm::normalize(glm::vec3(sinYaw, 0.0f, -cosYaw));
    m_RightXZ = glm::normalize(glm::vec3(cosYaw, 0.0f, sinYaw));

    m_Forward = glm::normalize(m_ForwardXZ * cosPitch + glm::vec3(0.0f, -sinPitch, 0.0f));
    m_Right = m_RightXZ;
    m_Up = glm::normalize(glm::cross(m_Right, m_Forward));

    m_Position = m_FocalPoint - m_Forward * m_Distance;
    m_ViewMatrix = glm::lookAt(m_Position, m_FocalPoint, m_Up);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

bool PerspectiveCamera::RaycastGroundPlane(const glm::vec2& ndc, float planeY, glm::vec3& outHitPoint) const {
    const glm::mat4 invVP = glm::inverse(m_ViewProjectionMatrix);

    glm::vec4 nearPoint = invVP * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
    glm::vec4 farPoint  = invVP * glm::vec4(ndc.x, ndc.y,  1.0f, 1.0f);
    if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f) {
        return false;
    }

    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    const glm::vec3 rayOrigin = glm::vec3(nearPoint);
    const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) - rayOrigin);

    if (std::abs(rayDir.y) < 1e-5f) {
        return false;
    }

    const float t = (planeY - rayOrigin.y) / rayDir.y;
    if (t < 0.0f) {
        return false;
    }

    outHitPoint = rayOrigin + rayDir * t;
    ENGINE_CORE_TRACE("PerspectiveCamera::RaycastGroundPlane: Hit at ({:.2f}, {:.2f}, {:.2f}) for NDC ({:.2f}, {:.2f})",
                      outHitPoint.x, outHitPoint.y, outHitPoint.z, ndc.x, ndc.y);
    return true;
}

} // namespace Engine

