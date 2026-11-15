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
    if (glad_glGenVertexArrays != nullptr) {
        glGenVertexArrays(1, &m_RendererID);
        ENGINE_CORE_DEBUG("VertexArray created with ID: {}", m_RendererID);
    } else {
        ENGINE_CORE_DEBUG("VertexArray created in headless mode (no OpenGL context)");
    }
}

VertexArray::~VertexArray() {
    if (m_RendererID != 0 && glad_glDeleteVertexArrays != nullptr) {
        ENGINE_CORE_DEBUG("VertexArray destroyed with ID: {}", m_RendererID);
        glDeleteVertexArrays(1, &m_RendererID);
        m_RendererID = 0;
    }
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_VertexBufferIndex(std::exchange(other.m_VertexBufferIndex, 0)),
      m_VertexBuffers(std::move(other.m_VertexBuffers)),
      m_IndexBuffer(std::move(other.m_IndexBuffer)) {
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteVertexArrays != nullptr) {
            glDeleteVertexArrays(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_VertexBufferIndex = std::exchange(other.m_VertexBufferIndex, 0);
        m_VertexBuffers = std::move(other.m_VertexBuffers);
        m_IndexBuffer = std::move(other.m_IndexBuffer);
    }
    return *this;
}

void VertexArray::Bind() const {
    if (m_RendererID == 0 || glad_glBindVertexArray == nullptr) {
        return;
    }
    glBindVertexArray(m_RendererID);
}

void VertexArray::Unbind() const {
    if (glad_glBindVertexArray == nullptr) {
        return;
    }
    glBindVertexArray(0);
}

void VertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) {
    if (!vertexBuffer) {
        ENGINE_CORE_WARN("VertexArray ID {}: Ignored null VertexBuffer", m_RendererID);
        return;
    }

    ENGINE_CORE_DEBUG("VertexArray ID {}: Adding VertexBuffer", m_RendererID);
    const bool hasGL = (m_RendererID != 0) &&
                       (glad_glBindVertexArray != nullptr) &&
                       (glad_glEnableVertexAttribArray != nullptr) &&
                       (glad_glVertexAttribPointer != nullptr);
    if (hasGL) {
        Bind();
        vertexBuffer->Bind();
    }

    const auto& layout = vertexBuffer->GetLayout();
    if (!layout.GetElements().empty()) {
        for (const auto& element : layout) {
            switch (element.Type) {
                case ShaderDataType::Float:
                case ShaderDataType::Float2:
                case ShaderDataType::Float3:
                case ShaderDataType::Float4: {
                    if (hasGL) {
                        glEnableVertexAttribArray(m_VertexBufferIndex);
                        glVertexAttribPointer(
                            m_VertexBufferIndex,
                            static_cast<GLint>(element.GetComponentCount()),
                            ShaderDataTypeToOpenGLBaseType(element.Type),
                            element.Normalized ? GL_TRUE : GL_FALSE,
                            static_cast<GLsizei>(layout.GetStride()),
                            reinterpret_cast<const void*>(element.Offset)
                        );
                    }
                    ++m_VertexBufferIndex;
                    break;
                }
                case ShaderDataType::Int:
                case ShaderDataType::Int2:
                case ShaderDataType::Int3:
                case ShaderDataType::Int4:
                case ShaderDataType::Bool: {
                    if (hasGL && glad_glVertexAttribIPointer != nullptr) {
                        glEnableVertexAttribArray(m_VertexBufferIndex);
                        glVertexAttribIPointer(
                            m_VertexBufferIndex,
                            static_cast<GLint>(element.GetComponentCount()),
                            ShaderDataTypeToOpenGLBaseType(element.Type),
                            static_cast<GLsizei>(layout.GetStride()),
                            reinterpret_cast<const void*>(element.Offset)
                        );
                    }
                    ++m_VertexBufferIndex;
                    break;
                }
                case ShaderDataType::Mat3:
                case ShaderDataType::Mat4: {
                    const uint32_t count = element.GetComponentCount();
                    for (uint32_t col = 0; col < count; ++col) {
                        if (hasGL) {
                            glEnableVertexAttribArray(m_VertexBufferIndex);
                            glVertexAttribPointer(
                                m_VertexBufferIndex,
                                static_cast<GLint>(count),
                                ShaderDataTypeToOpenGLBaseType(element.Type),
                                element.Normalized ? GL_TRUE : GL_FALSE,
                                static_cast<GLsizei>(layout.GetStride()),
                                reinterpret_cast<const void*>(element.Offset + sizeof(float) * count * col)
                            );
                        }
                        ++m_VertexBufferIndex;
                    }
                    break;
                }
                default:
                    ENGINE_CORE_WARN("VertexArray: Unknown ShaderDataType in BufferLayout");
                    break;
            }
        }
    } else {
        // Default layout fallback: Position (Float3) + TexCoord (Float2)
        if (hasGL) {
            constexpr uint32_t stride = 5 * sizeof(float);
            glEnableVertexAttribArray(m_VertexBufferIndex);
            glVertexAttribPointer(m_VertexBufferIndex, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
            glEnableVertexAttribArray(m_VertexBufferIndex + 1);
            glVertexAttribPointer(m_VertexBufferIndex + 1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
        }
        m_VertexBufferIndex += 2;
    }

    m_VertexBuffers.push_back(vertexBuffer);
}

void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
    if (!indexBuffer) {
        ENGINE_CORE_WARN("VertexArray ID {}: Clearing IndexBuffer (null passed)", m_RendererID);
        if (m_RendererID != 0 && glad_glBindVertexArray != nullptr) {
            Bind();
            if (glad_glBindBuffer != nullptr) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            }
        }
        m_IndexBuffer.reset();
        return;
    }
    ENGINE_CORE_DEBUG("VertexArray ID {}: Setting IndexBuffer (count: {})", m_RendererID, indexBuffer->GetCount());
    if (m_RendererID != 0 && glad_glBindVertexArray != nullptr) {
        Bind();
        indexBuffer->Bind();
    }
    m_IndexBuffer = indexBuffer;
}

} // namespace Engine
