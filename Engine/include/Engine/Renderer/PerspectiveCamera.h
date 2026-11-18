#ifndef RENDERER_PERSPECTIVECAMERA_H
#define RENDERER_PERSPECTIVECAMERA_H

#include "Engine/Math/Frustum.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

enum class FOVAdaptationMode : uint8_t {
    FixedVertical = 0,              // Legacy Hor+ (always locks vertical FOV)
    LandscapeVert_PortraitHorz = 1
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

    [[nodiscard]] FOVAdaptationMode GetFOVAdaptationMode() const noexcept { return m_FOVAdaptationMode; }
    void SetFOVAdaptationMode(FOVAdaptationMode mode);

    [[nodiscard]] float GetPortraitHorizontalFOVDegrees() const noexcept { return m_PortraitHorizontalFOVDegrees; }
    void SetPortraitHorizontalFOVDegrees(float horizontalFovDegrees);

    [[nodiscard]] float GetEffectiveVerticalFOVDegrees() const noexcept;
    [[nodiscard]] float GetEffectiveHorizontalFOVDegrees() const noexcept;

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
    float m_PortraitHorizontalFOVDegrees = 22.0f;
    FOVAdaptationMode m_FOVAdaptationMode = FOVAdaptationMode::LandscapeVert_PortraitHorz;
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

