#include "Engine/Renderer/Framebuffer.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <utility>

namespace Engine {

Framebuffer::Framebuffer(const FramebufferSpecification& spec)
    : m_Specification(spec) {
    Invalidate();
}

Framebuffer::~Framebuffer() {
    Release();
}

void Framebuffer::Release() {
    if (m_RendererID != 0) {
        ENGINE_CORE_DEBUG("Framebuffer destroyed with ID: {}", m_RendererID);
        glDeleteFramebuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
    if (m_ColorAttachment != 0) {
        glDeleteTextures(1, &m_ColorAttachment);
        m_ColorAttachment = 0;
    }
    if (m_DepthAttachment != 0) {
        glDeleteTextures(1, &m_DepthAttachment);
        m_DepthAttachment = 0;
    }
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_ColorAttachment(std::exchange(other.m_ColorAttachment, 0)),
      m_DepthAttachment(std::exchange(other.m_DepthAttachment, 0)),
      m_Specification(other.m_Specification) {
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        Release();
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_ColorAttachment = std::exchange(other.m_ColorAttachment, 0);
        m_DepthAttachment = std::exchange(other.m_DepthAttachment, 0);
        m_Specification = other.m_Specification;
    }
    return *this;
}

void Framebuffer::Invalidate() {
    Release();

    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // Color Attachment
    glGenTextures(1, &m_ColorAttachment);
    glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

    // Depth/Stencil Attachment
    glGenTextures(1, &m_DepthAttachment);
    glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_CORE_ERROR("Framebuffer is incomplete! Status: {:#x}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    } else {
        ENGINE_CORE_INFO("Framebuffer initialized (ID: {}, {}x{})", m_RendererID, m_Specification.Width, m_Specification.Height);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
    ENGINE_CORE_TRACE("Framebuffer ID {} bound ({}x{})", m_RendererID, m_Specification.Width, m_Specification.Height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
    glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

void Framebuffer::Unbind() {
    ENGINE_CORE_TRACE("Framebuffer ID {} unbound", m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        ENGINE_CORE_WARN("Attempted to resize Framebuffer ID {} to invalid dimensions: {}x{}", m_RendererID, width, height);
        return;
    }
    if (m_Specification.Width == width && m_Specification.Height == height)
        return;
    
    ENGINE_CORE_DEBUG("Resizing Framebuffer ID {} to {}x{}", m_RendererID, width, height);
    m_Specification.Width = width;
    m_Specification.Height = height;
    Invalidate();
}

} // namespace Engine
