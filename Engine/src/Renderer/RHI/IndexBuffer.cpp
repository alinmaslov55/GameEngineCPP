#include "Engine/Renderer/RHI/IndexBuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

IndexBuffer::IndexBuffer(const uint32_t* indices, uint32_t count) : m_Count(count) {
    if (glad_glGenBuffers == nullptr || glad_glBindBuffer == nullptr || glad_glBufferData == nullptr) {
        return;
    }

    GLint prevCopyWrite = 0;
    if (glad_glGetIntegerv != nullptr) {
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &prevCopyWrite);
    }

    // Upload via GL_COPY_WRITE_BUFFER so we never mutate VAO state or bind GL_ELEMENT_ARRAY_BUFFER with VAO == 0
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(count) * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<GLuint>(prevCopyWrite));

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
    // EBO binding is owned by the bound VAO; binding GL_ELEMENT_ARRAY_BUFFER with VAO == 0
    // triggers GL_INVALID_OPERATION on strict OpenGL 4.1 Core contexts.
}

} // namespace Engine
