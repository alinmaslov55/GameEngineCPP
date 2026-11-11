#include "Engine/Renderer/TorchShadowMap.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Engine {

namespace {

bool HasGLFramebufferSupport() {
    return glad_glGenFramebuffers != nullptr && glad_glGenTextures != nullptr;
}

struct TorchShadowMapStorage {
    uint32_t FramebufferID = 0;
    uint32_t DepthTextureID = 0;
    uint32_t QuadrantResolution = TorchShadowMap::DefaultQuadrantResolution;
    uint32_t AtlasWidth = TorchShadowMap::QuadrantCount * TorchShadowMap::DefaultQuadrantResolution;
    uint32_t AtlasHeight = TorchShadowMap::DefaultQuadrantResolution;

    GLint SavedFBO = 0;
    GLint SavedViewport[4] = { 0, 0, 1280, 720 };
    GLint SavedScissorBox[4] = { 0, 0, 1280, 720 };
    GLboolean SavedScissorEnabled = GL_FALSE;

    ~TorchShadowMapStorage() {
        if (HasGLFramebufferSupport()) {
            if (FramebufferID != 0) {
                glDeleteFramebuffers(1, &FramebufferID);
                FramebufferID = 0;
            }
            if (DepthTextureID != 0) {
                glDeleteTextures(1, &DepthTextureID);
                DepthTextureID = 0;
            }
        }
    }
};

std::unique_ptr<TorchShadowMapStorage> s_TorchShadowData = nullptr;

} // namespace

void TorchShadowMap::Init(uint32_t quadrantResolution) {
    if (s_TorchShadowData) {
        return;
    }

    s_TorchShadowData = std::make_unique<TorchShadowMapStorage>();
    s_TorchShadowData->QuadrantResolution = std::clamp(quadrantResolution, 128u, 1024u);
    s_TorchShadowData->AtlasWidth = QuadrantCount * s_TorchShadowData->QuadrantResolution;
    s_TorchShadowData->AtlasHeight = s_TorchShadowData->QuadrantResolution;

    if (!HasGLFramebufferSupport()) {
        return;
    }

    ENGINE_CORE_INFO("Initializing TorchShadowMap ({}x{} 4-quadrant 24-bit depth atlas)...",
                     s_TorchShadowData->AtlasWidth, s_TorchShadowData->AtlasHeight);

    glGenFramebuffers(1, &s_TorchShadowData->FramebufferID);
    glGenTextures(1, &s_TorchShadowData->DepthTextureID);

    glBindTexture(GL_TEXTURE_2D, s_TorchShadowData->DepthTextureID);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(s_TorchShadowData->AtlasWidth),
                 static_cast<GLsizei>(s_TorchShadowData->AtlasHeight),
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, s_TorchShadowData->FramebufferID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_TorchShadowData->DepthTextureID, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ENGINE_CORE_ERROR("TorchShadowMap FBO incomplete! Status: {:#x}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TorchShadowMap::Shutdown() {
    if (!s_TorchShadowData) {
        return;
    }
    s_TorchShadowData.reset();
    ENGINE_CORE_INFO("TorchShadowMap shutdown completed");
}

bool TorchShadowMap::IsValid() {
    return s_TorchShadowData != nullptr &&
           s_TorchShadowData->FramebufferID != 0 &&
           s_TorchShadowData->DepthTextureID != 0;
}

void TorchShadowMap::BeginCapture() {
    if (!IsValid()) {
        return;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_TorchShadowData->SavedFBO);
    glGetIntegerv(GL_VIEWPORT, s_TorchShadowData->SavedViewport);
    s_TorchShadowData->SavedScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, s_TorchShadowData->SavedScissorBox);

    // Ensure the torch shadow depth atlas is unbound from shader sampler units (6 & 14) before FBO write
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, s_TorchShadowData->FramebufferID);
    glViewport(0, 0, static_cast<GLsizei>(s_TorchShadowData->AtlasWidth), static_cast<GLsizei>(s_TorchShadowData->AtlasHeight));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void TorchShadowMap::BeginQuadrant(TorchShadowQuadrant quadrant) {
    if (!IsValid()) {
        return;
    }

    const uint32_t qIdx = std::min(static_cast<uint32_t>(quadrant), QuadrantCount - 1u);
    const uint32_t qSize = s_TorchShadowData->QuadrantResolution;
    const GLint xOffset = static_cast<GLint>(qIdx * qSize);
    const GLsizei size = static_cast<GLsizei>(qSize);

    glViewport(xOffset, 0, size, size);
    glEnable(GL_SCISSOR_TEST);
    glScissor(xOffset, 0, size, size);
}

