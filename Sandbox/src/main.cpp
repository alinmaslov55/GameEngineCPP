#include <Engine/Core/Application.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/VertexArray.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/OrthographicCamera.h>
#include <glm/gtc/matrix_transform.hpp>

int main(int argc, char* argv[]) {
    Engine::Application app;

    // 1. Quad Geometry (Vertices & Indices) stays the same...
    float vertices[4 * 5] = {
        -0.5f, -0.5f, 0.0f,    0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,    1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,    1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,    0.0f, 1.0f
    };
    auto vertexBuffer = std::make_shared<Engine::VertexBuffer>(vertices, static_cast<uint32_t>(sizeof(vertices)));
    uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    auto indexBuffer = std::make_shared<Engine::IndexBuffer>(indices, 6);

    auto vertexArray = std::make_shared<Engine::VertexArray>();
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(indexBuffer);

    // 2. Update Vertex Shader to use matrices
    std::string vertexSrc = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        
        out vec2 v_TexCoord;
        
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        
        void main() {
            v_TexCoord = a_TexCoord;
            // Multiply order matters: Projection * View * Model * VertexPosition
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
    auto shader = std::make_shared<Engine::Shader>(vertexSrc, fragmentSrc);

    // 3. Setup Camera (16:9 Aspect Ratio)
    // -1.6 to 1.6 on X axis, -0.9 to 0.9 on Y axis
    Engine::OrthographicCamera camera(-1.6f, 1.6f, -0.9f, 0.9f);

    // 4. Setup GameObject
    Engine::GameObject player("Player");
    auto spriteRenderer = player.AddComponent<Engine::SpriteRenderer>();
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png"); // Use your path
    spriteRenderer->SetTexture(texture);

    // Let's use the Transform to move and scale the player!
    auto transform = player.GetComponent<Engine::Transform>();
    transform->position = glm::vec3(0.5f, 0.0f, 0.0f); // Move right
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);    // Shrink to 50% size

    shader->Bind();
    shader->SetInt("u_Texture", 0);

    while (app.IsRunning()) {
        Engine::Renderer::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Engine::Renderer::Clear();

        shader->Bind();
        shader->SetFloat4("u_Color", spriteRenderer->GetColor());
        
        // Pass the Camera matrix to the shader
        shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());

        // Calculate the Model (Transform) matrix for the player
        glm::mat4 playerTransformMatrix = glm::translate(glm::mat4(1.0f), transform->position) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.z), glm::vec3(0, 0, 1)) *
                                          glm::scale(glm::mat4(1.0f), transform->scale);
        
        // Pass the Transform matrix to the shader
        shader->SetMat4("u_Transform", playerTransformMatrix);

        if (spriteRenderer->GetTexture()) {
            spriteRenderer->GetTexture()->Bind(0);
        }

        vertexArray->Bind();
        Engine::Renderer::DrawIndexed(vertexArray);

        app.Update(); 
    }

    return 0;
}