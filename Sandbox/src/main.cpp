#include <Engine/Core/Application.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/VertexArray.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Renderer.h>

int main(int argc, char* argv[]) {
    Engine::Application app;

    float vertices[3 * 3] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };
    auto vertexBuffer = std::make_shared<Engine::VertexBuffer>(vertices, sizeof(vertices));

    uint32_t indices[3] = { 0, 1, 2 };
    auto indexBuffer = std::make_shared<Engine::IndexBuffer>(indices, 3);

    auto vertexArray = std::make_shared<Engine::VertexArray>();
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(indexBuffer);

    std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        void main() {
            gl_Position = vec4(a_Position, 1.0);
        }
    )";
    std::string fragmentSrc = R"(
        #version 410 core
        out vec4 color;
        void main() {
            color = vec4(0.8, 0.2, 0.3, 1.0); // Red color
        }
    )";
    auto shader = std::make_shared<Engine::Shader>(vertexSrc, fragmentSrc);

    while (app.IsRunning()) {
        Engine::Renderer::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Engine::Renderer::Clear();

        shader->Bind();
        vertexArray->Bind();
        Engine::Renderer::DrawIndexed(vertexArray);

        app.Update(); // Swaps buffers and handles events
    }

    return 0;
}