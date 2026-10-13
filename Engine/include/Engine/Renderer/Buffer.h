#ifndef RENDERER_BUFFER_H
#define RENDERER_BUFFER_H
#include <cstdint>

namespace Engine {
class VertexBuffer {
public:
    VertexBuffer(float* vertices, uint32_t size);
    ~VertexBuffer();

    void Bind() const;
    void Unbind() const;

private:
    uint32_t m_RendererID;
};

class IndexBuffer {
public:
    IndexBuffer(uint32_t* indices, uint32_t count);
    ~IndexBuffer();

    void Bind() const;
    void Unbind() const;
    uint32_t GetCount() const { return m_Count; }

private:
    uint32_t m_RendererID;
    uint32_t m_Count;
};
} // namespace Engine

#endif // RENDERER_BUFFER_H
