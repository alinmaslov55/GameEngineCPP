#ifndef RENDERER_EDITORCAMERA_H
#define RENDERER_EDITORCAMERA_H

#include "Engine/Renderer/OrthographicCamera.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

class EditorCamera {
public:
    EditorCamera(float aspectRatio = 16.0f / 9.0f, float zoomLevel = 3.5f);

    void OnViewportResize(uint32_t width, uint32_t height);
    void OnMouseScroll(float yOffset);
    void Pan(const glm::vec2& pixelDelta, const glm::vec2& viewportSize);
    void Reset();

    const OrthographicCamera& GetCamera() const { return m_Camera; }

    const glm::vec3& GetPosition() const { return m_Position; }
    void SetPosition(const glm::vec3& position);

    float GetZoomLevel() const { return m_ZoomLevel; }
    void SetZoomLevel(float zoomLevel);

private:
    void UpdateProjection();

private:
    float m_AspectRatio = 16.0f / 9.0f;
    float m_ZoomLevel = 3.5f;
    glm::vec3 m_Position = { 0.0f, 2.25f, 0.0f };
    OrthographicCamera m_Camera;
};

} // namespace Engine

#endif // RENDERER_EDITORCAMERA_H

