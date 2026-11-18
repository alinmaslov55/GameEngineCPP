#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Renderer/GLUtils.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstring>
#include <utility>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace Engine {

namespace {

bool HasActiveGLArrayContext() noexcept {
    return glad_glGenTextures != nullptr &&
           glad_glBindTexture != nullptr &&
           glad_glTexImage3D != nullptr &&
           glad_glTexSubImage3D != nullptr;
}

bool SupportsAnisotropyExtension() noexcept {
    if (glad_glGetIntegerv == nullptr || glad_glGetStringi == nullptr) {
        return false;
    }
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    for (GLint i = 0; i < numExtensions; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!ext) {
            continue;
        }
        if (std::strcmp(ext, "GL_EXT_texture_filter_anisotropic") == 0 ||
            std::strcmp(ext, "GL_ARB_texture_filter_anisotropic") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

Texture2DArray::Texture2DArray(uint32_t width,
                               uint32_t height,
                               uint32_t layers,
                               bool linearFilter,
                               bool repeatWrap)
    : m_Width(std::max(1u, width)),
      m_Height(std::max(1u, height)),
      m_Layers(std::max(1u, layers)),
      m_LinearFilter(linearFilter),
      m_RepeatWrap(repeatWrap),
      m_UploadedLayers(m_Layers, 0u) {
    if (!HasActiveGLArrayContext()) {
        ENGINE_CORE_DEBUG("Texture2DArray ({}x{}x{}) created in headless mode (no GL context)",
                          m_Width, m_Height, m_Layers);
        return;
    }

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_RendererID);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                    m_LinearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER,
                    m_LinearFilter ? GL_LINEAR : GL_NEAREST);

    const GLint wrapMode = m_RepeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, wrapMode);

    if (m_LinearFilter && SupportsAnisotropyExtension()) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        m_AppliedAnisotropy = std::clamp(maxAniso, 1.0f, 8.0f);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_AppliedAnisotropy);
    }

    {
        ScopedUnpackAlignment unpackAlign(1);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,
                     0,
                     GL_RGBA8,
                     static_cast<GLsizei>(m_Width),
                     static_cast<GLsizei>(m_Height),
                     static_cast<GLsizei>(m_Layers),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
    }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    ENGINE_CORE_INFO("Texture2DArray allocated: ID={}, {}x{} x {} layers (Anisotropy={:.1f}x)",
                     m_RendererID, m_Width, m_Height, m_Layers, m_AppliedAnisotropy);
}

Texture2DArray::~Texture2DArray() {
    if (m_RendererID != 0 && glad_glDeleteTextures != nullptr) {
        glDeleteTextures(1, &m_RendererID);
        m_RendererID = 0;
    }
}

Texture2DArray::Texture2DArray(Texture2DArray&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_Width(std::exchange(other.m_Width, 0)),
      m_Height(std::exchange(other.m_Height, 0)),
      m_Layers(std::exchange(other.m_Layers, 0)),
      m_LinearFilter(other.m_LinearFilter),
      m_RepeatWrap(other.m_RepeatWrap),
      m_AppliedAnisotropy(std::exchange(other.m_AppliedAnisotropy, 0.0f)),
      m_UploadedLayers(std::move(other.m_UploadedLayers)) {
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_Width = std::exchange(other.m_Width, 0);
        m_Height = std::exchange(other.m_Height, 0);
        m_Layers = std::exchange(other.m_Layers, 0);
        m_LinearFilter = other.m_LinearFilter;
        m_RepeatWrap = other.m_RepeatWrap;
        m_AppliedAnisotropy = std::exchange(other.m_AppliedAnisotropy, 0.0f);
        m_UploadedLayers = std::move(other.m_UploadedLayers);
    }
    return *this;
}

void Texture2DArray::Bind(uint32_t slot) const {
    if (m_RendererID == 0 || !HasActiveGLArrayContext()) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_RendererID);
}

void Texture2DArray::Unbind() const {
    if (HasActiveGLArrayContext()) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
}

