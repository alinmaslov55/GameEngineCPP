#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
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

struct StbiDeleter {
    void operator()(unsigned char* ptr) const noexcept {
        if (ptr) {
            stbi_image_free(ptr);
        }
    }
};
using StbiPtr = std::unique_ptr<unsigned char, StbiDeleter>;

std::vector<uint8_t> ConvertToTightRGBA8(const uint8_t* pixels,
                                         int channels,
                                         uint32_t width,
                                         uint32_t height,
                                         bool alphaOpaqueDefault) {
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgba(pixelCount * 4u, 255u);
    const int safeChannels = std::clamp(channels, 1, 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t srcIdx = i * static_cast<size_t>(safeChannels);
        const size_t dstIdx = i * 4u;
        if (safeChannels == 1) {
            const uint8_t v = pixels[srcIdx];
            rgba[dstIdx + 0] = v;
            rgba[dstIdx + 1] = v;
            rgba[dstIdx + 2] = v;
            rgba[dstIdx + 3] = alphaOpaqueDefault ? 255u : v;
        } else if (safeChannels == 2) {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 0];
            rgba[dstIdx + 2] = pixels[srcIdx + 0];
            rgba[dstIdx + 3] = pixels[srcIdx + 1];
        } else if (safeChannels == 3) {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 1];
            rgba[dstIdx + 2] = pixels[srcIdx + 2];
            rgba[dstIdx + 3] = 255u;
        } else {
            rgba[dstIdx + 0] = pixels[srcIdx + 0];
            rgba[dstIdx + 1] = pixels[srcIdx + 1];
            rgba[dstIdx + 2] = pixels[srcIdx + 2];
            rgba[dstIdx + 3] = pixels[srcIdx + 3];
        }
    }
    return rgba;
}

uint8_t ExtractLuminance(const uint8_t* pixel, int channels) noexcept {
    if (channels <= 2) {
        return pixel[0];
    }
    const float lum = 0.299f * static_cast<float>(pixel[0]) +
                      0.587f * static_cast<float>(pixel[1]) +
                      0.114f * static_cast<float>(pixel[2]);
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(lum)), 0, 255));
}

bool FileExistsResolved(const std::string& rawOrResolvedPath, std::string& outResolved) {
    if (rawOrResolvedPath.empty()) {
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(rawOrResolvedPath, ec) && std::filesystem::is_regular_file(rawOrResolvedPath, ec)) {
        outResolved = rawOrResolvedPath;
        return true;
    }
    const std::string resolved = ResourceManager::ResolvePath(rawOrResolvedPath);
    if (std::filesystem::exists(resolved, ec) && std::filesystem::is_regular_file(resolved, ec)) {
        outResolved = resolved;
        return true;
    }
    return false;
}

std::string ProbeCompanionSuffix(const std::filesystem::path& diffusePath,
                                 const std::string& suffixTag) {
    const std::string ext = diffusePath.extension().string();
    const std::string stem = diffusePath.stem().string();
    const std::filesystem::path dir = diffusePath.parent_path();

    std::vector<std::string> candidates;
    if (stem.size() > 2u && stem.compare(stem.size() - 2u, 2u, "_d") == 0) {
        candidates.push_back(stem.substr(0, stem.size() - 2u) + suffixTag + ext);
    }
    if (stem.size() > 9u && stem.compare(stem.size() - 9u, 9u, "_seamless") == 0) {
        candidates.push_back(stem.substr(0, stem.size() - 9u) + suffixTag + ext);
    }
    candidates.push_back(stem + suffixTag + ext);

    for (const auto& name : candidates) {
        std::string resolved;
        const std::string candidate = (dir / name).string();
        if (FileExistsResolved(candidate, resolved)) {
            return resolved;
        }
    }
    return "";
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

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
    : m_RendererID(other.m_RendererID),
      m_Width(other.m_Width),
      m_Height(other.m_Height),
      m_Layers(other.m_Layers),
      m_LinearFilter(other.m_LinearFilter),
      m_RepeatWrap(other.m_RepeatWrap),
      m_AppliedAnisotropy(other.m_AppliedAnisotropy),
      m_UploadedLayers(std::move(other.m_UploadedLayers)) {
    other.m_RendererID = 0;
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0 && glad_glDeleteTextures != nullptr) {
            glDeleteTextures(1, &m_RendererID);
        }
        m_RendererID = other.m_RendererID;
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        m_Layers = other.m_Layers;
        m_LinearFilter = other.m_LinearFilter;
        m_RepeatWrap = other.m_RepeatWrap;
        m_AppliedAnisotropy = other.m_AppliedAnisotropy;
        m_UploadedLayers = std::move(other.m_UploadedLayers);
        other.m_RendererID = 0;
    }
    return *this;
}

