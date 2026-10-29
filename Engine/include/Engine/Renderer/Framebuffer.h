#ifndef RENDERER_FRAMEBUFFER_H
#define RENDERER_FRAMEBUFFER_H
#include <cstdint>

namespace Engine {
struct FramebufferSpecification {
    uint32_t Width = 0;
    uint32_t Height = 0;
};

class Framebuffer {
public:
    Framebuffer(const FramebufferSpecification& spec);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    void Invalidate();
    void Bind();
    void Unbind();
    void Resize(uint32_t width, uint32_t height);

    uint32_t GetColorAttachmentRendererID() const { return m_ColorAttachment; }
    const FramebufferSpecification& GetSpecification() const { return m_Specification; }

private:
    void Release();

    uint32_t m_RendererID = 0;
    uint32_t m_ColorAttachment = 0;
    uint32_t m_DepthAttachment = 0;
    FramebufferSpecification m_Specification;
};
} // namespace Engine

#endif // RENDERER_FRAMEBUFFER_H
