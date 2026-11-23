#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Renderer/RHI/Buffer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstring>
#include <memory>

namespace Engine {

namespace {
std::unique_ptr<UniformBuffer> s_PointLightUBO = nullptr;
PointLightUBOBlock s_PointLightUploadScratch{};
PointLightUBOBlock s_LastUploadedPointLights{};
bool s_HasUploadedPointLights = false;
uint64_t s_PointLightUploadCount = 0;
} // namespace

void Renderer::Init() {
    ENGINE_CORE_INFO("Initializing Renderer (3D Depth Buffering enabled)...");
    if (glad_glEnable != nullptr) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    s_HasUploadedPointLights = false;
    s_PointLightUploadCount = 0;
    std::memset(&s_LastUploadedPointLights, 0, sizeof(PointLightUBOBlock));

    s_PointLightUBO = std::make_unique<UniformBuffer>(
        static_cast<uint32_t>(sizeof(PointLightUBOBlock)),
        SceneLightingData::PointLightUBOBinding
    );
    ENGINE_CORE_INFO("Renderer initialized");
}

void Renderer::Shutdown() {
    s_PointLightUBO.reset();
    s_HasUploadedPointLights = false;
    s_PointLightUploadCount = 0;
    std::memset(&s_LastUploadedPointLights, 0, sizeof(PointLightUBOBlock));
    ENGINE_CORE_INFO("Renderer shut down");
}

void Renderer::UploadPointLights(const SceneLightingData* lighting) {
    if (!s_PointLightUBO) {
        return;
    }

    uint32_t count = 0;
    int shadowLightActive = 0;
    if (lighting && lighting->LightingActive) {
        count = std::min(lighting->PointLightCount, SceneLightingData::MaxPointLights);
        if (count > 0 && lighting->PointLights[0].Params.y > 0.5f) {
            shadowLightActive = 1;
        }
        if (count > 0) {
            std::copy_n(lighting->PointLights.begin(), count, s_PointLightUploadScratch.PointLights.begin());
        }
    }

    s_PointLightUploadScratch.Header = { static_cast<int>(count), shadowLightActive, 0, 0 };
    const uint32_t uploadBytes = static_cast<uint32_t>(sizeof(glm::ivec4)) +
                                 count * static_cast<uint32_t>(sizeof(PointLightUniform));

    // Deduplicate uploads: skip glBufferSubData when lighting payload is identical
    if (s_HasUploadedPointLights &&
        std::memcmp(&s_PointLightUploadScratch, &s_LastUploadedPointLights, uploadBytes) == 0) {
        return;
    }

    s_PointLightUBO->SetData(&s_PointLightUploadScratch, uploadBytes, 0);
    std::memcpy(&s_LastUploadedPointLights, &s_PointLightUploadScratch, uploadBytes);
    s_HasUploadedPointLights = true;
    ++s_PointLightUploadCount;
}

uint64_t Renderer::GetPointLightUploadCount() {
    return s_PointLightUploadCount;
}

void Renderer::ResetPointLightUploadCount() {
    s_PointLightUploadCount = 0;
    s_HasUploadedPointLights = false;
    std::memset(&s_LastUploadedPointLights, 0, sizeof(PointLightUBOBlock));
}

void Renderer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (glad_glViewport != nullptr) {
        glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }
}

void Renderer::SetDepthTest(bool enabled) {
    if (glad_glEnable == nullptr || glad_glDisable == nullptr) {
        return;
    }
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void Renderer::SetDepthMask(bool writeEnabled) {
    if (glad_glDepthMask != nullptr) {
        glDepthMask(writeEnabled ? GL_TRUE : GL_FALSE);
    }
}

void Renderer::SetPolygonOffset(bool enabled, float factor, float units) {
    if (glad_glEnable == nullptr || glad_glDisable == nullptr || glad_glPolygonOffset == nullptr) {
        return;
    }
    if (enabled) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(factor, units);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void Renderer::SetClearColor(const glm::vec4& color) {
    if (glad_glClearColor != nullptr) {
        glClearColor(color.r, color.g, color.b, color.a);
    }
}

void Renderer::Clear() {
    if (glad_glDepthMask != nullptr) {
        glDepthMask(GL_TRUE);
    }
    if (glad_glClear != nullptr) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void Renderer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount, uint32_t baseVertex) {
    if (!vertexArray) {
        return;
    }
    uint32_t count = indexCount;
    if (count == 0) {
        const auto& ib = vertexArray->GetIndexBuffer();
        if (!ib) {
            return;
        }
        count = ib->GetCount();
    }
    if (count == 0) {
        return;
    }

    if (baseVertex == 0 || glad_glDrawElementsBaseVertex == nullptr) {
        if (glad_glDrawElements != nullptr) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
        }
    } else {
        glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr, static_cast<GLint>(baseVertex));
    }
}
} // namespace Engine
