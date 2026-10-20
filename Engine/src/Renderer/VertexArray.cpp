#include "Engine/Renderer/VertexArray.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

namespace Engine {
VertexArray::VertexArray() {
    glGenVertexArrays(1, &m_RendererID);
    ENGINE_CORE_DEBUG("VertexArray created with ID: {}", m_RendererID);
}

VertexArray::~VertexArray() {
    ENGINE_CORE_DEBUG("VertexArray destroyed with ID: {}", m_RendererID);
    glDeleteVertexArrays(1, &m_RendererID);
}

void VertexArray::Bind() const {
    ENGINE_CORE_TRACE("VertexArray ID {} bound", m_RendererID);
    glBindVertexArray(m_RendererID);
}

void VertexArray::Unbind() const {
    ENGINE_CORE_TRACE("VertexArray ID {} unbound", m_RendererID);
    glBindVertexArray(0);
}

void VertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) {
    ENGINE_CORE_DEBUG("VertexArray ID {}: Adding VertexBuffer", m_RendererID);
    Bind();
    vertexBuffer->Bind();
    
    uint32_t stride = 5 * sizeof(float);

    // Attribute 0: Position (X, Y, Z)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);

    // Attribute 1: Texture Coordinates (U, V)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));

    m_VertexBuffers.push_back(vertexBuffer);
}

void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
    ENGINE_CORE_DEBUG("VertexArray ID {}: Setting IndexBuffer (count: {})", m_RendererID, indexBuffer ? indexBuffer->GetCount() : 0);
    Bind();
    indexBuffer->Bind();
    m_IndexBuffer = indexBuffer;
}
} // namespace Engine
