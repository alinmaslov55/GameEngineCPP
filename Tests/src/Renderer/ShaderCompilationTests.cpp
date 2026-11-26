#include <gtest/gtest.h>
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include "Engine/Renderer/RHI/Shader.h"
#include <filesystem>
#include <memory>

namespace {

struct ScopedGLContext {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool valid = false;

    ScopedGLContext() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        window = SDL_CreateWindow("ShaderValidation", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window) {
            return;
        }
        context = SDL_GL_CreateContext(window);
        if (!context) {
            SDL_DestroyWindow(window);
            window = nullptr;
            return;
        }
        SDL_GL_MakeCurrent(window, context);
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            SDL_GL_DestroyContext(context);
            SDL_DestroyWindow(window);
            window = nullptr;
            context = nullptr;
            return;
        }
        valid = true;
    }

    ~ScopedGLContext() {
        if (context) {
            SDL_GL_DestroyContext(context);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

TEST(ShaderCompilationTest, TerrainSplatmapCompilesAndLinksCleanlyWithValidUniforms) {
    ScopedGLContext gl;
    if (!gl.valid) {
        GTEST_SKIP() << "No OpenGL context available for live shader compilation test.";
    }

    const std::filesystem::path shaderPath = "Sandbox/assets/shaders/TerrainSplatmap.glsl";
    ASSERT_TRUE(std::filesystem::exists(shaderPath)) << "Shader path does not exist: " << shaderPath.string();

    Engine::Shader shader(shaderPath.string());
    ASSERT_NE(shader.GetRendererID(), 0u) << "TerrainSplatmap.glsl failed to compile or link!";

    // Verify all primary terrain uniforms resolve to valid locations
    EXPECT_NE(shader.GetUniformLocation("u_ViewProjection"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_TerrainOrigin"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_TerrainSizeXZ"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_SplatWeights0"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_SplatWeights1"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_AlbedoHeightArray"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_NormalRoughnessArray"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_ShadowMap"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_TorchShadowMap"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_LayerTilingScales"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_BlendSharpness"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_EdgeDarkening"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_CameraPos"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_LightingEnabled"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_SunDirection"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_ShadowsEnabled"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_TorchShadowsEnabled"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_BrushEnabled"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_LayerGlossiness"), -1);
}

TEST(ShaderCompilationTest, Batch2DCompilesAndLinksCleanly) {
    ScopedGLContext gl;
    if (!gl.valid) {
        GTEST_SKIP() << "No OpenGL context available for live shader compilation test.";
    }

    const std::filesystem::path shaderPath = "Sandbox/assets/shaders/Batch2D.glsl";
    ASSERT_TRUE(std::filesystem::exists(shaderPath));

    Engine::Shader shader(shaderPath.string());
    ASSERT_NE(shader.GetRendererID(), 0u) << "Batch2D.glsl failed to compile or link!";
    EXPECT_NE(shader.GetUniformLocation("u_ViewProjection"), -1);
    EXPECT_NE(shader.GetUniformLocation("u_Textures"), -1);
}

TEST(ShaderCompilationTest, PostProcessShadersCompileAndLinkCleanly) {
    ScopedGLContext gl;
    if (!gl.valid) {
        GTEST_SKIP() << "No OpenGL context available for live shader compilation test.";
    }

    {
        const std::filesystem::path path = "Sandbox/assets/shaders/PostProcess_BloomDownsample.glsl";
        ASSERT_TRUE(std::filesystem::exists(path));
        Engine::Shader shader(path.string());
        ASSERT_NE(shader.GetRendererID(), 0u) << "PostProcess_BloomDownsample.glsl failed to compile or link!";
        EXPECT_NE(shader.GetUniformLocation("u_SrcTexture"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_SrcTexelSize"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_MipLevel"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_Threshold"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_Knee"), -1);
    }

    {
        const std::filesystem::path path = "Sandbox/assets/shaders/PostProcess_BloomUpsample.glsl";
        ASSERT_TRUE(std::filesystem::exists(path));
        Engine::Shader shader(path.string());
        ASSERT_NE(shader.GetRendererID(), 0u) << "PostProcess_BloomUpsample.glsl failed to compile or link!";
        EXPECT_NE(shader.GetUniformLocation("u_SrcTexture"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_TexelRadius"), -1);
    }

    {
        const std::filesystem::path path = "Sandbox/assets/shaders/PostProcess_Composite.glsl";
        ASSERT_TRUE(std::filesystem::exists(path));
        Engine::Shader shader(path.string());
        ASSERT_NE(shader.GetRendererID(), 0u) << "PostProcess_Composite.glsl failed to compile or link!";
        EXPECT_NE(shader.GetUniformLocation("u_SceneHDR"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_BloomTexture"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_BloomEnabled"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_BloomIntensity"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_ToneMapMode"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_Exposure"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_VignetteEnabled"), -1);
        EXPECT_NE(shader.GetUniformLocation("u_AspectRatio"), -1);
    }
}

} // namespace
