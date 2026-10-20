#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
    : m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), m_ViewMatrix(1.0f) {
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    ENGINE_CORE_DEBUG("OrthographicCamera created with bounds: left={:.2f}, right={:.2f}, bottom={:.2f}, top={:.2f}",
                      left, right, bottom, top);
}

void OrthographicCamera::SetProjection(float left, float right, float bottom, float top) {
    ENGINE_CORE_DEBUG("OrthographicCamera SetProjection: left={:.2f}, right={:.2f}, bottom={:.2f}, top={:.2f}",
                      left, right, bottom, top);
    m_ProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

void OrthographicCamera::RecalculateViewMatrix() {
    ENGINE_CORE_TRACE("OrthographicCamera RecalculateViewMatrix (pos: {:.2f}, {:.2f}, {:.2f}; rot: {:.2f})",
                      m_Position.x, m_Position.y, m_Position.z, m_Rotation);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));

    m_ViewMatrix = glm::inverse(transform);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}
} // namespace Engine
