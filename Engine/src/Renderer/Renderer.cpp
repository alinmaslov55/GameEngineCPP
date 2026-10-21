#include "Engine/Renderer/Renderer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

namespace Engine {
void Renderer::Init() {
    ENGINE_CORE_INFO("Initializing Renderer...");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ENGINE_CORE_INFO("Renderer initialized");
}

void Renderer::SetClearColor(const glm::vec4& color) {
    ENGINE_CORE_TRACE("Renderer SetClearColor ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
    glClearColor(color.r, color.g, color.b, color.a);
}

void Renderer::Clear() {
    ENGINE_CORE_TRACE("Renderer Clear");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray){
    ENGINE_CORE_TRACE("Renderer DrawIndexed (IndexCount: {})", vertexArray->GetIndexBuffer()->GetCount());
    glDrawElements(GL_TRIANGLES, vertexArray->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);
}
} // namespace Engine
