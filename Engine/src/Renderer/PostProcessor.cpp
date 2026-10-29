#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/Shader.h"
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

PostProcessorStorage* s_PPData = nullptr;

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

    s_PPData = new PostProcessorStorage();
    if (!HasGLPostProcessSupport()) {
        return;
    }

    ENGINE_CORE_INFO("Initializing PostProcessor (GL_RGBA16F HDR + 5-Mip Bloom + Storybook Color Grading)...");

    glGenVertexArrays(1, &s_PPData->FullscreenVAO);

    const std::string fullscreenVertSrc = R"(
        #version 410 core
        out vec2 v_UV;

        void main() {
            vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            v_UV = uv;
            gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    // 1. 13-Tap Energy-Preserving Karis Downsample + Quadratic Soft-Knee Prefilter
    const std::string downsampleFragSrc = R"(
        #version 410 core
        in vec2 v_UV;
        out vec4 color;

        uniform sampler2D u_SrcTexture;
        uniform vec2 u_SrcTexelSize;
        uniform int u_MipLevel;
        uniform float u_Threshold;
        uniform float u_Knee;

        float luma(vec3 c) {
            return dot(c, vec3(0.2126, 0.7152, 0.0722));
        }

        float karisWeight(vec3 c) {
            return 1.0 / (1.0 + max(luma(c), 0.0));
        }

        vec3 softKneePrefilter(vec3 c) {
            c = max(c, vec3(0.0));
            float brightness = max(c.r, max(c.g, c.b));
            if (brightness <= 1e-6) {
                return vec3(0.0);
            }
            float rq = brightness - u_Threshold;
            if (u_Knee > 1e-5) {
                float soft = clamp(brightness - u_Threshold + u_Knee, 0.0, 2.0 * u_Knee);
                soft = (soft * soft) / (4.0 * u_Knee);
                rq = max(soft, brightness - u_Threshold);
            } else {
                rq = max(0.0, brightness - u_Threshold);
            }
            return c * (rq / brightness);
        }

        void main() {
            vec2 t = u_SrcTexelSize;

            vec3 a = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x,  2.0 * t.y)).rgb;
            vec3 b = texture(u_SrcTexture, v_UV + vec2( 0.0,        2.0 * t.y)).rgb;
            vec3 c = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x,  2.0 * t.y)).rgb;

            vec3 d = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x,  0.0)).rgb;
            vec3 e = texture(u_SrcTexture, v_UV).rgb;
            vec3 f = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x,  0.0)).rgb;

            vec3 g = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x, -2.0 * t.y)).rgb;
            vec3 h = texture(u_SrcTexture, v_UV + vec2( 0.0,       -2.0 * t.y)).rgb;
            vec3 i = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x, -2.0 * t.y)).rgb;

            vec3 j = texture(u_SrcTexture, v_UV + vec2(-1.0 * t.x,  1.0 * t.y)).rgb;
            vec3 k = texture(u_SrcTexture, v_UV + vec2( 1.0 * t.x,  1.0 * t.y)).rgb;
            vec3 l = texture(u_SrcTexture, v_UV + vec2(-1.0 * t.x, -1.0 * t.y)).rgb;
            vec3 m = texture(u_SrcTexture, v_UV + vec2( 1.0 * t.x, -1.0 * t.y)).rgb;

            vec3 g0 = (j + k + l + m) * 0.25;
            vec3 g1 = (a + b + d + e) * 0.25;
            vec3 g2 = (b + c + e + f) * 0.25;
            vec3 g3 = (d + e + g + h) * 0.25;
            vec3 g4 = (e + f + h + i) * 0.25;

            vec3 result;
            if (u_MipLevel == 0) {
                float w0 = 0.500 * karisWeight(g0);
                float w1 = 0.125 * karisWeight(g1);
                float w2 = 0.125 * karisWeight(g2);
                float w3 = 0.125 * karisWeight(g3);
                float w4 = 0.125 * karisWeight(g4);
                float wSum = max(w0 + w1 + w2 + w3 + w4, 1e-5);
                result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / wSum;
                result = softKneePrefilter(result);
            } else {
                result = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
            }

            color = vec4(max(result, vec3(0.0)), 1.0);
        }
    )";

    // 2. 9-Tap Tent Filter Progressive Upsample Shader
    const std::string upsampleFragSrc = R"(
        #version 410 core
        in vec2 v_UV;
        out vec4 color;

        uniform sampler2D u_SrcTexture;
        uniform vec2 u_TexelRadius;

        void main() {
            vec2 d = u_TexelRadius;
            vec3 s = vec3(0.0);
            s += texture(u_SrcTexture, v_UV + vec2(-d.x,  d.y)).rgb * 1.0;
            s += texture(u_SrcTexture, v_UV + vec2( 0.0,  d.y)).rgb * 2.0;
            s += texture(u_SrcTexture, v_UV + vec2( d.x,  d.y)).rgb * 1.0;

            s += texture(u_SrcTexture, v_UV + vec2(-d.x,  0.0)).rgb * 2.0;
            s += texture(u_SrcTexture, v_UV                   ).rgb * 4.0;
            s += texture(u_SrcTexture, v_UV + vec2( d.x,  0.0)).rgb * 2.0;

            s += texture(u_SrcTexture, v_UV + vec2(-d.x, -d.y)).rgb * 1.0;
            s += texture(u_SrcTexture, v_UV + vec2( 0.0, -d.y)).rgb * 2.0;
            s += texture(u_SrcTexture, v_UV + vec2( d.x, -d.y)).rgb * 1.0;

            color = vec4(max(s * (1.0 / 16.0), vec3(0.0)), 1.0);
        }
    )";

    // 3. Final HDR Composite, Tone Mapping, Storybook Split-Tone Grading & Diorama Vignette Shader
    const std::string compositeFragSrc = R"(
        #version 410 core
        in vec2 v_UV;
        out vec4 color;

        uniform sampler2D u_SceneHDR;
        uniform sampler2D u_BloomTexture;

        uniform int u_BloomEnabled;
        uniform float u_BloomIntensity;

        uniform int u_ToneMapMode;
        uniform float u_Exposure;

        uniform float u_Temperature;
        uniform float u_Tint;
        uniform float u_Contrast;
        uniform float u_Saturation;
        uniform vec3 u_ShadowTint;
        uniform vec3 u_HighlightTint;

        uniform int u_VignetteEnabled;
        uniform float u_VignetteIntensity;
        uniform float u_VignetteSmoothness;
        uniform vec3 u_VignetteColor;
        uniform float u_AspectRatio;

        vec3 toneMapACES(vec3 x) {
            vec3 num = x * (2.51 * x + vec3(0.03));
            vec3 den = x * (2.43 * x + vec3(0.59)) + vec3(0.14);
            return clamp(num / den, 0.0, 1.0);
        }

        vec3 toneMapStorybookNeutral(vec3 c) {
            const float startCompression = 0.8 - 0.04;
            const float desaturation = 0.15;

            float minCh = min(c.r, min(c.g, c.b));
            float offset = (minCh < 0.08) ? (minCh - 6.25 * minCh * minCh) : 0.04;
            c -= vec3(offset);

            float peak = max(c.r, max(c.g, c.b));
            if (peak < startCompression) {
                return clamp(c, 0.0, 1.0);
            }

            const float d = 1.0 - startCompression;
            float newPeak = 1.0 - (d * d) / (peak + d - startCompression);
            c *= (newPeak / peak);

            float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
            return clamp(mix(c, vec3(newPeak), g), 0.0, 1.0);
        }

        void main() {
            vec3 hdr = max(texture(u_SceneHDR, v_UV).rgb, vec3(0.0));
            if (u_BloomEnabled == 1) {
                vec3 bloom = max(texture(u_BloomTexture, v_UV).rgb, vec3(0.0));
                hdr += bloom * max(u_BloomIntensity, 0.0);
            }

            // Exposure (EV stops) & Pre-Tonemap Sensor White Balance
            hdr *= exp2(u_Exposure);
            vec3 wbGain = vec3(
                1.0 + 0.18 * u_Temperature + 0.08 * u_Tint,
                1.0 - 0.12 * u_Tint,
                1.0 - 0.18 * u_Temperature + 0.08 * u_Tint
            );
            hdr = max(hdr * max(wbGain, vec3(0.05)), vec3(0.0));

            // Tone Mapping to [0, 1] Display Range
            vec3 ldr = hdr;
            if (u_ToneMapMode == 0) {
                ldr = toneMapACES(hdr);
            } else if (u_ToneMapMode == 1) {
                ldr = toneMapStorybookNeutral(hdr);
            } else if (u_ToneMapMode == 2) {
                ldr = clamp(hdr / (vec3(1.0) + hdr), 0.0, 1.0);
            } else {
                ldr = clamp(hdr, 0.0, 1.0);
            }

            // Perceptual Split-Toning (Plum/Indigo Shadows + Warm Ivory Highlights)
            float baseLuma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
            float shadowMask = 1.0 - smoothstep(0.0, 0.55, baseLuma);
            float highlightMask = smoothstep(0.35, 1.0, baseLuma);
            vec3 splitGain = mix(vec3(1.0), max(u_ShadowTint, vec3(0.0)), shadowMask * 0.85) *
                             mix(vec3(1.0), max(u_HighlightTint, vec3(0.0)), highlightMask * 0.85);
            ldr *= splitGain;

            // Midpoint Contrast & Luma-Preserving Saturation
            ldr = (ldr - 0.5) * u_Contrast + 0.5;
            float gradedLuma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
            ldr = clamp(mix(vec3(gradedLuma), ldr, u_Saturation), 0.0, 1.0);

            // Colored Atmospheric Diorama Vignette
            if (u_VignetteEnabled == 1 && u_VignetteIntensity > 0.0001) {
                vec2 centered = (v_UV - 0.5) * 2.0;
                centered.x *= mix(1.0, max(u_AspectRatio, 1.0) * 0.82, 0.35);
                float dist = length(centered);
                float inner = max(0.05, 1.35 - u_VignetteIntensity * 0.95 - u_VignetteSmoothness * 0.55);
                float outer = inner + max(0.08, u_VignetteSmoothness * 1.15);
                float vigMask = smoothstep(inner, outer, dist) * clamp(u_VignetteIntensity * 1.25, 0.0, 1.0);
                ldr = mix(ldr, clamp(u_VignetteColor, 0.0, 1.0), clamp(vigMask, 0.0, 1.0));
            }

            color = vec4(ldr, 1.0);
        }
    )";

    s_PPData->DownsampleShader = std::make_shared<Shader>(fullscreenVertSrc, downsampleFragSrc);
    s_PPData->DownsampleShader->Bind();
    s_PPData->DownsampleShader->SetInt("u_SrcTexture", 0);

    s_PPData->UpsampleShader = std::make_shared<Shader>(fullscreenVertSrc, upsampleFragSrc);
    s_PPData->UpsampleShader->Bind();
    s_PPData->UpsampleShader->SetInt("u_SrcTexture", 0);

    s_PPData->CompositeShader = std::make_shared<Shader>(fullscreenVertSrc, compositeFragSrc);
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

    s_PPData->DownsampleShader.reset();
    s_PPData->UpsampleShader.reset();
    s_PPData->CompositeShader.reset();

    delete s_PPData;
    s_PPData = nullptr;
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

