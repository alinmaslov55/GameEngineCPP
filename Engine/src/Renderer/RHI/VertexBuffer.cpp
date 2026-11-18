#include "Engine/Renderer/RHI/VertexBuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

VertexBuffer::VertexBuffer(uint32_t size)
    : m_Capacity(size) {
    if (glad_glGenBuffers == nullptr) {
        return;
    }
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    ENGINE_CORE_DEBUG("Dynamic VertexBuffer created with ID: {} (capacity: {} bytes)", m_RendererID, size);
}

VertexBuffer::VertexBuffer(const float* vertices, uint32_t size)
    : m_Capacity(size) {
    if (glad_glGenBuffers == nullptr) {
        return;
    }
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    ENGINE_CORE_DEBUG("VertexBuffer created with ID: {} (size: {} bytes)", m_RendererID, size);
}

VertexBuffer::VertexBuffer(float* vertices, uint32_t size)
    : VertexBuffer(static_cast<const float*>(vertices), size) {}

VertexBuffer::VertexBuffer(std::span<const float> vertices)
    : VertexBuffer(vertices.data(), static_cast<uint32_t>(vertices.size_bytes())) {}

VertexBuffer::~VertexBuffer() {
    if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
        ENGINE_CORE_DEBUG("VertexBuffer destroyed with ID: {}", m_RendererID);
        glDeleteBuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_Capacity(std::exchange(other.m_Capacity, 0)),
      m_Layout(std::move(other.m_Layout)) {
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteBuffers != nullptr) {
            glDeleteBuffers(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Capacity = std::exchange(other.m_Capacity, 0);
        m_Layout = std::move(other.m_Layout);
    }
    return *this;
}

void VertexBuffer::Bind() const {
    if (m_RendererID == 0 || glad_glBindBuffer == nullptr) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
}

void VertexBuffer::Unbind() const {
    if (glad_glBindBuffer == nullptr) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::SetData(const void* data, uint32_t size) {
    SetSubData(data, size, 0);
}

void VertexBuffer::SetSubData(const void* data, uint32_t size, uint32_t offset) {
    if (m_RendererID == 0 || glad_glBindBuffer == nullptr) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
}

void VertexBuffer::Orphan() {
    if (m_RendererID == 0 || glad_glBindBuffer == nullptr) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, m_Capacity, nullptr, GL_DYNAMIC_DRAW);
}

} // namespace Engine
