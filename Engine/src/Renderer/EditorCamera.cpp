#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace Engine {

EditorCamera::EditorCamera(float aspectRatio, float distance)
    : m_DefaultDistance(std::clamp(distance, 3.0f, 60.0f)),
      m_Camera(32.0f, aspectRatio, 0.1f, 250.0f) {
    m_Camera.SetFocalPoint(m_DefaultFocalPoint);
    m_Camera.SetDistance(m_DefaultDistance);
    m_Camera.SetPitchDegrees(m_DefaultPitchDegrees);
    m_Camera.SetYawDegrees(m_DefaultYawDegrees);
    ENGINE_CORE_DEBUG("EditorCamera initialized (3D Diorama FOV: 32°, Pitch: {:.1f}°, Distance: {:.2f})",
                      m_DefaultPitchDegrees, m_DefaultDistance);
}

void EditorCamera::OnViewportResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_Camera.SetAspectRatio(aspectRatio);
    ENGINE_CORE_DEBUG("EditorCamera::OnViewportResize: {}x{} (Aspect={:.3f})", width, height, aspectRatio);
}

void EditorCamera::OnMouseScroll(float yOffset) {
    float nextDistance = m_Camera.GetDistance() - yOffset * 0.12f * m_Camera.GetDistance();
    nextDistance = std::clamp(nextDistance, 3.0f, 60.0f);
    m_Camera.SetDistance(nextDistance);
}

void EditorCamera::Pan(const glm::vec2& pixelDelta, const glm::vec2& viewportSize) {
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        return;
    }

    const float halfFovTan = std::tan(glm::radians(m_Camera.GetFOVDegrees() * 0.5f));
    const float frustumHeight = 2.0f * m_Camera.GetDistance() * halfFovTan;
    const float frustumWidth = frustumHeight * m_Camera.GetAspectRatio();

    // Scale ground-plane travel by 1 / sin(pitch) so vertical mouse drag matches ground depth under pitch
    const float sinPitch = std::max(0.25f, std::sin(glm::radians(m_Camera.GetPitchDegrees())));

    const float moveRight = -(pixelDelta.x / viewportSize.x) * frustumWidth;
    const float moveForward = (pixelDelta.y / viewportSize.y) * (frustumHeight / sinPitch);

    const glm::vec3 nextFocal = m_Camera.GetFocalPoint() +
                                m_Camera.GetRightXZ() * moveRight +
                                m_Camera.GetForwardXZ() * moveForward;
    m_Camera.SetFocalPoint(nextFocal);
}

void EditorCamera::Orbit(float deltaYawDegrees, float deltaPitchDegrees) {
    m_Camera.SetYawDegrees(m_Camera.GetYawDegrees() + deltaYawDegrees);
    if (std::abs(deltaPitchDegrees) > 1e-5f) {
        const float nextPitch = std::clamp(m_Camera.GetPitchDegrees() + deltaPitchDegrees, 15.0f, 80.0f);
        m_Camera.SetPitchDegrees(nextPitch);
    }
}

void EditorCamera::Reset() {
    m_Camera.SetFocalPoint(m_DefaultFocalPoint);
    m_Camera.SetDistance(m_DefaultDistance);
    m_Camera.SetPitchDegrees(m_DefaultPitchDegrees);
    m_Camera.SetYawDegrees(m_DefaultYawDegrees);
    ENGINE_CORE_DEBUG("EditorCamera::Reset: FocalPoint=({:.1f}, {:.1f}, {:.1f}), Distance={:.1f}, Pitch={:.1f} deg",
                      m_DefaultFocalPoint.x, m_DefaultFocalPoint.y, m_DefaultFocalPoint.z, m_DefaultDistance, m_DefaultPitchDegrees);
}

void EditorCamera::SetFocalPoint(const glm::vec3& focalPoint) {
    m_Camera.SetFocalPoint(focalPoint);
}

void EditorCamera::SetDistance(float distance) {
    m_Camera.SetDistance(std::clamp(distance, 3.0f, 60.0f));
}

} // namespace Engine