void TorchShadowMap::EndCapture() {
    if (!IsValid()) {
        return;
    }

    if (s_TorchShadowData->SavedScissorEnabled) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    glScissor(s_TorchShadowData->SavedScissorBox[0],
              s_TorchShadowData->SavedScissorBox[1],
              s_TorchShadowData->SavedScissorBox[2],
              s_TorchShadowData->SavedScissorBox[3]);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s_TorchShadowData->SavedFBO));
    glViewport(s_TorchShadowData->SavedViewport[0],
               s_TorchShadowData->SavedViewport[1],
               s_TorchShadowData->SavedViewport[2],
               s_TorchShadowData->SavedViewport[3]);
}

uint32_t TorchShadowMap::GetDepthTextureID() {
    return s_TorchShadowData ? s_TorchShadowData->DepthTextureID : 0u;
}

uint32_t TorchShadowMap::GetQuadrantResolution() {
    return s_TorchShadowData ? s_TorchShadowData->QuadrantResolution : DefaultQuadrantResolution;
}

uint32_t TorchShadowMap::GetAtlasWidth() {
    return s_TorchShadowData ? s_TorchShadowData->AtlasWidth : (QuadrantCount * DefaultQuadrantResolution);
}

uint32_t TorchShadowMap::GetAtlasHeight() {
    return s_TorchShadowData ? s_TorchShadowData->AtlasHeight : DefaultQuadrantResolution;
}

glm::vec3 TorchShadowMap::GetQuadrantForward(TorchShadowQuadrant quadrant) noexcept {
    switch (quadrant) {
        case TorchShadowQuadrant::North: return {  0.0f, 0.0f, -1.0f };
        case TorchShadowQuadrant::East:  return {  1.0f, 0.0f,  0.0f };
        case TorchShadowQuadrant::South: return {  0.0f, 0.0f,  1.0f };
        case TorchShadowQuadrant::West:  return { -1.0f, 0.0f,  0.0f };
        default:                         return {  0.0f, 0.0f, -1.0f };
    }
}

glm::vec3 TorchShadowMap::GetQuadrantRight(TorchShadowQuadrant quadrant) noexcept {
    switch (quadrant) {
        case TorchShadowQuadrant::North: return {  1.0f, 0.0f,  0.0f };
        case TorchShadowQuadrant::East:  return {  0.0f, 0.0f,  1.0f };
        case TorchShadowQuadrant::South: return { -1.0f, 0.0f,  0.0f };
        case TorchShadowQuadrant::West:  return {  0.0f, 0.0f, -1.0f };
        default:                         return {  1.0f, 0.0f,  0.0f };
    }
}

glm::mat4 TorchShadowMap::ComputeQuadrantLightMatrix(TorchShadowQuadrant quadrant,
                                                     const glm::vec3& torchWorldPos,
                                                     float radius) noexcept {
    const float safeFar = std::clamp(radius, NearPlane + 0.5f, MaxShadowRadius);
    const glm::vec3 forward = GetQuadrantForward(quadrant);
    const glm::mat4 lightView = glm::lookAt(torchWorldPos, torchWorldPos + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 lightProj = glm::perspective(glm::radians(90.0f), 1.0f, NearPlane, safeFar);
    return lightProj * lightView;
}

std::array<glm::mat4, TorchShadowMap::QuadrantCount> TorchShadowMap::ComputeAllQuadrantMatrices(
    const glm::vec3& torchWorldPos,
    float radius) noexcept {
    return {
        ComputeQuadrantLightMatrix(TorchShadowQuadrant::North, torchWorldPos, radius),
        ComputeQuadrantLightMatrix(TorchShadowQuadrant::East,  torchWorldPos, radius),
        ComputeQuadrantLightMatrix(TorchShadowQuadrant::South, torchWorldPos, radius),
        ComputeQuadrantLightMatrix(TorchShadowQuadrant::West,  torchWorldPos, radius)
    };
}

glm::vec3 TorchShadowMap::ComputeRadialBillboardRightXZ(const glm::vec3& standeePos,
                                                        const glm::vec3& torchPos) noexcept {
    const glm::vec3 toStandeeXZ{ standeePos.x - torchPos.x, 0.0f, standeePos.z - torchPos.z };
    const float lenSq = toStandeeXZ.x * toStandeeXZ.x + toStandeeXZ.z * toStandeeXZ.z;
    if (lenSq <= 1e-8f) {
        return { 1.0f, 0.0f, 0.0f };
    }
    return glm::normalize(glm::cross(toStandeeXZ, glm::vec3(0.0f, 1.0f, 0.0f)));
}

} // namespace Engine
