#ifndef RENDERER_EDITORCAMERA_H
#define RENDERER_EDITORCAMERA_H

#include "Engine/Renderer/PerspectiveCamera.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

class EditorCamera {
public:
    EditorCamera(float aspectRatio = 16.0f / 9.0f, float distance = 15.0f);

    void OnViewportResize(uint32_t width, uint32_t height);
    void OnMouseScroll(float yOffset);
    void Pan(const glm::vec2& pixelDelta, const glm::vec2& viewportSize);
    void Orbit(float deltaYawDegrees, float deltaPitchDegrees = 0.0f);
    void Reset();

    const PerspectiveCamera& GetCamera() const { return m_Camera; }
    PerspectiveCamera& GetCamera() { return m_Camera; }

    // Focal point on the 3D ground plane
    const glm::vec3& GetFocalPoint() const { return m_Camera.GetFocalPoint(); }
    void SetFocalPoint(const glm::vec3& focalPoint);

    // Position aliases for convenience / compatibility
    const glm::vec3& GetPosition() const { return m_Camera.GetFocalPoint(); }
    void SetPosition(const glm::vec3& focalPoint) { SetFocalPoint(focalPoint); }

    float GetDistance() const { return m_Camera.GetDistance(); }
    void SetDistance(float distance);

    // ZoomLevel aliases mapped to camera Distance for UI & test convenience
    float GetZoomLevel() const { return m_Camera.GetDistance(); }
    void SetZoomLevel(float distance) { SetDistance(distance); }

    float GetYawDegrees() const { return m_Camera.GetYawDegrees(); }
    float GetPitchDegrees() const { return m_Camera.GetPitchDegrees(); }

private:
    float m_DefaultDistance = 15.0f;
    glm::vec3 m_DefaultFocalPoint = { 6.5f, 0.0f, 6.5f };
    float m_DefaultPitchDegrees = 40.0f;
    float m_DefaultYawDegrees = 0.0f;
    PerspectiveCamera m_Camera;
};

} // namespace Engine

#endif // RENDERER_EDITORCAMERA_H
