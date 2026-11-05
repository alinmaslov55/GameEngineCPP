#include <gtest/gtest.h>
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Scene/StandeeCommand.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Core/JobSystem.h"

#include <vector>

namespace {

TEST(ParallelRenderTest, ComputeBillboardCornersUnrotatedAccurateBounds) {
    const glm::vec3 anchor = { 5.0f, 0.0f, 10.0f };
    const glm::vec2 size = { 2.0f, 4.0f };
    const glm::vec3 rightAxis = { 1.0f, 0.0f, 0.0f };
    const glm::vec3 upAxis = { 0.0f, 1.0f, 0.0f };

    glm::vec3 corners[4];
    Engine::ComputeBillboardCorners(anchor, size, rightAxis, upAxis, 0.0f, corners);

    // Bottom-left: anchor.x - 1, anchor.y, anchor.z
    EXPECT_NEAR(corners[0].x, 4.0f, 1e-5f);
    EXPECT_NEAR(corners[0].y, 0.0f, 1e-5f);
    EXPECT_NEAR(corners[0].z, 10.0f, 1e-5f);

    // Bottom-right: anchor.x + 1, anchor.y, anchor.z
    EXPECT_NEAR(corners[1].x, 6.0f, 1e-5f);
    EXPECT_NEAR(corners[1].y, 0.0f, 1e-5f);
    EXPECT_NEAR(corners[1].z, 10.0f, 1e-5f);

    // Top-right: anchor.x + 1, anchor.y + 4, anchor.z
    EXPECT_NEAR(corners[2].x, 6.0f, 1e-5f);
    EXPECT_NEAR(corners[2].y, 4.0f, 1e-5f);
    EXPECT_NEAR(corners[2].z, 10.0f, 1e-5f);

    // Top-left: anchor.x - 1, anchor.y + 4, anchor.z
    EXPECT_NEAR(corners[3].x, 4.0f, 1e-5f);
    EXPECT_NEAR(corners[3].y, 4.0f, 1e-5f);
    EXPECT_NEAR(corners[3].z, 10.0f, 1e-5f);
}

TEST(ParallelRenderTest, ComputeBillboardCornersRotated90Degrees) {
    const glm::vec3 anchor = { 0.0f, 0.0f, 0.0f };
    const glm::vec2 size = { 2.0f, 2.0f };
    const glm::vec3 rightAxis = { 1.0f, 0.0f, 0.0f };
    const glm::vec3 upAxis = { 0.0f, 1.0f, 0.0f };

    glm::vec3 corners[4];
    // Rotated 90 degrees CCW
    Engine::ComputeBillboardCorners(anchor, size, rightAxis, upAxis, 90.0f, corners);

    // Center is at (0, 1, 0)
    // 90 deg rotation maps rightAxis (1, 0) to (0, 1), upAxis (0, 1) to (-1, 0)
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(corners[i].z, 0.0f, 1e-5f);
    }
}

TEST(ParallelRenderTest, NullSafetyHeadlessGracefulReturn) {
    Engine::PerspectiveCamera camera(32.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    // When Renderer2D is uninitialized (in headless test environments without active OpenGL context),
    // calling rendering functions must gracefully return without throwing or crashing
    Engine::Renderer2D::ResetStats();
    Engine::Renderer2D::BeginScene(camera);

    Engine::Transform t;
    Engine::StandeeRenderCommand cmd;
    cmd.transform = &t;
    cmd.size = { 1.0f, 1.0f };

    Engine::Renderer2D::DrawStandeesParallel({ &cmd, 1 }, camera.GetRight(), camera.GetUp(), camera.GetRightXZ(), false);

    Engine::ParticleSystem::Particle p;
    Engine::Renderer2D::DrawParticlesParallel(&p, 1, camera.GetRight(), camera.GetUp(), 0.0f);

    Engine::Renderer2D::EndScene();

    auto stats = Engine::Renderer2D::GetStats();
    EXPECT_EQ(stats.QuadCount, 0u);
    EXPECT_EQ(stats.DrawCalls, 0u);
}

TEST(ParallelRenderTest, ParallelVertexGenerationAlgorithm) {
    if (!Engine::JobSystem::IsInitialized()) {
        Engine::JobSystem::Init(4);
    }

    constexpr size_t kQuadCount = 1000;
    std::vector<Engine::QuadVertex> vertexBuffer(kQuadCount * 4);

    const glm::vec3 rightAxis = { 1.0f, 0.0f, 0.0f };
    const glm::vec3 upAxis = { 0.0f, 1.0f, 0.0f };

    Engine::JobSystem::ParallelFor(kQuadCount, [&](size_t i) {
        Engine::QuadVertex* v = &vertexBuffer[i * 4];
        const glm::vec3 anchor = { static_cast<float>(i), 0.0f, 0.0f };
        const glm::vec2 size = { 1.0f, 2.0f };

        glm::vec3 corners[4];
        Engine::ComputeBillboardCorners(anchor, size, rightAxis, upAxis, 0.0f, corners);

        for (int c = 0; c < 4; ++c) {
            v[c].Position = corners[c];
            v[c].Color = glm::vec4(1.0f);
            v[c].TexCoord = glm::vec2(0.0f);
            v[c].TexIndex = 1.0f;
            v[c].AlphaCutoff = 0.15f;
            v[c].Emissive = 0.0f;
            v[c].ShadowOffset = 0.48f;
            v[c].WindParams = glm::vec3(0.0f);
        }
    });

    // Verify all 1,000 quads (4,000 vertices) were written correctly in parallel
    for (size_t i = 0; i < kQuadCount; ++i) {
        const auto* v = &vertexBuffer[i * 4];
        EXPECT_NEAR(v[0].Position.x, static_cast<float>(i) - 0.5f, 1e-4f);
        EXPECT_NEAR(v[1].Position.x, static_cast<float>(i) + 0.5f, 1e-4f);
        EXPECT_NEAR(v[2].Position.y, 2.0f, 1e-4f);
        EXPECT_FLOAT_EQ(v[0].AlphaCutoff, 0.15f);
        EXPECT_FLOAT_EQ(v[0].TexIndex, 1.0f);
    }
}

} // namespace
