#include "Engine/Renderer/Renderer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

namespace Engine {
void Renderer::Init() {
    ENGINE_CORE_INFO("Initializing Renderer (3D Depth Buffering enabled)...");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ENGINE_CORE_INFO("Renderer initialized");
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

void Renderer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount) {
    uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
    ENGINE_CORE_TRACE("Renderer DrawIndexed (IndexCount: {})", count);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
}
} // namespace Engine
