#ifndef RENDERER_PERSPECTIVECAMERA_H
#define RENDERER_PERSPECTIVECAMERA_H

#include <glm/glm.hpp>

namespace Engine {

class PerspectiveCamera {
public:
    PerspectiveCamera(float fovDegrees = 32.0f,
                      float aspectRatio = 16.0f / 9.0f,
                      float nearClip = 0.1f,
                      float farClip = 250.0f);

    void SetPerspective(float fovDegrees, float aspectRatio, float nearClip = 0.1f, float farClip = 250.0f);
    void SetAspectRatio(float aspectRatio);

    float GetFOVDegrees() const { return m_FOVDegrees; }
    void SetFOVDegrees(float fovDegrees);

    float GetAspectRatio() const { return m_AspectRatio; }
    float GetNearClip() const { return m_NearClip; }
    float GetFarClip() const { return m_FarClip; }

    const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }
    void SetFocalPoint(const glm::vec3& focalPoint);

    float GetDistance() const { return m_Distance; }
    void SetDistance(float distance);

    float GetPitchDegrees() const { return m_PitchDegrees; }
    void SetPitchDegrees(float pitchDegrees);

    float GetYawDegrees() const { return m_YawDegrees; }
    void SetYawDegrees(float yawDegrees);

    const glm::vec3& GetPosition() const { return m_Position; }
    const glm::vec3& GetForward() const { return m_Forward; }
    const glm::vec3& GetRight() const { return m_Right; }
    const glm::vec3& GetUp() const { return m_Up; }

    // Horizontal ground-plane basis vectors (Y = 0) for camera-relative movement & panning
    const glm::vec3& GetForwardXZ() const { return m_ForwardXZ; }
    const glm::vec3& GetRightXZ() const { return m_RightXZ; }

    const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
    const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
    const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

    // Unprojects Normalized Device Coordinates [-1, 1]^2 onto horizontal plane Y = planeY
    bool RaycastGroundPlane(const glm::vec2& ndc, float planeY, glm::vec3& outHitPoint) const;

    // Computes visible ground plane bounds [minX, minZ] to [maxX, maxZ] on Y = planeY
    bool GetGroundPlaneVisibleAABB(float planeY, glm::vec2& outMinXZ, glm::vec2& outMaxXZ) const;

private:
    void RecalculateProjection();
    void RecalculateView();

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
};

} // namespace Engine

#endif // RENDERER_PERSPECTIVECAMERA_H

