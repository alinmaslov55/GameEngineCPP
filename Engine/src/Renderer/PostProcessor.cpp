#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <memory>

namespace Engine {

namespace {

constexpr uint32_t kBloomMipCount = 5;

struct BloomMip {
    uint32_t FramebufferID = 0;
    uint32_t TextureID = 0;
    uint32_t Width = 1;
    uint32_t Height = 1;
};

struct PostProcessorStorage {
    uint32_t HDRFramebufferID = 0;
    uint32_t HDRColorTextureID = 0;
    uint32_t HDRDepthRBO = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;

    std::array<BloomMip, kBloomMipCount> BloomMips{};

    uint32_t FullscreenVAO = 0;
    std::shared_ptr<Shader> DownsampleShader;
    std::shared_ptr<Shader> UpsampleShader;
    std::shared_ptr<Shader> CompositeShader;

    GLint SavedFBO = 0;
    GLint SavedViewport[4] = { 0, 0, 1280, 720 };
    bool SceneActive = false;
    bool ResourcesValid = false;
};

std::unique_ptr<PostProcessorStorage> s_PPData = nullptr;

bool HasGLPostProcessSupport() {
    return glad_glGenFramebuffers != nullptr &&
           glad_glGenTextures != nullptr &&
           glad_glGenRenderbuffers != nullptr &&
           glad_glGenVertexArrays != nullptr;
}

void DestroyFramebuffers() {
    if (!s_PPData || !HasGLPostProcessSupport()) {
        return;
    }

    for (auto& mip : s_PPData->BloomMips) {
        if (mip.FramebufferID != 0) {
            glDeleteFramebuffers(1, &mip.FramebufferID);
            mip.FramebufferID = 0;
        }
        if (mip.TextureID != 0) {
            glDeleteTextures(1, &mip.TextureID);
            mip.TextureID = 0;
        }
    }

    if (s_PPData->HDRDepthRBO != 0) {
        glDeleteRenderbuffers(1, &s_PPData->HDRDepthRBO);
        s_PPData->HDRDepthRBO = 0;
    }
    if (s_PPData->HDRColorTextureID != 0) {
        glDeleteTextures(1, &s_PPData->HDRColorTextureID);
        s_PPData->HDRColorTextureID = 0;
    }
    if (s_PPData->HDRFramebufferID != 0) {
        glDeleteFramebuffers(1, &s_PPData->HDRFramebufferID);
        s_PPData->HDRFramebufferID = 0;
    }

    s_PPData->Width = 0;
    s_PPData->Height = 0;
}

void EnsureTargetsSized(uint32_t width, uint32_t height) {
    if (!s_PPData || !HasGLPostProcessSupport()) {
        return;
    }

    const uint32_t safeW = std::clamp(width, 2u, 8192u);
    const uint32_t safeH = std::clamp(height, 2u, 8192u);
    if (s_PPData->HDRFramebufferID != 0 &&
        s_PPData->Width == safeW &&
        s_PPData->Height == safeH) {
        return;
    }

    DestroyFramebuffers();

    s_PPData->Width = safeW;
    s_PPData->Height = safeH;

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    // 1. Primary GL_RGBA16F HDR Scene Framebuffer + 24-bit Depth / 8-bit Stencil RBO
    glGenFramebuffers(1, &s_PPData->HDRFramebufferID);
    glBindFramebuffer(GL_FRAMEBUFFER, s_PPData->HDRFramebufferID);

    glGenTextures(1, &s_PPData->HDRColorTextureID);
    glBindTexture(GL_TEXTURE_2D, s_PPData->HDRColorTextureID);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA16F,
                 static_cast<GLsizei>(safeW),
                 static_cast<GLsizei>(safeH),
                 0,
                 GL_RGBA,
                 GL_HALF_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_PPData->HDRColorTextureID, 0);

    glGenRenderbuffers(1, &s_PPData->HDRDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, s_PPData->HDRDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER,
                          GL_DEPTH24_STENCIL8,
                          static_cast<GLsizei>(safeW),
                          static_cast<GLsizei>(safeH));
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_PPData->HDRDepthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_CORE_ERROR("PostProcessor HDR FBO incomplete! Status: {:#x}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    // 2. 5-Level Progressive GL_RGBA16F Bloom Pyramid (1/2, 1/4, 1/8, 1/16, 1/32)
    uint32_t mipW = safeW;
    uint32_t mipH = safeH;
    for (uint32_t i = 0; i < kBloomMipCount; ++i) {
        mipW = std::max(1u, mipW / 2u);
        mipH = std::max(1u, mipH / 2u);

        BloomMip& mip = s_PPData->BloomMips[i];
        mip.Width = mipW;
        mip.Height = mipH;

        glGenFramebuffers(1, &mip.FramebufferID);
        glBindFramebuffer(GL_FRAMEBUFFER, mip.FramebufferID);

        glGenTextures(1, &mip.TextureID);
        glBindTexture(GL_TEXTURE_2D, mip.TextureID);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA16F,
                     static_cast<GLsizei>(mipW),
                     static_cast<GLsizei>(mipH),
                     0,
                     GL_RGBA,
                     GL_HALF_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.TextureID, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ENGINE_CORE_ERROR("PostProcessor Bloom Mip {} FBO incomplete! Status: {:#x}",
                              i, glCheckFramebufferStatus(GL_FRAMEBUFFER));
        }
    }

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));

    ENGINE_CORE_INFO("PostProcessor resized HDR targets to {}x{} (5-mip Bloom chain down to {}x{})",
                     safeW, safeH,
                     s_PPData->BloomMips.back().Width,
                     s_PPData->BloomMips.back().Height);
}

} // namespace

