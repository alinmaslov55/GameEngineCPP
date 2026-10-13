#ifndef RENDERER_VERTEXARRAY_H
#define RENDERER_VERTEXARRAY_H
#include <memory>
#include "Engine/Renderer/Buffer.h"

namespace Engine {
class VertexArray {
public:
    VertexArray();
    ~VertexArray();

    void Bind() const;
    void Unbind() const;

    void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer);
    void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer);

    const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }

private:
    uint32_t m_RendererID;
    std::shared_ptr<IndexBuffer> m_IndexBuffer;
};
} // namespace Engine

#endif // RENDERE_VERTEXARRAY_H