void Texture2DArray::SetLayerData(uint32_t layerIndex, const void* rgba8Data, bool generateMips) {
    if (layerIndex >= m_Layers || layerIndex >= m_UploadedLayers.size() || !rgba8Data) {
        ENGINE_CORE_WARN("Texture2DArray::SetLayerData ignored invalid arguments (layer={}/{}, data={})",
                         layerIndex, m_Layers, rgba8Data ? "valid" : "null");
        return;
    }
    m_UploadedLayers[layerIndex] = 1u;
    if (m_RendererID == 0 || !HasActiveGLArrayContext()) {
        ENGINE_CORE_DEBUG("Texture2DArray::SetLayerData marked CPU layer {} without GL upload (id={})",
                          layerIndex, m_RendererID);
        return;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, m_RendererID);
    {
        ScopedUnpackAlignment unpackAlign(1);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                        0,
                        0,
                        0,
                        static_cast<GLint>(layerIndex),
                        static_cast<GLsizei>(m_Width),
                        static_cast<GLsizei>(m_Height),
                        1,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        rgba8Data);
    }
    if (generateMips && m_LinearFilter) {
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Texture2DArray::GenerateMipmaps() {
    if (m_RendererID == 0 || !m_LinearFilter || !HasActiveGLArrayContext()) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_RendererID);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Texture2DArray::ResizeRGBA8Bilinear(const uint8_t* src,
                                         uint32_t srcW,
                                         uint32_t srcH,
                                         uint32_t dstW,
                                         uint32_t dstH,
                                         std::vector<uint8_t>& outRgba8) {
    TerrainTexturePacker::ResizeRGBA8Bilinear(src, srcW, srcH, dstW, dstH, outRgba8);
}

void Texture2DArray::ResizeR8Bilinear(const uint8_t* src,
                                      uint32_t srcW,
                                      uint32_t srcH,
                                      uint32_t dstW,
                                      uint32_t dstH,
                                      std::vector<uint8_t>& outR8) {
    TerrainTexturePacker::ResizeR8Bilinear(src, srcW, srcH, dstW, dstH, outR8);
}

bool Texture2DArray::PackAlbedoHeightRGBA8(const uint8_t* diffusePixels,
                                           int diffuseChannels,
                                           uint32_t diffuseW,
                                           uint32_t diffuseH,
                                           const uint8_t* heightPixels,
                                           int heightChannels,
                                           uint32_t heightW,
                                           uint32_t heightH,
                                           uint32_t targetW,
                                           uint32_t targetH,
                                           std::vector<uint8_t>& outRgba8,
                                           int fallbackHeight) {
    return TerrainTexturePacker::PackAlbedoHeightRGBA8(diffusePixels, diffuseChannels, diffuseW, diffuseH,
                                                       heightPixels, heightChannels, heightW, heightH,
                                                       targetW, targetH, outRgba8, fallbackHeight);
}

bool Texture2DArray::PackNormalGlossRGBA8(const uint8_t* normalPixels,
                                          int normalChannels,
                                          uint32_t normalW,
                                          uint32_t normalH,
                                          float defaultGlossiness,
                                          const uint8_t* glossPixels,
                                          int glossChannels,
                                          uint32_t glossW,
                                          uint32_t glossH,
                                          uint32_t targetW,
                                          uint32_t targetH,
                                          std::vector<uint8_t>& outRgba8) {
    return TerrainTexturePacker::PackNormalGlossRGBA8(normalPixels, normalChannels, normalW, normalH,
                                                      defaultGlossiness, glossPixels, glossChannels, glossW, glossH,
                                                      targetW, targetH, outRgba8);
}

ResolvedTerrainMaps Texture2DArray::ResolveTerrainMapCompanions(const std::string& diffusePath,
                                                                const std::string& explicitHeightPath,
                                                                const std::string& explicitNormalPath) {
    return TerrainTexturePacker::ResolveTerrainMapCompanions(diffusePath, explicitHeightPath, explicitNormalPath);
}

bool Texture2DArray::BuildAlbedoHeightSliceFromFiles(const std::string& diffusePath,
                                                     const std::string& heightPath,
                                                     uint32_t targetW,
                                                     uint32_t targetH,
                                                     const glm::u8vec4& fallbackRgba,
                                                     std::vector<uint8_t>& outRgba8) {
    return TerrainTexturePacker::BuildAlbedoHeightSliceFromFiles(diffusePath, heightPath, targetW, targetH,
                                                                 fallbackRgba, outRgba8);
}

bool Texture2DArray::BuildNormalGlossSliceFromFiles(const std::string& normalPath,
                                                    float glossiness,
                                                    uint32_t targetW,
                                                    uint32_t targetH,
                                                    std::vector<uint8_t>& outRgba8) {
    return TerrainTexturePacker::BuildNormalGlossSliceFromFiles(normalPath, glossiness, targetW, targetH, outRgba8);
}

} // namespace Engine
