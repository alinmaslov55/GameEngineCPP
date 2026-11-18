#include "Engine/Renderer/RHI/IndexBuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

IndexBuffer::IndexBuffer(const uint32_t* indices, uint32_t count) : m_Count(count) {
    if (glad_glGenBuffers == nullptr) {
        return;
    }
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
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

} // namespace Engine
