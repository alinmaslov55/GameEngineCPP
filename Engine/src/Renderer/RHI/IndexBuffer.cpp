#include "Engine/Renderer/RHI/IndexBuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

IndexBuffer::IndexBuffer(const uint32_t* indices, uint32_t count) : m_Count(count) {
    if (glad_glGenBuffers == nullptr || glad_glBindBuffer == nullptr || glad_glBufferData == nullptr) {
        return;
    }

    GLint prevVAO = 0;
    GLint prevEBO = 0;
    if (glad_glGetIntegerv != nullptr) {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEBO);
    }

    // Unbind VAO temporarily to avoid inadvertently mutating its element array buffer
    if (prevVAO != 0 && glad_glBindVertexArray != nullptr) {
        glBindVertexArray(0);
    }

    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);

    // Restore previous VAO/EBO bindings
    if (prevVAO != 0 && glad_glBindVertexArray != nullptr) {
        glBindVertexArray(static_cast<GLuint>(prevVAO));
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevEBO));
    }

    ENGINE_CORE_DEBUG("IndexBuffer created with ID: {} (count: {})", m_RendererID, count);
}

IndexBuffer::IndexBuffer(uint32_t* indices, uint32_t count)
    : IndexBuffer(static_cast<const uint32_t*>(indices), count) {}

IndexBuffer::IndexBuffer(std::span<const uint32_t> indices)
    : IndexBuffer(indices.data(), static_cast<uint32_t>(indices.size())) {}

IndexBuffer::~IndexBuffer() {
    if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
        ENGINE_CORE_DEBUG("IndexBuffer destroyed with ID: {}", m_RendererID);
        glDeleteBuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_Count(std::exchange(other.m_Count, 0)) {
}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
            glDeleteBuffers(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Count = std::exchange(other.m_Count, 0);
    }
    return *this;
}

void IndexBuffer::Bind() const {
    if (m_RendererID == 0 || glad_glBindBuffer == nullptr) {
        return;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
}

void IndexBuffer::Unbind() const {
    if (glad_glBindBuffer == nullptr) {
        return;
    }
    GLint boundVAO = 0;
    if (glad_glGetIntegerv != nullptr) {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
    }
    // Only unbind if no VAO is currently bound to avoid stripping the VAO's EBO
    if (boundVAO == 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

} // namespace Engine
