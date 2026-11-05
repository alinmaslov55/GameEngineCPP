#include "Engine/Renderer/Buffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

VertexBuffer::VertexBuffer(uint32_t size)
    : m_Capacity(size) {
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    ENGINE_CORE_DEBUG("Dynamic VertexBuffer created with ID: {} (capacity: {} bytes)", m_RendererID, size);
}

VertexBuffer::VertexBuffer(const float* vertices, uint32_t size)
    : m_Capacity(size) {
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
    if (m_RendererID != 0) {
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
        if (m_RendererID != 0) {
            glDeleteBuffers(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Capacity = std::exchange(other.m_Capacity, 0);
        m_Layout = std::move(other.m_Layout);
    }
    return *this;
}

void VertexBuffer::Bind() const {
    ENGINE_CORE_TRACE("VertexBuffer ID {} bound", m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
}

void VertexBuffer::Unbind() const {
    ENGINE_CORE_TRACE("VertexBuffer ID {} unbound", m_RendererID);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::SetData(const void* data, uint32_t size) {
    SetSubData(data, size, 0);
}

void VertexBuffer::SetSubData(const void* data, uint32_t size, uint32_t offset) {
    ENGINE_CORE_TRACE("VertexBuffer ID {} SetSubData ({} bytes at offset {})", m_RendererID, size, offset);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
}

void VertexBuffer::Orphan() {
    ENGINE_CORE_TRACE("VertexBuffer ID {} Orphan (capacity: {} bytes)", m_RendererID, m_Capacity);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ARRAY_BUFFER, m_Capacity, nullptr, GL_DYNAMIC_DRAW);
}

IndexBuffer::IndexBuffer(const uint32_t* indices, uint32_t count) : m_Count(count) {
    glGenBuffers(1, &m_RendererID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    ENGINE_CORE_DEBUG("IndexBuffer created with ID: {} (count: {})", m_RendererID, count);
}

IndexBuffer::IndexBuffer(uint32_t* indices, uint32_t count)
    : IndexBuffer(static_cast<const uint32_t*>(indices), count) {}

IndexBuffer::IndexBuffer(std::span<const uint32_t> indices)
    : IndexBuffer(indices.data(), static_cast<uint32_t>(indices.size())) {}

IndexBuffer::~IndexBuffer() {
    if (m_RendererID != 0) {
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
        if (m_RendererID != 0) {
            glDeleteBuffers(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Count = std::exchange(other.m_Count, 0);
    }
    return *this;
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
