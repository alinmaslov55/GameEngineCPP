#include <gtest/gtest.h>
#include "Engine/Renderer/RHI/Buffer.h"
#include "Engine/Renderer/RHI/Framebuffer.h"
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/RHI/Shader.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/Texture2DArray.h"
#include "Engine/Renderer/RHI/VertexArray.h"
#include "Engine/Renderer/RHI/IndexBuffer.h"
#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace {

TEST(RendererValidationTest, InvalidShaderSuppressesUniformLogSpamAndHandlesEmptyTypeTag) {
    Engine::Logger::Init();
    Engine::Logger::ClearLogs();

    // Empty #type\n and #type   \n should not underflow PreProcess substr lengths or crash
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_shader_preprocess_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path badShaderPath = tempDir / "empty_type.glsl";
    {
        std::ofstream out(badShaderPath);
        out << "#type\nvoid main() {}\n#type   \nvoid main() {}\n";
    }

    Engine::Shader badShader(badShaderPath.string());
    EXPECT_FALSE(badShader.IsValid());
    EXPECT_EQ(badShader.GetRendererID(), 0u);

    Engine::Logger::ClearLogs();
    EXPECT_EQ(badShader.GetUniformLocation("u_ViewProjection"), -1);
    EXPECT_FALSE(badShader.HasUniform("u_Color"));
    badShader.SetInt("u_Texture", 0);
    badShader.SetFloat("u_Time", 1.0f);
    badShader.SetMat4("u_Transform", glm::mat4(1.0f));

    // Must not flood logs with "Shader ID 0: Uniform ... not found or unused"
    for (const auto& entry : Engine::Logger::GetRecentLogs()) {
        EXPECT_EQ(entry.Message.find("Shader ID 0: Uniform"), std::string::npos) << entry.Message;
    }

    std::filesystem::remove_all(tempDir, ec);
}

TEST(RendererValidationTest, Texture2DArrayMoveZeroesDimensionsAndPreventsMovedFromOOBWrite) {
    Engine::Texture2DArray src(8, 8, 4, true, true);
    EXPECT_EQ(src.GetLayers(), 4u);

    Engine::Texture2DArray dst(std::move(src));
    EXPECT_EQ(dst.GetLayers(), 4u);
    EXPECT_EQ(src.GetLayers(), 0u);
    EXPECT_EQ(src.GetWidth(), 0u);
    EXPECT_EQ(src.GetHeight(), 0u);

    const std::vector<uint8_t> pixels(8u * 8u * 4u, 255u);
    // Invoking SetLayerData on moved-from Texture2DArray must safely no-op without OOB write
    src.SetLayerData(0, pixels.data(), false);
    src.SetLayerData(3, pixels.data(), false);
    EXPECT_FALSE(src.HasUploadedLayer(0));
    EXPECT_FALSE(src.HasUploadedLayer(3));

    Engine::Texture2DArray reassigned(4, 4, 2, true, true);
    reassigned = std::move(dst);
    EXPECT_EQ(reassigned.GetLayers(), 4u);
    EXPECT_EQ(dst.GetLayers(), 0u);
    dst.SetLayerData(0, pixels.data(), false);
    EXPECT_FALSE(dst.HasUploadedLayer(0));
}

TEST(RendererValidationTest, ZeroDimensionTextureAndFramebufferDoNotReportValidOrLoaded) {
    const uint32_t dummyPixel = 0xFFFFFFFFu;
    Engine::Texture zeroTex(0, 0, &dummyPixel, false, false);
    EXPECT_FALSE(zeroTex.IsLoaded());
    EXPECT_EQ(zeroTex.GetRendererID(), 0u);

    // Non-zero dimensions in headless mode (no active GL context) should also leave IsLoaded() false
    Engine::Texture headlessTex(4, 4, &dummyPixel, false, false);
    EXPECT_FALSE(headlessTex.IsLoaded());
    EXPECT_EQ(headlessTex.GetRendererID(), 0u);

    Engine::FramebufferSpecification zeroSpec;
    zeroSpec.Width = 0;
    zeroSpec.Height = 0;
    Engine::Framebuffer fbo(zeroSpec);
    EXPECT_EQ(fbo.GetColorAttachmentRendererID(), 0u);
    EXPECT_EQ(fbo.GetSpecification().Width, 0u);
    fbo.Resize(0, 128);
    fbo.Resize(128, 0);
    fbo.Bind();
    fbo.Unbind();
}

