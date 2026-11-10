#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace Engine {

namespace {

[[nodiscard]] float ComputeEffectiveVerticalFOVRadians(float verticalFovDeg,
                                                       float portraitHorizontalFovDeg,
                                                       float aspectRatio,
                                                       FOVAdaptationMode mode) noexcept {
    if (mode == FOVAdaptationMode::LandscapeVert_PortraitHorz && aspectRatio < 1.0f) {
        const float horizFovRad = glm::radians(portraitHorizontalFovDeg);
        const float halfHorizTan = std::tan(horizFovRad * 0.5f);
        return 2.0f * std::atan(halfHorizTan / aspectRatio);
    }
    return glm::radians(verticalFovDeg);
}

} // namespace

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
    const float clamped = std::clamp(fovDegrees, 10.0f, 110.0f);
    if (std::abs(m_FOVDegrees - clamped) < 1e-6f) {
        return;
    }
    m_FOVDegrees = clamped;
    RecalculateProjection();
}

void PerspectiveCamera::SetFOVAdaptationMode(FOVAdaptationMode mode) {
    if (m_FOVAdaptationMode == mode) {
        return;
    }
    m_FOVAdaptationMode = mode;
    RecalculateProjection();
}

void PerspectiveCamera::SetPortraitHorizontalFOVDegrees(float horizontalFovDegrees) {
    const float clamped = std::clamp(horizontalFovDegrees, 10.0f, 120.0f);
    if (std::abs(m_PortraitHorizontalFOVDegrees - clamped) < 1e-6f) {
        return;
    }
    m_PortraitHorizontalFOVDegrees = clamped;
    RecalculateProjection();
}

float PerspectiveCamera::GetEffectiveVerticalFOVDegrees() const noexcept {
    return glm::degrees(ComputeEffectiveVerticalFOVRadians(
        m_FOVDegrees, m_PortraitHorizontalFOVDegrees, m_AspectRatio, m_FOVAdaptationMode));
}

float PerspectiveCamera::GetEffectiveHorizontalFOVDegrees() const noexcept {
    if (m_FOVAdaptationMode == FOVAdaptationMode::LandscapeVert_PortraitHorz && m_AspectRatio < 1.0f) {
        return m_PortraitHorizontalFOVDegrees;
    }
    const float halfVertTan = std::tan(glm::radians(m_FOVDegrees) * 0.5f);
    return glm::degrees(2.0f * std::atan(halfVertTan * m_AspectRatio));
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

void PerspectiveCamera::UpdateDerivedMatrices() {
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    m_InverseViewProjectionMatrix = glm::inverse(m_ViewProjectionMatrix);
    m_Frustum = Frustum::FromViewProjection(m_ViewProjectionMatrix);
}

void PerspectiveCamera::RecalculateProjection() {
    const float effectiveVertFovRad = ComputeEffectiveVerticalFOVRadians(
        m_FOVDegrees, m_PortraitHorizontalFOVDegrees, m_AspectRatio, m_FOVAdaptationMode);
    m_ProjectionMatrix = glm::perspective(effectiveVertFovRad, m_AspectRatio, m_NearClip, m_FarClip);
    UpdateDerivedMatrices();
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
    UpdateDerivedMatrices();
}

bool PerspectiveCamera::RaycastGroundPlane(const glm::vec2& ndc, float planeY, glm::vec3& outHitPoint) const {
    if (std::abs(m_ProjectionMatrix[0][0]) < 1e-6f || std::abs(m_ProjectionMatrix[1][1]) < 1e-6f) {
        return false;
    }

    // Analytical perspective unprojection avoids far-plane hyperbolic w_far precision amplification
    const glm::vec3 rayOrigin = m_Position;
    const glm::vec3 rayDir = glm::normalize(
        m_Right * (ndc.x / m_ProjectionMatrix[0][0]) +
        m_Up    * (ndc.y / m_ProjectionMatrix[1][1]) +
        m_Forward
    );

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

bool PerspectiveCamera::GetGroundPlaneVisibleAABB(float planeY, glm::vec2& outMinXZ, glm::vec2& outMaxXZ) const {
    // Unproject all 8 NDC corners of the view frustum polyhedron
    static const std::array<glm::vec3, 8> kNdcCorners = {{
        { -1.0f, -1.0f, -1.0f }, // 0: Near Bottom-Left
        {  1.0f, -1.0f, -1.0f }, // 1: Near Bottom-Right
        {  1.0f,  1.0f, -1.0f }, // 2: Near Top-Right
        { -1.0f,  1.0f, -1.0f }, // 3: Near Top-Left
        { -1.0f, -1.0f,  1.0f }, // 4: Far Bottom-Left
        {  1.0f, -1.0f,  1.0f }, // 5: Far Bottom-Right
        {  1.0f,  1.0f,  1.0f }, // 6: Far Top-Right
        { -1.0f,  1.0f,  1.0f }  // 7: Far Top-Left
    }};

    std::array<glm::vec3, 8> worldCorners{};
    for (size_t i = 0; i < 8; ++i) {
        glm::vec4 wp = m_InverseViewProjectionMatrix * glm::vec4(kNdcCorners[i], 1.0f);
        if (std::abs(wp.w) > 1e-6f) {
            wp /= wp.w;
        }
        worldCorners[i] = glm::vec3(wp);
    }

    // 12 edges of the frustum hexahedron: 4 side rays, 4 near-plane edges, 4 far-plane edges
    static constexpr std::array<std::pair<uint8_t, uint8_t>, 12> kFrustumEdges = {{
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // Side rays (Near -> Far)
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // Near quad
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }  // Far quad
    }};

    float minX = 1e9f, maxX = -1e9f;
    float minZ = 1e9f, maxZ = -1e9f;
    int hitCount = 0;

    for (const auto& [idxA, idxB] : kFrustumEdges) {
        const glm::vec3& a = worldCorners[idxA];
        const glm::vec3& b = worldCorners[idxB];
        const float dyA = a.y - planeY;
        const float dyB = b.y - planeY;

        if (dyA * dyB <= 0.0f) {
            const float denom = b.y - a.y;
            const float t = (std::abs(denom) > 1e-6f) ? ((planeY - a.y) / denom) : 0.0f;
            const glm::vec3 hit = a + (b - a) * std::clamp(t, 0.0f, 1.0f);
            minX = std::min(minX, hit.x);
            maxX = std::max(maxX, hit.x);
            minZ = std::min(minZ, hit.z);
            maxZ = std::max(maxZ, hit.z);
            hitCount++;
        }
    }

    if (hitCount > 0) {
        const float margin = 2.5f;
        outMinXZ = { minX - margin, minZ - margin };
        outMaxXZ = { maxX + margin, maxZ + margin };
        return true;
    }

    outMinXZ = { m_FocalPoint.x - 60.0f, m_FocalPoint.z - 60.0f };
    outMaxXZ = { m_FocalPoint.x + 60.0f, m_FocalPoint.z + 60.0f };
    return false;
}

} // namespace Engine