void Texture2DArray::Bind(uint32_t slot) const {
    if (m_RendererID == 0 || !HasActiveGLArrayContext()) {
        ENGINE_CORE_TRACE("Texture2DArray::Bind skipped (id={}, activeGL={})",
                          m_RendererID, HasActiveGLArrayContext());
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
    if (layerIndex >= m_Layers || !rgba8Data) {
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
    if (!src || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        outRgba8.clear();
        return;
    }

    const size_t dstBytes = static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u;
    outRgba8.resize(dstBytes);

    if (srcW == dstW && srcH == dstH) {
        std::memcpy(outRgba8.data(), src, dstBytes);
        return;
    }

    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (uint32_t y = 0; y < dstH; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const float cy = std::clamp(fy, 0.0f, static_cast<float>(srcH - 1u));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(cy));
        const uint32_t y1 = std::min(srcH - 1u, y0 + 1u);
        const float wy = cy - static_cast<float>(y0);

        for (uint32_t x = 0; x < dstW; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const float cx = std::clamp(fx, 0.0f, static_cast<float>(srcW - 1u));
            const uint32_t x0 = static_cast<uint32_t>(std::floor(cx));
            const uint32_t x1 = std::min(srcW - 1u, x0 + 1u);
            const float wx = cx - static_cast<float>(x0);

            const size_t idx00 = (static_cast<size_t>(y0) * srcW + x0) * 4u;
            const size_t idx10 = (static_cast<size_t>(y0) * srcW + x1) * 4u;
            const size_t idx01 = (static_cast<size_t>(y1) * srcW + x0) * 4u;
            const size_t idx11 = (static_cast<size_t>(y1) * srcW + x1) * 4u;
            const size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4u;

            for (int c = 0; c < 4; ++c) {
                const float top = static_cast<float>(src[idx00 + c]) * (1.0f - wx) +
                                  static_cast<float>(src[idx10 + c]) * wx;
                const float bot = static_cast<float>(src[idx01 + c]) * (1.0f - wx) +
                                  static_cast<float>(src[idx11 + c]) * wx;
                const float val = top * (1.0f - wy) + bot * wy;
                outRgba8[dstIdx + c] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(val)), 0, 255));
            }
        }
    }
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
    if (!diffusePixels || diffuseW == 0 || diffuseH == 0 || targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("Texture2DArray::PackAlbedoHeightRGBA8 rejected invalid dimensions/input ({}x{} -> {}x{})",
                         diffuseW, diffuseH, targetW, targetH);
        return false;
    }

    const std::vector<uint8_t> srcRgba = ConvertToTightRGBA8(diffusePixels, diffuseChannels, diffuseW, diffuseH, true);
    ResizeRGBA8Bilinear(srcRgba.data(), diffuseW, diffuseH, targetW, targetH, outRgba8);

    const size_t pixelCount = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    if (heightPixels && heightW > 0 && heightH > 0) {
        const int safeHCh = std::clamp(heightChannels, 1, 4);
        std::vector<uint8_t> heightRgba(static_cast<size_t>(heightW) * static_cast<size_t>(heightH) * 4u);
        for (size_t i = 0; i < static_cast<size_t>(heightW) * static_cast<size_t>(heightH); ++i) {
            const uint8_t hByte = ExtractLuminance(&heightPixels[i * static_cast<size_t>(safeHCh)], safeHCh);
            heightRgba[i * 4u + 0] = hByte;
            heightRgba[i * 4u + 1] = hByte;
            heightRgba[i * 4u + 2] = hByte;
            heightRgba[i * 4u + 3] = hByte;
        }

        std::vector<uint8_t> resizedHeight;
        ResizeRGBA8Bilinear(heightRgba.data(), heightW, heightH, targetW, targetH, resizedHeight);
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = resizedHeight[i * 4u + 0];
        }
    } else if (fallbackHeight >= 0) {
        const uint8_t hConst = static_cast<uint8_t>(std::clamp(fallbackHeight, 0, 255));
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = hConst;
        }
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            outRgba8[i * 4u + 3] = ExtractLuminance(&outRgba8[i * 4u], 3);
        }
    }

    return true;
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
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("Texture2DArray::PackNormalGlossRGBA8 rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    const size_t targetPixels = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    const uint8_t defaultGlossByte = static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::lround(std::clamp(defaultGlossiness, 0.0f, 1.0f) * 255.0f)), 0, 255)
    );

    if (normalPixels && normalW > 0 && normalH > 0) {
        const std::vector<uint8_t> srcNormal = ConvertToTightRGBA8(normalPixels, normalChannels, normalW, normalH, true);
        ResizeRGBA8Bilinear(srcNormal.data(), normalW, normalH, targetW, targetH, outRgba8);

        // Ensure decoded tangent normals remain normalized and upward-facing (+Z tangent space)
        for (size_t i = 0; i < targetPixels; ++i) {
            const size_t idx = i * 4u;
            float nx = (static_cast<float>(outRgba8[idx + 0]) / 255.0f) * 2.0f - 1.0f;
            float ny = (static_cast<float>(outRgba8[idx + 1]) / 255.0f) * 2.0f - 1.0f;
            float nz = (static_cast<float>(outRgba8[idx + 2]) / 255.0f) * 2.0f - 1.0f;
            const float lenSq = nx * nx + ny * ny + nz * nz;
            if (lenSq < 1e-5f) {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            } else {
                const float invLen = 1.0f / std::sqrt(lenSq);
                nx *= invLen;
                ny *= invLen;
                nz = std::max(nz * invLen, 0.0f);
            }
            outRgba8[idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((nx * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((ny * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((nz * 0.5f + 0.5f) * 255.0f)), 0, 255));
            outRgba8[idx + 3] = defaultGlossByte;
        }
    } else {
        outRgba8.resize(targetPixels * 4u);
        for (size_t i = 0; i < targetPixels; ++i) {
            const size_t idx = i * 4u;
            outRgba8[idx + 0] = 128u;
            outRgba8[idx + 1] = 128u;
            outRgba8[idx + 2] = 255u;
            outRgba8[idx + 3] = defaultGlossByte;
        }
    }

    if (glossPixels && glossW > 0 && glossH > 0) {
        const int safeGCh = std::clamp(glossChannels, 1, 4);
        std::vector<uint8_t> glossRgba(static_cast<size_t>(glossW) * static_cast<size_t>(glossH) * 4u);
        for (size_t i = 0; i < static_cast<size_t>(glossW) * static_cast<size_t>(glossH); ++i) {
            const uint8_t gByte = ExtractLuminance(&glossPixels[i * static_cast<size_t>(safeGCh)], safeGCh);
            glossRgba[i * 4u + 0] = gByte;
            glossRgba[i * 4u + 1] = gByte;
            glossRgba[i * 4u + 2] = gByte;
            glossRgba[i * 4u + 3] = gByte;
        }
        std::vector<uint8_t> resizedGloss;
        ResizeRGBA8Bilinear(glossRgba.data(), glossW, glossH, targetW, targetH, resizedGloss);
        for (size_t i = 0; i < targetPixels; ++i) {
            outRgba8[i * 4u + 3] = resizedGloss[i * 4u + 0];
        }
    }

    return true;
}

ResolvedTerrainMaps Texture2DArray::ResolveTerrainMapCompanions(const std::string& diffusePath,
                                                                const std::string& explicitHeightPath,
                                                                const std::string& explicitNormalPath) {
    ResolvedTerrainMaps out;
    FileExistsResolved(diffusePath, out.DiffusePath);
    FileExistsResolved(explicitHeightPath, out.HeightPath);
    FileExistsResolved(explicitNormalPath, out.NormalPath);

    if (!diffusePath.empty()) {
        const std::filesystem::path probeBase(!out.DiffusePath.empty() ? out.DiffusePath : diffusePath);
        if (out.HeightPath.empty()) {
            out.HeightPath = ProbeCompanionSuffix(probeBase, "_h");
        }
        if (out.NormalPath.empty()) {
            out.NormalPath = ProbeCompanionSuffix(probeBase, "_n");
        }
    }
    return out;
}

bool Texture2DArray::BuildAlbedoHeightSliceFromFiles(const std::string& diffusePath,
                                                     const std::string& heightPath,
                                                     uint32_t targetW,
                                                     uint32_t targetH,
                                                     const glm::u8vec4& fallbackRgba,
                                                     std::vector<uint8_t>& outRgba8) {
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("Texture2DArray::BuildAlbedoHeightSliceFromFiles rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    std::string resolvedDiffuse;
    std::string resolvedHeight;
    FileExistsResolved(diffusePath, resolvedDiffuse);
    FileExistsResolved(heightPath, resolvedHeight);

    stbi_set_flip_vertically_on_load_thread(1);

    int dw = 0, dh = 0, dch = 0;
    StbiPtr dPixels(!resolvedDiffuse.empty() ? stbi_load(resolvedDiffuse.c_str(), &dw, &dh, &dch, 0) : nullptr);

    int hw = 0, hh = 0, hch = 0;
    StbiPtr hPixels(!resolvedHeight.empty() ? stbi_load(resolvedHeight.c_str(), &hw, &hh, &hch, 0) : nullptr);

    if (!diffusePath.empty() && !dPixels) {
        ENGINE_CORE_WARN("Texture2DArray: Failed to load terrain diffuse '{}' (using solid fallback color)", diffusePath);
    }
    if (!heightPath.empty() && !hPixels) {
        ENGINE_CORE_WARN("Texture2DArray: Failed to load terrain height map '{}' (using fallback height)", heightPath);
    }

    if (!dPixels || dw <= 0 || dh <= 0) {
        const uint8_t solidPixel[4] = { fallbackRgba.r, fallbackRgba.g, fallbackRgba.b, fallbackRgba.a };
        return PackAlbedoHeightRGBA8(solidPixel,
                                     4,
                                     1u,
                                     1u,
                                     hPixels.get(),
                                     hch,
                                     static_cast<uint32_t>(std::max(0, hw)),
                                     static_cast<uint32_t>(std::max(0, hh)),
                                     targetW,
                                     targetH,
                                     outRgba8,
                                     hPixels ? -1 : static_cast<int>(fallbackRgba.a));
    }

    ENGINE_CORE_DEBUG("Texture2DArray: Built albedo+height slice ({}x{}) from '{}' (diffuse {}x{}, height={})",
                      targetW, targetH, resolvedDiffuse, dw, dh,
                      hPixels ? resolvedHeight : "<luminance fallback>");

    return PackAlbedoHeightRGBA8(dPixels.get(),
                                 dch,
                                 static_cast<uint32_t>(dw),
                                 static_cast<uint32_t>(dh),
                                 hPixels.get(),
                                 hch,
                                 static_cast<uint32_t>(std::max(0, hw)),
                                 static_cast<uint32_t>(std::max(0, hh)),
                                 targetW,
                                 targetH,
                                 outRgba8);
}

bool Texture2DArray::BuildNormalGlossSliceFromFiles(const std::string& normalPath,
                                                    float glossiness,
                                                    uint32_t targetW,
                                                    uint32_t targetH,
                                                    std::vector<uint8_t>& outRgba8) {
    if (targetW == 0 || targetH == 0) {
        ENGINE_CORE_WARN("Texture2DArray::BuildNormalGlossSliceFromFiles rejected zero target dimensions ({}x{})",
                         targetW, targetH);
        return false;
    }

    std::string resolvedNormal;
    FileExistsResolved(normalPath, resolvedNormal);

    stbi_set_flip_vertically_on_load_thread(1);
    int nw = 0, nh = 0, nch = 0;
    StbiPtr nPixels(!resolvedNormal.empty() ? stbi_load(resolvedNormal.c_str(), &nw, &nh, &nch, 0) : nullptr);

    if (!normalPath.empty() && !nPixels) {
        ENGINE_CORE_WARN("Texture2DArray: Failed to load terrain normal map '{}' (using flat +Z fallback)", normalPath);
    } else if (nPixels) {
        ENGINE_CORE_DEBUG("Texture2DArray: Built normal+gloss slice ({}x{}, gloss={:.2f}) from '{}'",
                          targetW, targetH, glossiness, resolvedNormal);
    }

    return PackNormalGlossRGBA8(nPixels.get(),
                                nch,
                                static_cast<uint32_t>(std::max(0, nw)),
                                static_cast<uint32_t>(std::max(0, nh)),
                                glossiness,
                                nullptr,
                                0,
                                0u,
                                0u,
                                targetW,
                                targetH,
                                outRgba8);
}

} // namespace Engine
