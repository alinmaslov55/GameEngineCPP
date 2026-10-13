#include "Engine/Renderer/VertexArray.h"
#include <glad/glad.h>

namespace Engine {
VertexArray::VertexArray() { glGenVertexArrays(1, &m_RendererID); }
VertexArray::~VertexArray() { glDeleteVertexArrays(1, &m_RendererID); }
void VertexArray::Bind() const { glBindVertexArray(m_RendererID); }
void VertexArray::Unbind() const { glBindVertexArray(0); }

void VertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) {
    Bind();
    vertexBuffer->Bind();
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
}

void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
    Bind();
    indexBuffer->Bind();
    m_IndexBuffer = indexBuffer;
}
} // namespace Engine