#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Utils/Logger.h"
#include <algorithm>

namespace Engine {

EditorCamera::EditorCamera(float aspectRatio, float zoomLevel)
    : m_AspectRatio(aspectRatio),
      m_ZoomLevel(zoomLevel),
      m_Camera(-aspectRatio * zoomLevel, aspectRatio * zoomLevel, -zoomLevel, zoomLevel) {
    m_Camera.SetPosition(m_Position);
    ENGINE_CORE_DEBUG("EditorCamera initialized (aspect: {:.2f}, zoom: {:.2f})", m_AspectRatio, m_ZoomLevel);
}

void EditorCamera::OnViewportResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
    UpdateProjection();
}

void EditorCamera::OnMouseScroll(float yOffset) {
    m_ZoomLevel -= yOffset * 0.15f * m_ZoomLevel;
    m_ZoomLevel = std::clamp(m_ZoomLevel, 0.25f, 25.0f);
    UpdateProjection();
}

void EditorCamera::Pan(const glm::vec2& pixelDelta, const glm::vec2& viewportSize) {
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        return;
    }

    float worldWidth  = 2.0f * m_AspectRatio * m_ZoomLevel;
    float worldHeight = 2.0f * m_ZoomLevel;

    m_Position.x -= (pixelDelta.x / viewportSize.x) * worldWidth;
    m_Position.y += (pixelDelta.y / viewportSize.y) * worldHeight;
    m_Camera.SetPosition(m_Position);
}

void EditorCamera::Reset() {
    m_Position = { 0.0f, 2.25f, 0.0f };
    m_ZoomLevel = 3.5f;
    m_Camera.SetPosition(m_Position);
    UpdateProjection();
}

void EditorCamera::SetPosition(const glm::vec3& position) {
    m_Position = position;
    m_Camera.SetPosition(m_Position);
}

void EditorCamera::SetZoomLevel(float zoomLevel) {
    m_ZoomLevel = std::clamp(zoomLevel, 0.25f, 25.0f);
    UpdateProjection();
}

void EditorCamera::UpdateProjection() {
    m_Camera.SetProjection(
        -m_AspectRatio * m_ZoomLevel,
         m_AspectRatio * m_ZoomLevel,
        -m_ZoomLevel,
         m_ZoomLevel
    );
}

} // namespace Engine