void PostProcessor::Init() {
    if (s_PPData) {
        return;
    }

    s_PPData = std::make_unique<PostProcessorStorage>();
    if (!HasGLPostProcessSupport()) {
        return;
    }

    ENGINE_CORE_INFO("Initializing PostProcessor (GL_RGBA16F HDR + 5-Mip Bloom + Storybook Color Grading)...");

    glGenVertexArrays(1, &s_PPData->FullscreenVAO);

    std::string downsamplePath = ResourceManager::ResolvePath("assets/shaders/PostProcess_BloomDownsample.glsl");
    s_PPData->DownsampleShader = std::make_shared<Shader>(downsamplePath);
    s_PPData->DownsampleShader->Bind();
    s_PPData->DownsampleShader->SetInt("u_SrcTexture", 0);

    std::string upsamplePath = ResourceManager::ResolvePath("assets/shaders/PostProcess_BloomUpsample.glsl");
    s_PPData->UpsampleShader = std::make_shared<Shader>(upsamplePath);
    s_PPData->UpsampleShader->Bind();
    s_PPData->UpsampleShader->SetInt("u_SrcTexture", 0);

    std::string compositePath = ResourceManager::ResolvePath("assets/shaders/PostProcess_Composite.glsl");
    s_PPData->CompositeShader = std::make_shared<Shader>(compositePath);
    s_PPData->CompositeShader->Bind();
    s_PPData->CompositeShader->SetInt("u_SceneHDR", 0);
    s_PPData->CompositeShader->SetInt("u_BloomTexture", 1);

    s_PPData->ResourcesValid = (s_PPData->FullscreenVAO != 0);
    ENGINE_CORE_INFO("PostProcessor initialized successfully");
}

void PostProcessor::Shutdown() {
    if (!s_PPData) {
        return;
    }

    DestroyFramebuffers();

    if (HasGLPostProcessSupport() && s_PPData->FullscreenVAO != 0) {
        glDeleteVertexArrays(1, &s_PPData->FullscreenVAO);
        s_PPData->FullscreenVAO = 0;
    }

    s_PPData.reset();
    ENGINE_CORE_INFO("PostProcessor shutdown completed");
}

bool PostProcessor::IsValid() {
    return s_PPData != nullptr && s_PPData->ResourcesValid;
}

