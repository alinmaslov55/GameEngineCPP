#ifndef RENDERER_UNIFORMBUFFER_H
#define RENDERER_UNIFORMBUFFER_H

#include <cstdint>

namespace Engine {

class UniformBuffer {
public:
    UniformBuffer(uint32_t size, uint32_t bindingPoint);
    ~UniformBuffer();

    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;

    UniformBuffer(UniformBuffer&& other) noexcept;
    UniformBuffer& operator=(UniformBuffer&& other) noexcept;

    void SetData(const void* data, uint32_t size, uint32_t offset = 0) const;

    [[nodiscard]] uint32_t GetRendererID() const noexcept { return m_RendererID; }
    [[nodiscard]] uint32_t GetSize() const noexcept { return m_Size; }
    [[nodiscard]] uint32_t GetBindingPoint() const noexcept { return m_BindingPoint; }

private:
    uint32_t m_RendererID = 0;
    uint32_t m_Size = 0;
    uint32_t m_BindingPoint = 0;
};

} // namespace Engine

#endif // RENDERER_UNIFORMBUFFER_H
