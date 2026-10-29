#include "Engine/Renderer/VertexArray.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <utility>

namespace Engine {

static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float:
        case ShaderDataType::Float2:
        case ShaderDataType::Float3:
        case ShaderDataType::Float4:
        case ShaderDataType::Mat3:
        case ShaderDataType::Mat4:   return GL_FLOAT;
        case ShaderDataType::Int:
        case ShaderDataType::Int2:
        case ShaderDataType::Int3:
        case ShaderDataType::Int4:   return GL_INT;
        case ShaderDataType::Bool:   return GL_BOOL;
        case ShaderDataType::None:   break;
    }
    return 0;
}

VertexArray::VertexArray() {
    glGenVertexArrays(1, &m_RendererID);
    ENGINE_CORE_DEBUG("VertexArray created with ID: {}", m_RendererID);
}

VertexArray::~VertexArray() {
    if (m_RendererID != 0) {
        ENGINE_CORE_DEBUG("VertexArray destroyed with ID: {}", m_RendererID);
        glDeleteVertexArrays(1, &m_RendererID);
        m_RendererID = 0;
    }
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_VertexBuffers(std::move(other.m_VertexBuffers)),
      m_IndexBuffer(std::move(other.m_IndexBuffer)) {
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0) {
            glDeleteVertexArrays(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_VertexBuffers = std::move(other.m_VertexBuffers);
        m_IndexBuffer = std::move(other.m_IndexBuffer);
    }
    return *this;
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

    const auto& layout = vertexBuffer->GetLayout();
    if (!layout.GetElements().empty()) {
        uint32_t index = 0;
        for (const auto& element : layout) {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(
                index,
                static_cast<GLint>(element.GetComponentCount()),
                ShaderDataTypeToOpenGLBaseType(element.Type),
                element.Normalized ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(layout.GetStride()),
                reinterpret_cast<const void*>(element.Offset)
            );
            index++;
        }
    } else {
        // Default layout fallback: Position (Float3) + TexCoord (Float2)
        uint32_t stride = 5 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
    }

    m_VertexBuffers.push_back(vertexBuffer);
}

void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
    ENGINE_CORE_DEBUG("VertexArray ID {}: Setting IndexBuffer (count: {})", m_RendererID, indexBuffer ? indexBuffer->GetCount() : 0);
    Bind();
    indexBuffer->Bind();
    m_IndexBuffer = indexBuffer;
}

} // namespace Engine
