#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <algorithm>

namespace Engine {

namespace {

struct ShadowMapStorage {
    uint32_t FramebufferID = 0;
    uint32_t DepthTextureID = 0;
    uint32_t Resolution = 2048;

    GLint SavedFBO = 0;
    GLint SavedViewport[4] = { 0, 0, 1280, 720 };
};

ShadowMapStorage* s_ShadowData = nullptr;

bool HasGLFramebufferSupport() {
    return glad_glGenFramebuffers != nullptr && glad_glGenTextures != nullptr;
}

} // namespace

void ShadowMap::Init(uint32_t resolution) {
    if (s_ShadowData) {
        return;
    }

    s_ShadowData = new ShadowMapStorage();
    s_ShadowData->Resolution = std::clamp(resolution, 256u, 4096u);

    if (!HasGLFramebufferSupport()) {
        return;
    }

    ENGINE_CORE_INFO("Initializing ShadowMap ({}x{} 24-bit depth FBO)...",
                     s_ShadowData->Resolution, s_ShadowData->Resolution);

    glGenFramebuffers(1, &s_ShadowData->FramebufferID);
    glGenTextures(1, &s_ShadowData->DepthTextureID);

    glBindTexture(GL_TEXTURE_2D, s_ShadowData->DepthTextureID);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(s_ShadowData->Resolution),
                 static_cast<GLsizei>(s_ShadowData->Resolution),
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);

    // GL_NEAREST ensures PCF compares raw depth values before averaging visibility
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float borderWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderWhite);

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowData->FramebufferID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_ShadowData->DepthTextureID, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_CORE_ERROR("ShadowMap FBO incomplete! Status: {:#x}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ShadowMap::Shutdown() {
    if (!s_ShadowData) {
        return;
    }
    if (HasGLFramebufferSupport()) {
        if (s_ShadowData->FramebufferID != 0) {
            glDeleteFramebuffers(1, &s_ShadowData->FramebufferID);
        }
        if (s_ShadowData->DepthTextureID != 0) {
            glDeleteTextures(1, &s_ShadowData->DepthTextureID);
        }
    }
    delete s_ShadowData;
    s_ShadowData = nullptr;
    ENGINE_CORE_INFO("ShadowMap shutdown completed");
}

bool ShadowMap::IsValid() {
    return s_ShadowData != nullptr &&
           s_ShadowData->FramebufferID != 0 &&
           s_ShadowData->DepthTextureID != 0;
}

void ShadowMap::BeginCapture() {
    if (!IsValid()) {
        return;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_ShadowData->SavedFBO);
    glGetIntegerv(GL_VIEWPORT, s_ShadowData->SavedViewport);

    // Ensure the shadow depth texture is unbound from shader sampler units (slots 5 & 15) before FBO write
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, s_ShadowData->FramebufferID);
    glViewport(0, 0, static_cast<GLsizei>(s_ShadowData->Resolution), static_cast<GLsizei>(s_ShadowData->Resolution));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::EndCapture() {
    if (!IsValid()) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s_ShadowData->SavedFBO));
    glViewport(s_ShadowData->SavedViewport[0],
               s_ShadowData->SavedViewport[1],
               s_ShadowData->SavedViewport[2],
               s_ShadowData->SavedViewport[3]);
}

uint32_t ShadowMap::GetDepthTextureID() {
    return s_ShadowData ? s_ShadowData->DepthTextureID : 0u;
}

uint32_t ShadowMap::GetResolution() {
    return s_ShadowData ? s_ShadowData->Resolution : 2048u;
}

} // namespace Engine
