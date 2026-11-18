#include "Engine/Renderer/RHI/UniformBuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

UniformBuffer::UniformBuffer(uint32_t size, uint32_t bindingPoint)
    : m_Size(size), m_BindingPoint(bindingPoint) {
    if (size == 0 || glad_glGenBuffers == nullptr) {
        return;
    }
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, m_BindingPoint, m_RendererID);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    ENGINE_CORE_DEBUG("UniformBuffer created with ID: {} (size: {} bytes, binding: {})",
                      m_RendererID, m_Size, m_BindingPoint);
}

UniformBuffer::~UniformBuffer() {
    if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
        ENGINE_CORE_DEBUG("UniformBuffer destroyed with ID: {}", m_RendererID);
        glDeleteBuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_Size(std::exchange(other.m_Size, 0)),
      m_BindingPoint(std::exchange(other.m_BindingPoint, 0)) {
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
            glDeleteBuffers(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Size = std::exchange(other.m_Size, 0);
        m_BindingPoint = std::exchange(other.m_BindingPoint, 0);
    }
    return *this;
}

void UniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) const {
    if (m_RendererID == 0 || !data || size == 0 || offset + size > m_Size) {
        return;
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, m_BindingPoint, m_RendererID);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
}

} // namespace Engine
