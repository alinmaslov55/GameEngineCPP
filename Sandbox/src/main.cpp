#include <Engine/Core/Application.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/VertexArray.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/SpriteRenderer.h>

int main(int argc, char* argv[]) {
    Engine::Application app;

    float vertices[4 * 5] = {
        // Position            // TexCoords
        -0.5f, -0.5f, 0.0f,    0.0f, 0.0f,  // Bottom-Left
         0.5f, -0.5f, 0.0f,    1.0f, 0.0f,  // Bottom-Right
         0.5f,  0.5f, 0.0f,    1.0f, 1.0f,  // Top-Right
        -0.5f,  0.5f, 0.0f,    0.0f, 1.0f   // Top-Left
    };
    auto vertexBuffer = std::make_shared<Engine::VertexBuffer>(
        vertices, 
        static_cast<uint32_t>(sizeof(vertices))
    );
    uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    auto indexBuffer = std::make_shared<Engine::IndexBuffer>(indices, 6);

    auto vertexArray = std::make_shared<Engine::VertexArray>();
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(indexBuffer);

    std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        
        out vec2 v_TexCoord;
        
        void main() {
            v_TexCoord = a_TexCoord;
            gl_Position = vec4(a_Position, 1.0);
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
    auto shader = std::make_shared<Engine::Shader>(vertexSrc, fragmentSrc);

    Engine::GameObject player("Player");
    auto spriteRenderer = player.AddComponent<Engine::SpriteRenderer>();
    
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png");
    spriteRenderer->SetTexture(texture);
    spriteRenderer->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // No tint

    shader->Bind();
    shader->SetInt("u_Texture", 0);
    
    while (app.IsRunning()) {
        Engine::Renderer::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Engine::Renderer::Clear();

        shader->Bind();
        shader->SetFloat4("u_Color", spriteRenderer->GetColor()); // Apply component color
        
        if (spriteRenderer->GetTexture()) {
            spriteRenderer->GetTexture()->Bind(0); // Bind component texture
        }

        vertexArray->Bind();
        Engine::Renderer::DrawIndexed(vertexArray);

        app.Update(); 
    }

    return 0;
}