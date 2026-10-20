#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Renderer.h"

namespace Engine {

struct Renderer2DStorage {
    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<Shader> TextureShader;
};

static Renderer2DStorage* s_Data{nullptr};

void Renderer2D::Init() {
    s_Data = new Renderer2DStorage();

    float vertices[4 * 5] = {
        -0.5f, -0.5f, 0.0f,    0.0f, 0.0f,
        0.5f, -0.5f, 0.0f,    1.0f, 0.0f,
        0.5f,  0.5f, 0.0f,    1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,    0.0f, 1.0f
    };
    auto vertexBuffer = std::make_shared<VertexBuffer>(vertices, static_cast<uint32_t>(sizeof(vertices)));

    uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    auto indexBuffer = std::make_shared<IndexBuffer>(indices, 6);

    s_Data->QuadVertexArray = std::make_shared<VertexArray>();
    s_Data->QuadVertexArray->AddVertexBuffer(vertexBuffer);
    s_Data->QuadVertexArray->SetIndexBuffer(indexBuffer);

    std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        
        out vec2 v_TexCoord;
        
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        
        void main() {
            v_TexCoord = a_TexCoord;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    std::string fragmentSrc = R"(
        #version 410 core
        in vec2 v_TexCoord;
        out vec4 color;
        
        uniform sampler2D u_Texture;
        uniform vec4 u_Color;
        
        void main() {
            color = texture(u_Texture, v_TexCoord) * u_Color;
        }
    )";

    s_Data->TextureShader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetInt("u_Texture", 0);
}

void Renderer2D::Shutdown() {
    delete s_Data;
    s_Data = nullptr;
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
}

void Renderer2D::EndScene() {
    // Placeholder for future batch rendering flush
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor) {
    s_Data->TextureShader->SetFloat4("u_Color", tintColor);
    s_Data->TextureShader->SetMat4("u_Transform", transform);

    if (texture) {
        texture->Bind(0);
    }

    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray);
}

} // namespace Engine

