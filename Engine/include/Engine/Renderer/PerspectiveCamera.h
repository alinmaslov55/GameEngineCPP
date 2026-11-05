#ifndef RENDERER_PERSPECTIVECAMERA_H
#define RENDERER_PERSPECTIVECAMERA_H

#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

struct Frustum {
    // 6 inward-facing planes (Left, Right, Bottom, Top, Near, Far): (nx, ny, nz, d)
    std::array<glm::vec4, 6> Planes{};

    [[nodiscard]] static Frustum FromViewProjection(const glm::mat4& vp) noexcept {
        Frustum frustum;
        const glm::vec4 row0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        const glm::vec4 row1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        const glm::vec4 row2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        const glm::vec4 row3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

        frustum.Planes[0] = NormalizePlane(row3 + row0); // Left
        frustum.Planes[1] = NormalizePlane(row3 - row0); // Right
        frustum.Planes[2] = NormalizePlane(row3 + row1); // Bottom
        frustum.Planes[3] = NormalizePlane(row3 - row1); // Top
        frustum.Planes[4] = NormalizePlane(row3 + row2); // Near
        frustum.Planes[5] = NormalizePlane(row3 - row2); // Far
        return frustum;
    }

    [[nodiscard]] bool IntersectsSphere(const glm::vec3& center, float radius) const noexcept {
        for (const auto& plane : Planes) {
            const float dist = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
            if (dist < -radius) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IntersectsAABB(const glm::vec3& minBounds, const glm::vec3& maxBounds) const noexcept {
        for (const auto& plane : Planes) {
            const glm::vec3 pVertex{
                (plane.x >= 0.0f) ? maxBounds.x : minBounds.x,
                (plane.y >= 0.0f) ? maxBounds.y : minBounds.y,
                (plane.z >= 0.0f) ? maxBounds.z : minBounds.z
            };
            if (plane.x * pVertex.x + plane.y * pVertex.y + plane.z * pVertex.z + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] static glm::vec4 NormalizePlane(const glm::vec4& plane) noexcept {
        const float len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        return (len > 1e-6f) ? (plane / len) : plane;
    }
};

class PerspectiveCamera {
public:
    PerspectiveCamera(float fovDegrees = 32.0f,
                      float aspectRatio = 16.0f / 9.0f,
                      float nearClip = 0.1f,
                      float farClip = 250.0f);

    void SetPerspective(float fovDegrees, float aspectRatio, float nearClip = 0.1f, float farClip = 250.0f);
    void SetAspectRatio(float aspectRatio);

    [[nodiscard]] float GetFOVDegrees() const noexcept { return m_FOVDegrees; }
    void SetFOVDegrees(float fovDegrees);

    [[nodiscard]] float GetAspectRatio() const noexcept { return m_AspectRatio; }
    [[nodiscard]] float GetNearClip() const noexcept { return m_NearClip; }
    [[nodiscard]] float GetFarClip() const noexcept { return m_FarClip; }

    [[nodiscard]] const glm::vec3& GetFocalPoint() const noexcept { return m_FocalPoint; }
    void SetFocalPoint(const glm::vec3& focalPoint);

    [[nodiscard]] float GetDistance() const noexcept { return m_Distance; }
    void SetDistance(float distance);

    [[nodiscard]] float GetPitchDegrees() const noexcept { return m_PitchDegrees; }
    void SetPitchDegrees(float pitchDegrees);

    [[nodiscard]] float GetYawDegrees() const noexcept { return m_YawDegrees; }
    void SetYawDegrees(float yawDegrees);

    [[nodiscard]] const glm::vec3& GetPosition() const noexcept { return m_Position; }
    [[nodiscard]] const glm::vec3& GetForward() const noexcept { return m_Forward; }
    [[nodiscard]] const glm::vec3& GetRight() const noexcept { return m_Right; }
    [[nodiscard]] const glm::vec3& GetUp() const noexcept { return m_Up; }

    // Horizontal ground-plane basis vectors (Y = 0) for camera-relative movement & panning
    [[nodiscard]] const glm::vec3& GetForwardXZ() const noexcept { return m_ForwardXZ; }
    [[nodiscard]] const glm::vec3& GetRightXZ() const noexcept { return m_RightXZ; }

    [[nodiscard]] const glm::mat4& GetProjectionMatrix() const noexcept { return m_ProjectionMatrix; }
    [[nodiscard]] const glm::mat4& GetViewMatrix() const noexcept { return m_ViewMatrix; }
    [[nodiscard]] const glm::mat4& GetViewProjectionMatrix() const noexcept { return m_ViewProjectionMatrix; }
    [[nodiscard]] const glm::mat4& GetInverseViewProjectionMatrix() const noexcept { return m_InverseViewProjectionMatrix; }
    [[nodiscard]] const Frustum& GetFrustum() const noexcept { return m_Frustum; }

    // Unprojects Normalized Device Coordinates [-1, 1]^2 onto horizontal plane Y = planeY
    bool RaycastGroundPlane(const glm::vec2& ndc, float planeY, glm::vec3& outHitPoint) const;

    // Computes visible ground plane bounds [minX, minZ] to [maxX, maxZ] on Y = planeY
    bool GetGroundPlaneVisibleAABB(float planeY, glm::vec2& outMinXZ, glm::vec2& outMaxXZ) const;

private:
    void RecalculateProjection();
    void RecalculateView();
    void UpdateDerivedMatrices();

private:
    float m_FOVDegrees = 32.0f;
    float m_AspectRatio = 16.0f / 9.0f;
    float m_NearClip = 0.1f;
    float m_FarClip = 250.0f;

    glm::vec3 m_FocalPoint = { 6.5f, 0.0f, 6.5f };
    float m_Distance = 14.0f;
    float m_PitchDegrees = 40.0f;
    float m_YawDegrees = 0.0f;

    glm::vec3 m_Position = { 6.5f, 9.0f, 17.2f };
    glm::vec3 m_Forward = { 0.0f, -0.6428f, -0.7660f };
    glm::vec3 m_Right = { 1.0f, 0.0f, 0.0f };
    glm::vec3 m_Up = { 0.0f, 0.7660f, -0.6428f };
    glm::vec3 m_ForwardXZ = { 0.0f, 0.0f, -1.0f };
    glm::vec3 m_RightXZ = { 1.0f, 0.0f, 0.0f };

    glm::mat4 m_ProjectionMatrix{ 1.0f };
    glm::mat4 m_ViewMatrix{ 1.0f };
    glm::mat4 m_ViewProjectionMatrix{ 1.0f };
    glm::mat4 m_InverseViewProjectionMatrix{ 1.0f };
    Frustum m_Frustum{};
};

} // namespace Engine

#endif // RENDERER_PERSPECTIVECAMERA_H

