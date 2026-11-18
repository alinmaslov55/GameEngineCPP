#ifndef RENDERER_VERTEXBUFFER_H
#define RENDERER_VERTEXBUFFER_H

#include "Engine/Renderer/RHI/BufferLayout.h"

#include <cstdint>
#include <span>

namespace Engine {

class VertexBuffer {
public:
    VertexBuffer(uint32_t size);
    VertexBuffer(const float* vertices, uint32_t size);
    VertexBuffer(float* vertices, uint32_t size);
    VertexBuffer(std::span<const float> vertices);
    ~VertexBuffer();

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VertexBuffer(VertexBuffer&& other) noexcept;
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    void SetData(const void* data, uint32_t size);
    void SetSubData(const void* data, uint32_t size, uint32_t offset = 0);
    void Orphan();

    template<typename T>
    void SetData(std::span<const T> data) {
        SetData(data.data(), static_cast<uint32_t>(data.size_bytes()));
    }

    template<typename T>
    void SetSubData(std::span<const T> data, uint32_t offset = 0) {
        SetSubData(data.data(), static_cast<uint32_t>(data.size_bytes()), offset);
    }

    [[nodiscard]] uint32_t GetCapacity() const noexcept { return m_Capacity; }

    [[nodiscard]] const BufferLayout& GetLayout() const noexcept { return m_Layout; }
    void SetLayout(const BufferLayout& layout) { m_Layout = layout; }

private:
    uint32_t m_RendererID{0};
    uint32_t m_Capacity{0};
    BufferLayout m_Layout;
};

} // namespace Engine

#endif // RENDERER_VERTEXBUFFER_H
