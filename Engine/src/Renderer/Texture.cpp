#include "Engine/Renderer/Texture.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <stb_image.h>

namespace Engine {
Texture::Texture(const std::string& path)
    : m_RendererID(0), m_Filepath(path), m_Width(0), m_Height(0), m_BPP(0) {

    ENGINE_CORE_INFO("Loading texture from: '{}'", path);
    stbi_set_flip_vertically_on_load(1);

    unsigned char* localBuffer = stbi_load(path.c_str(), &m_Width, &m_Height, &m_BPP, 4);

    if (!localBuffer) {
        ENGINE_CORE_ERROR("Failed to load texture: '{}'", path);
        return;
    }

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);

    // Keep small pixel-art atlases (< 64px, e.g. font_atlas.png) crisp with GL_NEAREST,
    // and use GL_LINEAR for high-resolution illustrated 2.5D diorama sprites & ground tiles.
    const bool useLinear = (m_Width >= 64 && m_Height >= 64);
    const GLint minFilter = useLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST;
    const GLint magFilter = useLinear ? GL_LINEAR : GL_NEAREST;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, localBuffer);
    if (useLinear) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(localBuffer);

    ENGINE_CORE_INFO("Texture loaded successfully: '{}' (ID: {}, {}x{}, BPP: {})", path, m_RendererID, m_Width, m_Height, m_BPP);
}

Texture::~Texture() {
    ENGINE_CORE_DEBUG("Texture destroyed with ID: {}", m_RendererID);
    if (m_RendererID != 0) {
        glDeleteTextures(1, &m_RendererID);
    }
}

void Texture::SetFilterLinear(bool linear) {
    if (m_RendererID == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind(uint32_t slot) const {
    ENGINE_CORE_TRACE("Texture ID {} bound to slot {}", m_RendererID, slot);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture::Unbind() const {
    ENGINE_CORE_TRACE("Texture ID {} unbound", m_RendererID);
    glBindTexture(GL_TEXTURE_2D, 0);
}
} // namespace Engine