void PostProcessor::BeginHDRScene(uint32_t fallbackWidth,
                                  uint32_t fallbackHeight,
                                  const glm::vec3& clearColor) {
    if (!IsValid()) {
        return;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_PPData->SavedFBO);
    glGetIntegerv(GL_VIEWPORT, s_PPData->SavedViewport);

    const uint32_t targetW = (s_PPData->SavedViewport[2] > 0)
        ? static_cast<uint32_t>(s_PPData->SavedViewport[2])
        : std::max(1u, fallbackWidth);
    const uint32_t targetH = (s_PPData->SavedViewport[3] > 0)
        ? static_cast<uint32_t>(s_PPData->SavedViewport[3])
        : std::max(1u, fallbackHeight);

    // Ensure HDR attachments are unbound from texture units before FBO write
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    EnsureTargetsSized(targetW, targetH);

    glBindFramebuffer(GL_FRAMEBUFFER, s_PPData->HDRFramebufferID);
    glViewport(0, 0, static_cast<GLsizei>(s_PPData->Width), static_cast<GLsizei>(s_PPData->Height));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(std::max(0.0f, clearColor.r),
                 std::max(0.0f, clearColor.g),
                 std::max(0.0f, clearColor.b),
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    s_PPData->SceneActive = true;
}

void PostProcessor::EndHDRAndComposite(const PostProcessComponent& settings) {
    if (!IsValid() || !s_PPData->SceneActive) {
        return;
    }
    s_PPData->SceneActive = false;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glBindVertexArray(s_PPData->FullscreenVAO);

    const bool runBloom = settings.BloomEnabled && settings.BloomIntensity > 0.0001f;
    if (runBloom) {
        // 1. Progressive 13-Tap Downsample Pass (HDR Scene -> Mip 0 -> Mip 1 -> Mip 2 -> Mip 3 -> Mip 4)
        s_PPData->DownsampleShader->Bind();
        s_PPData->DownsampleShader->SetFloat("u_Threshold", std::max(0.0f, settings.BloomThreshold));
        s_PPData->DownsampleShader->SetFloat("u_Knee", std::max(0.0f, settings.BloomKnee));

        uint32_t srcTex = s_PPData->HDRColorTextureID;
        uint32_t srcW = s_PPData->Width;
        uint32_t srcH = s_PPData->Height;

        glActiveTexture(GL_TEXTURE0);
        for (uint32_t i = 0; i < kBloomMipCount; ++i) {
            const BloomMip& dstMip = s_PPData->BloomMips[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dstMip.FramebufferID);
            glViewport(0, 0, static_cast<GLsizei>(dstMip.Width), static_cast<GLsizei>(dstMip.Height));

            glBindTexture(GL_TEXTURE_2D, srcTex);
            s_PPData->DownsampleShader->SetInt("u_MipLevel", static_cast<int>(i));
            s_PPData->DownsampleShader->SetFloat2("u_SrcTexelSize", {
                1.0f / static_cast<float>(std::max(1u, srcW)),
                1.0f / static_cast<float>(std::max(1u, srcH))
            });

            glDrawArrays(GL_TRIANGLES, 0, 3);

            srcTex = dstMip.TextureID;
            srcW = dstMip.Width;
            srcH = dstMip.Height;
        }

        // 2. Progressive 9-Tap Tent Upsample Pass (Mip 4 -> Mip 3 -> Mip 2 -> Mip 1 -> Mip 0)
        //    Energy-conserving scatter blend: dst = scatter * Tent(Mip[i]) + (1 - scatter) * Mip[i-1]
        const float scatter = std::clamp(settings.BloomScatter, 0.0f, 0.95f);
        glEnable(GL_BLEND);
        if (glad_glBlendColor != nullptr) {
            glBlendColor(scatter, scatter, scatter, 1.0f - scatter);
            glBlendFunc(GL_CONSTANT_COLOR, GL_CONSTANT_ALPHA);
        } else {
            glBlendFunc(GL_ONE, GL_ONE);
        }

        s_PPData->UpsampleShader->Bind();
        for (int i = static_cast<int>(kBloomMipCount) - 1; i >= 1; --i) {
            const BloomMip& srcMip = s_PPData->BloomMips[static_cast<size_t>(i)];
            const BloomMip& dstMip = s_PPData->BloomMips[static_cast<size_t>(i - 1)];

            glBindFramebuffer(GL_FRAMEBUFFER, dstMip.FramebufferID);
            glViewport(0, 0, static_cast<GLsizei>(dstMip.Width), static_cast<GLsizei>(dstMip.Height));

            glBindTexture(GL_TEXTURE_2D, srcMip.TextureID);
            s_PPData->UpsampleShader->SetFloat2("u_TexelRadius", {
                1.15f / static_cast<float>(std::max(1u, srcMip.Width)),
                1.15f / static_cast<float>(std::max(1u, srcMip.Height))
            });

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glDisable(GL_BLEND);
    }

    // 3. Final HDR Composite, ToneMap, Color Grade & Vignette onto Saved LDR Target FBO
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s_PPData->SavedFBO));
    glViewport(s_PPData->SavedViewport[0],
               s_PPData->SavedViewport[1],
               s_PPData->SavedViewport[2],
               s_PPData->SavedViewport[3]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_PPData->HDRColorTextureID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, runBloom ? s_PPData->BloomMips[0].TextureID : s_PPData->HDRColorTextureID);

    auto& comp = *s_PPData->CompositeShader;
    comp.Bind();
    comp.SetInt("u_BloomEnabled", runBloom ? 1 : 0);
    comp.SetFloat("u_BloomIntensity", std::max(0.0f, settings.BloomIntensity));

    comp.SetInt("u_ToneMapMode", static_cast<int>(settings.ToneMapping));
    comp.SetFloat("u_Exposure", std::clamp(settings.Exposure, -5.0f, 5.0f));

    comp.SetFloat("u_Temperature", std::clamp(settings.Temperature, -1.0f, 1.0f));
    comp.SetFloat("u_Tint", std::clamp(settings.Tint, -1.0f, 1.0f));
    comp.SetFloat("u_Contrast", std::clamp(settings.Contrast, 0.2f, 2.5f));
    comp.SetFloat("u_Saturation", std::clamp(settings.Saturation, 0.0f, 2.5f));
    comp.SetFloat3("u_ShadowTint", settings.ShadowTint);
    comp.SetFloat3("u_HighlightTint", settings.HighlightTint);

    comp.SetInt("u_VignetteEnabled", settings.VignetteEnabled ? 1 : 0);
    comp.SetFloat("u_VignetteIntensity", std::clamp(settings.VignetteIntensity, 0.0f, 1.0f));
    comp.SetFloat("u_VignetteSmoothness", std::clamp(settings.VignetteSmoothness, 0.05f, 1.5f));
    comp.SetFloat3("u_VignetteColor", settings.VignetteColor);

    const float aspect = (s_PPData->Height > 0)
        ? (static_cast<float>(s_PPData->Width) / static_cast<float>(s_PPData->Height))
        : (16.0f / 9.0f);
    comp.SetFloat("u_AspectRatio", aspect);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Restore clean baseline OpenGL state for Pass 4 (2D Screen-Space UI HUD)
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace Engine

