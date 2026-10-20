#include "Engine/Renderer/Buffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>

namespace Engine {
VertexBuffer::VertexBuffer(float* vertices, uint32_t size) {
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    ENGINE_CORE_DEBUG("VertexBuffer created with ID: {} (size: {} bytes)", m_RendererID, size);
}

VertexBuffer::~VertexBuffer() {
    ENGINE_CORE_DEBUG("VertexBuffer destroyed with ID: {}", m_RendererID);
    glDeleteBuffers(1, &m_RendererID);
}

void VertexBuffer::Bind() const {
    ENGINE_CORE_TRACE("VertexBuffer ID {} bound", m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
}

void VertexBuffer::Unbind() const {
    ENGINE_CORE_TRACE("VertexBuffer ID {} unbound", m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

IndexBuffer::IndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count) {
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    ENGINE_CORE_DEBUG("IndexBuffer created with ID: {} (count: {})", m_RendererID, count);
}

IndexBuffer::~IndexBuffer() {
    ENGINE_CORE_DEBUG("IndexBuffer destroyed with ID: {}", m_RendererID);
    glDeleteBuffers(1, &m_RendererID);
}

void IndexBuffer::Bind() const {
    ENGINE_CORE_TRACE("IndexBuffer ID {} bound", m_RendererID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
}

void IndexBuffer::Unbind() const {
    ENGINE_CORE_TRACE("IndexBuffer ID {} unbound", m_RendererID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
} // namespace Engine
