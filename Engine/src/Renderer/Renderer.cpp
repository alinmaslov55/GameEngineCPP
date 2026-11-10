#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Buffer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <memory>

namespace Engine {

namespace {
std::unique_ptr<UniformBuffer> s_PointLightUBO = nullptr;
PointLightUBOBlock s_PointLightUploadScratch{};
} // namespace

void Renderer::Init() {
    ENGINE_CORE_INFO("Initializing Renderer (3D Depth Buffering enabled)...");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (glad_glGenBuffers != nullptr) {
        s_PointLightUBO = std::make_unique<UniformBuffer>(
            static_cast<uint32_t>(sizeof(PointLightUBOBlock)),
            SceneLightingData::PointLightUBOBinding
        );
    }
    ENGINE_CORE_INFO("Renderer initialized");
}

void Renderer::Shutdown() {
    s_PointLightUBO.reset();
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
    s_PointLightUBO->SetData(&s_PointLightUploadScratch, uploadBytes, 0);
}

void Renderer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    ENGINE_CORE_TRACE("Renderer SetViewport ({}, {}, {}x{})", x, y, width, height);
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void Renderer::SetDepthTest(bool enabled) {
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void Renderer::SetDepthMask(bool writeEnabled) {
    glDepthMask(writeEnabled ? GL_TRUE : GL_FALSE);
}

void Renderer::SetPolygonOffset(bool enabled, float factor, float units) {
    if (enabled) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(factor, units);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void Renderer::SetClearColor(const glm::vec4& color) {
    ENGINE_CORE_TRACE("Renderer SetClearColor ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
    glClearColor(color.r, color.g, color.b, color.a);
}

void Renderer::Clear() {
    ENGINE_CORE_TRACE("Renderer Clear");
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount, uint32_t baseVertex) {
    uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
    ENGINE_CORE_TRACE("Renderer DrawIndexed (IndexCount: {}, BaseVertex: {})", count, baseVertex);
    if (baseVertex == 0 || glad_glDrawElementsBaseVertex == nullptr) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr, static_cast<GLint>(baseVertex));
    }
}
} // namespace Engine
