#include <gtest/gtest.h>
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Scene/StandeeCommand.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Core/JobSystem.h"

#include <vector>
#include <unordered_set>
#include "Engine/Scene/GameObject.h"

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

    Engine::JobSystem::Shutdown();
}

TEST(ParallelRenderTest, ThreadSafeUUIDGenerationAcrossThreads) {
    if (!Engine::JobSystem::IsInitialized()) {
        Engine::JobSystem::Init(4);
    }

    constexpr size_t kTotalUUIDs = 10000;
    std::vector<uint64_t> generatedUUIDs(kTotalUUIDs, 0);

    Engine::JobSystem::ParallelFor(kTotalUUIDs, [&](size_t i) {
        Engine::GameObject go("ConcurrentEntity");
        generatedUUIDs[i] = go.GetUUID();
    });

    std::unordered_set<uint64_t> uniqueUUIDs;
    uniqueUUIDs.reserve(kTotalUUIDs);
    for (size_t i = 0; i < kTotalUUIDs; ++i) {
        EXPECT_NE(generatedUUIDs[i], 0ULL);
        uniqueUUIDs.insert(generatedUUIDs[i]);
    }

    EXPECT_EQ(uniqueUUIDs.size(), kTotalUUIDs);

    Engine::JobSystem::Shutdown();
}

TEST(ParallelRenderTest, ParallelStandeeVertexGenerationWithParentedHierarchyAndSubFrameInterpolation) {
    // 1. Verify parented hierarchy sub-frame interpolation
    Engine::Transform root;
    root.prevPosition = { 0.0f, 0.0f, 0.0f };
    root.position = { 10.0f, 0.0f, 0.0f };

    Engine::Transform child;
    child.prevPosition = { 5.0f, 0.0f, 0.0f };
    child.position = { 5.0f, 2.0f, 0.0f };
    child.SetParent(&root, /*keepWorldTransform=*/false);

    // At alpha = 0.5f:
    // Root interpolated world pos: (5.0, 0.0, 0.0)
    // Child interpolated local pos: (5.0, 1.0, 0.0)
    // Child interpolated world pos: (10.0, 1.0, 0.0)
    glm::vec3 interpWorldPos = child.GetInterpolatedWorldPosition(0.5f);
    EXPECT_NEAR(interpWorldPos.x, 10.0f, 1e-4f);
    EXPECT_NEAR(interpWorldPos.y, 1.0f, 1e-4f);
    EXPECT_NEAR(interpWorldPos.z, 0.0f, 1e-4f);

    // 2. Parallel vertex generation without accessing Transform mutable state
    if (!Engine::JobSystem::IsInitialized()) {
        Engine::JobSystem::Init(4);
    }

    constexpr size_t kCommandCount = 1000;
    std::vector<Engine::StandeeRenderCommand> commands(kCommandCount);
    for (size_t i = 0; i < kCommandCount; ++i) {
        commands[i].transform = &child;
        commands[i].worldPosition = interpWorldPos + glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        commands[i].worldRotation = child.GetInterpolatedWorldRotation(0.5f);
        commands[i].scaleZ = child.scale.z;
        commands[i].size = { 2.0f, 3.0f };
        commands[i].rotationZ = 0.0f;
    }

    std::vector<Engine::QuadVertex> vertexBuffer(kCommandCount * 4);
    const glm::vec3 rightAxis = { 1.0f, 0.0f, 0.0f };
    const glm::vec3 upAxis = { 0.0f, 1.0f, 0.0f };

    // Parallel execution purely on POD command fields
    Engine::JobSystem::ParallelFor(kCommandCount, [&](size_t i) {
        const auto& cmd = commands[i];
        glm::vec3 corners[4];
        Engine::ComputeBillboardCorners(cmd.worldPosition, cmd.size, rightAxis, upAxis, cmd.rotationZ, corners);

        for (int c = 0; c < 4; ++c) {
            vertexBuffer[i * 4 + c].Position = corners[c];
        }
    });

    for (size_t i = 0; i < kCommandCount; ++i) {
        const float expectedCenterX = 10.0f + static_cast<float>(i);
        EXPECT_NEAR(vertexBuffer[i * 4 + 0].Position.x, expectedCenterX - 1.0f, 1e-4f);
        EXPECT_NEAR(vertexBuffer[i * 4 + 1].Position.x, expectedCenterX + 1.0f, 1e-4f);
        EXPECT_NEAR(vertexBuffer[i * 4 + 2].Position.y, 1.0f + 3.0f, 1e-4f);
    }

    Engine::JobSystem::Shutdown();
}

} // namespace
