#ifndef RENDERER_INDEXBUFFER_H
#define RENDERER_INDEXBUFFER_H

#include <cstdint>
#include <span>

namespace Engine {

class IndexBuffer {
public:
    IndexBuffer(const uint32_t* indices, uint32_t count);
    IndexBuffer(uint32_t* indices, uint32_t count);
    IndexBuffer(std::span<const uint32_t> indices);
    ~IndexBuffer();

    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;

    IndexBuffer(IndexBuffer&& other) noexcept;
    IndexBuffer& operator=(IndexBuffer&& other) noexcept;

    void Bind() const;
    void Unbind() const;
    [[nodiscard]] uint32_t GetCount() const noexcept { return m_Count; }

private:
    uint32_t m_RendererID = 0;
    uint32_t m_Count = 0;
};

} // namespace Engine

#endif // RENDERER_INDEXBUFFER_H
