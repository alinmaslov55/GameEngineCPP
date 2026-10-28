#include "Engine/Renderer/Texture.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <stb_image.h>

namespace Engine {

namespace {
bool HasActiveGLContext() {
    return glad_glGenTextures != nullptr && glad_glBindTexture != nullptr;
}
} // namespace

Texture::Texture(const std::string& path)
    : m_RendererID(0), m_Filepath(path), m_Width(0), m_Height(0), m_BPP(0) {

    ENGINE_CORE_INFO("Loading texture from: '{}'", path);
    stbi_set_flip_vertically_on_load(1);

    unsigned char* localBuffer = stbi_load(path.c_str(), &m_Width, &m_Height, &m_BPP, 4);

    if (!localBuffer) {
        ENGINE_CORE_ERROR("Failed to load texture: '{}'", path);
        return;
    }

    m_LinearFilter = (m_Width >= 64 && m_Height >= 64);
    m_HasMipmaps = m_LinearFilter;
    m_RepeatWrap = false;

    if (!HasActiveGLContext()) {
        stbi_image_free(localBuffer);
        ENGINE_CORE_DEBUG("Texture '{}': Loaded metadata ({}x{}) in headless mode (no GL context)", path, m_Width, m_Height);
        return;
    }

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);

    // Keep small pixel-art atlases (< 64px, e.g. font_atlas.png) crisp with GL_NEAREST,
    // and use GL_LINEAR for high-resolution illustrated 2.5D diorama sprites & ground tiles.
    const GLint minFilter = m_LinearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST;
    const GLint magFilter = m_LinearFilter ? GL_LINEAR : GL_NEAREST;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, localBuffer);
    if (m_HasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(localBuffer);

    ENGINE_CORE_INFO("Texture loaded successfully: '{}' (ID: {}, {}x{}, BPP: {})", path, m_RendererID, m_Width, m_Height, m_BPP);
}

Texture::Texture(uint32_t width,
                 uint32_t height,
                 const void* rgbaData,
                 bool linearFilter,
                 bool repeatWrap)
    : m_RendererID(0),
      m_Filepath(""),
      m_Width(static_cast<int>(width)),
      m_Height(static_cast<int>(height)),
      m_BPP(4),
      m_LinearFilter(linearFilter),
      m_HasMipmaps(false),
      m_RepeatWrap(repeatWrap) {
    if (width == 0 || height == 0 || !HasActiveGLContext()) {
        return;
    }

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);

    const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
    const GLint wrap = repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture() {
    ENGINE_CORE_DEBUG("Texture destroyed with ID: {}", m_RendererID);
    if (m_RendererID != 0 && glad_glDeleteTextures != nullptr) {
        glDeleteTextures(1, &m_RendererID);
    }
}

void Texture::SetFilterLinear(bool linear) {
    m_LinearFilter = linear;
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    const GLint minFilter = linear
        ? (m_HasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR)
        : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetWrapRepeat(bool repeat) {
    m_RepeatWrap = repeat;
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    const GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetData(const void* rgbaData, uint32_t sizeBytes) {
    const uint32_t expectedBytes = static_cast<uint32_t>(m_Width) * static_cast<uint32_t>(m_Height) * 4u;
    if (!rgbaData || sizeBytes < expectedBytes) {
        ENGINE_CORE_ERROR("Texture::SetData size mismatch (got {} bytes, expected {} bytes)", sizeBytes, expectedBytes);
        return;
    }
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    if (m_HasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetSubData(uint32_t xOffset,
                         uint32_t yOffset,
                         uint32_t width,
                         uint32_t height,
                         const void* rgbaSubData) {
    if (!rgbaSubData || width == 0 || height == 0) {
        return;
    }
    if (static_cast<int>(xOffset + width) > m_Width || static_cast<int>(yOffset + height) > m_Height) {
        ENGINE_CORE_ERROR("Texture::SetSubData region ({}, {}, {}x{}) exceeds texture bounds ({}x{})",
                          xOffset, yOffset, width, height, m_Width, m_Height);
        return;
    }
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    static_cast<GLint>(xOffset),
                    static_cast<GLint>(yOffset),
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height),
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    rgbaSubData);
    if (m_HasMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind(uint32_t slot) const {
    if (m_RendererID == 0 || !HasActiveGLContext()) {
        return;
    }
    ENGINE_CORE_TRACE("Texture ID {} bound to slot {}", m_RendererID, slot);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture::Unbind() const {
    if (!HasActiveGLContext()) {
        return;
    }
    ENGINE_CORE_TRACE("Texture ID {} unbound", m_RendererID);
    glBindTexture(GL_TEXTURE_2D, 0);
}
} // namespace Engine
