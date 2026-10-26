#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/VertexArray.h"
#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

namespace Engine {

struct Renderer2DStorage {
    std::shared_ptr<VertexArray> QuadVertexArray;
    std::shared_ptr<Shader> TextureShader;
    uint32_t WhiteTexture = 0;
};

static Renderer2DStorage* s_Data{nullptr};

void Renderer2D::Init() {
    ENGINE_CORE_INFO("Initializing Renderer2D...");
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
        uniform vec4 u_TexBounds; // xy = min, zw = max
        void main() {
            // Mix interpolates between min and max based on the base 0-1 quad coordinates
            v_TexCoord = vec2(
                mix(u_TexBounds.x, u_TexBounds.z, a_TexCoord.x),
                mix(u_TexBounds.y, u_TexBounds.w, a_TexCoord.y)
            );
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

    // Create 1x1 white texture for solid color rendering
    glGenTextures(1, &s_Data->WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uint32_t whiteData = 0xffffffff;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteData);
    glBindTexture(GL_TEXTURE_2D, 0);

    ENGINE_CORE_INFO("Renderer2D initialized successfully");
}

void Renderer2D::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Renderer2D...");
    if (s_Data->WhiteTexture) {
        glDeleteTextures(1, &s_Data->WhiteTexture);
    }
    delete s_Data;
    s_Data = nullptr;
    ENGINE_CORE_INFO("Renderer2D shut down");
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    ENGINE_CORE_TRACE("Renderer2D::BeginScene");
    Renderer::SetDepthTest(false);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
}

void Renderer2D::BeginScene(const glm::mat4& projectionMatrix) {
    ENGINE_CORE_TRACE("Renderer2D::BeginScene (Projection Matrix)");
    Renderer::SetDepthTest(false);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", projectionMatrix);
}

void Renderer2D::EndScene() {
    ENGINE_CORE_TRACE("Renderer2D::EndScene");
    // Placeholder for future batch rendering flush
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color) {
    DrawQuad(transform, std::shared_ptr<Texture>(nullptr), color);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture>& texture, const glm::vec4& tintColor) {
    ENGINE_CORE_TRACE("Renderer2D::DrawQuad");
    s_Data->TextureShader->SetFloat4("u_Color", tintColor);
    s_Data->TextureShader->SetMat4("u_Transform", transform);
    s_Data->TextureShader->SetFloat4("u_TexBounds", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    
    if (texture) {
        texture->Bind(0);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
    }

    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray);
}

void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<SubTexture2D>& subTexture, const glm::vec4& tintColor) {
    ENGINE_CORE_TRACE("Renderer2D::DrawQuad (SubTexture2D)");
    s_Data->TextureShader->SetFloat4("u_Color", tintColor);
    s_Data->TextureShader->SetMat4("u_Transform", transform);
    
    if (subTexture) {
        s_Data->TextureShader->SetFloat4("u_TexBounds", glm::vec4(
            subTexture->GetMin().x, subTexture->GetMin().y, 
            subTexture->GetMax().x, subTexture->GetMax().y
        ));
        subTexture->GetTexture()->Bind(0);
    } else {
        s_Data->TextureShader->SetFloat4("u_TexBounds", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_Data->WhiteTexture);
    }

    s_Data->QuadVertexArray->Bind();
    Renderer::DrawIndexed(s_Data->QuadVertexArray);
}

} // namespace Engine