TEST(RendererValidationTest, VertexArrayAccumulatesAttributeIndicesAndHandlesNullIndexBuffer) {
    Engine::VertexArray vao;

    auto vbo0 = std::make_shared<Engine::VertexBuffer>(static_cast<uint32_t>(sizeof(float) * 4));
    vbo0->SetLayout({
        { Engine::ShaderDataType::Float3, "a_Position" },
        { Engine::ShaderDataType::Int,    "a_EntityID" }
    });

    auto vbo1 = std::make_shared<Engine::VertexBuffer>(static_cast<uint32_t>(sizeof(float) * 18));
    vbo1->SetLayout({
        { Engine::ShaderDataType::Float2, "a_TexCoord" },
        { Engine::ShaderDataType::Mat4,   "a_InstanceTransform" }
    });

    vao.AddVertexBuffer(vbo0);
    EXPECT_EQ(vao.GetAttributeCount(), 2u);

    vao.AddVertexBuffer(vbo1);
    // 2 (Float3 + Int) + 1 (Float2) + 4 (Mat4 columns) = 7 cumulative attribute slots
    EXPECT_EQ(vao.GetAttributeCount(), 7u);
    EXPECT_EQ(vao.GetVertexBuffers().size(), 2u);

    vao.SetIndexBuffer(nullptr);
    EXPECT_EQ(vao.GetIndexBuffer(), nullptr);
}

TEST(RendererValidationTest, Renderer2DAndPostProcessorUninitializedSafetyGuards) {
    Engine::Renderer2D::Shutdown();
    // DrawQuad forwards directly into SubmitQuad; EndScene calls Flush()
    Engine::Renderer2D::DrawQuad(glm::mat4(1.0f), glm::vec4(1.0f));
    Engine::Renderer2D::EndScene();

    Engine::PostProcessor::Shutdown();
    EXPECT_FALSE(Engine::PostProcessor::IsValid());
}

TEST(RendererValidationTest, ShaderDataTypeBoolDoesNotMapToInvalidGLBool) {
    Engine::VertexArray vao;
    auto vbo = std::make_shared<Engine::VertexBuffer>(static_cast<uint32_t>(sizeof(uint8_t) * 4));
    vbo->SetLayout({
        { Engine::ShaderDataType::Bool, "a_ActiveFlag" }
    });
    // AddVertexBuffer internally processes ShaderDataType::Bool
    vao.AddVertexBuffer(vbo);
    EXPECT_EQ(vao.GetAttributeCount(), 1u);
}

TEST(RendererValidationTest, HeadlessRendererAPICallsDoNotCrash) {
    // These methods should have null guards for glad_gl* function pointers
    Engine::Renderer::SetViewport(0, 0, 1920, 1080);
    Engine::Renderer::SetDepthTest(true);
    Engine::Renderer::SetDepthTest(false);
    Engine::Renderer::SetDepthMask(true);
    Engine::Renderer::SetDepthMask(false);
    Engine::Renderer::SetPolygonOffset(true, 1.0f, 1.0f);
    Engine::Renderer::SetPolygonOffset(false, 0.0f, 0.0f);
    Engine::Renderer::SetClearColor({ 0.1f, 0.2f, 0.3f, 1.0f });
    Engine::Renderer::Clear();

    // DrawIndexed with null vertex array or null index buffer must safely return
    Engine::Renderer::DrawIndexed(nullptr, 0);

    auto vao = std::make_shared<Engine::VertexArray>();
    Engine::Renderer::DrawIndexed(vao, 0); // null index buffer, zero index count
}

TEST(RendererValidationTest, IndexBufferCreationPreservesBoundVAOAndIndexBuffer) {
    auto vaoA = std::make_shared<Engine::VertexArray>();
    const uint32_t indicesA[3] = { 0, 1, 2 };
    auto ibA = std::make_shared<Engine::IndexBuffer>(indicesA, 3);
    vaoA->SetIndexBuffer(ibA);

    vaoA->Bind();
    EXPECT_EQ(vaoA->GetIndexBuffer(), ibA);

    // Construct an unrelated IndexBuffer B while vaoA is bound
    const uint32_t indicesB[6] = { 0, 1, 2, 2, 3, 0 };
    auto ibB = std::make_shared<Engine::IndexBuffer>(indicesB, 6);

    // VAO A's bound index buffer pointer must not be clobbered
    EXPECT_EQ(vaoA->GetIndexBuffer(), ibA);
    EXPECT_EQ(ibB->GetCount(), 6u);

    ibB->Unbind();
    EXPECT_EQ(vaoA->GetIndexBuffer(), ibA);
    vaoA->Unbind();
}

TEST(RendererValidationTest, HeadlessRenderer2DBeginSceneDrawAndFlushDoesNotCrash) {
    Engine::Renderer2D::Init();
    Engine::Renderer2D::ResetStats();

    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    Engine::Renderer2D::BeginScene(cam, true, 0.1f);
    Engine::Renderer2D::DrawQuad(glm::mat4(1.0f), { 1.0f, 0.5f, 0.25f, 1.0f });
    Engine::Renderer2D::EndScene();

    const auto stats = Engine::Renderer2D::GetStats();
    EXPECT_EQ(stats.QuadCount, 1u);
    EXPECT_GE(stats.DrawCalls, 1u);

    Engine::Renderer2D::Shutdown();
}

} // namespace
