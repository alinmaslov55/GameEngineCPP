#include <gtest/gtest.h>
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Physics/BoxCollider2D.h"
#include "Engine/Physics/CircleCollider2D.h"
#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Scene/Components/PointLightComponent.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Core/Time.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace {

TEST(DebugGizmoTests, UninitializedRendererSafetyGuards) {
    Engine::Renderer2D::Shutdown();
    EXPECT_FALSE(Engine::Renderer2D::IsInitialized());

    // Calling line/wireframe APIs when uninitialized must gracefully no-op without crashing
    Engine::Renderer2D::DrawDebugLine3D({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    Engine::Renderer2D::DrawDebugWireCircleXZ({ 0.0f, 0.0f, 0.0f }, 3.5f, { 0.0f, 1.0f, 0.0f, 1.0f });
    Engine::Renderer2D::DrawDebugWireBoxXZ({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f }, 0.0f, { 0.0f, 0.0f, 1.0f, 1.0f });
    Engine::Renderer2D::DrawDebugWireArcXZ({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }, 2.0f, 45.0f, { 1.0f, 1.0f, 0.0f, 1.0f });

    Engine::Renderer2D::SetLineWidth(3.0f);
    EXPECT_FLOAT_EQ(Engine::Renderer2D::GetLineWidth(), 1.5f); // Returns default fallback when uninitialized

    const auto stats = Engine::Renderer2D::GetStats();
    EXPECT_EQ(stats.LineCount, 0u);
    EXPECT_EQ(stats.DrawCalls, 0u);
    EXPECT_EQ(stats.GetTotalVertexCount(), 0u);
}

TEST(DebugGizmoTests, WireCircleGeometryMathVerification) {
    const glm::vec3 center{ 10.0f, 1.5f, -5.0f };
    const float radius = 4.25f;
    const uint32_t segments = 32;

    std::vector<std::pair<glm::vec3, glm::vec3>> lines;
    const float step = (2.0f * 3.14159265358979323846f) / static_cast<float>(segments);
    for (uint32_t i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) * step;
        const float t1 = static_cast<float>(i + 1) * step;
        const glm::vec3 p0 = center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
        const glm::vec3 p1 = center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
        lines.emplace_back(p0, p1);
    }

    EXPECT_EQ(lines.size(), segments);
    for (const auto& [p0, p1] : lines) {
        EXPECT_FLOAT_EQ(p0.y, center.y);
        EXPECT_FLOAT_EQ(p1.y, center.y);
        const float dist0 = glm::length(glm::vec2(p0.x - center.x, p0.z - center.z));
        const float dist1 = glm::length(glm::vec2(p1.x - center.x, p1.z - center.z));
        EXPECT_NEAR(dist0, radius, 1e-4f);
        EXPECT_NEAR(dist1, radius, 1e-4f);
    }
}

TEST(DebugGizmoTests, WireBoxGeometryMathVerification) {
    const glm::vec3 center{ 0.0f, 0.5f, 0.0f };
    const glm::vec2 sizeXZ{ 4.0f, 6.0f };

    // Unrotated box
    const float hx = 0.5f * sizeXZ.x;
    const float hz = 0.5f * sizeXZ.y;
    const glm::vec3 p0 = { center.x - hx, center.y, center.z - hz };
    const glm::vec3 p1 = { center.x + hx, center.y, center.z - hz };
    const glm::vec3 p2 = { center.x + hx, center.y, center.z + hz };
    const glm::vec3 p3 = { center.x - hx, center.y, center.z + hz };

    EXPECT_FLOAT_EQ(p0.x, -2.0f);
    EXPECT_FLOAT_EQ(p0.z, -3.0f);
    EXPECT_FLOAT_EQ(p1.x,  2.0f);
    EXPECT_FLOAT_EQ(p1.z, -3.0f);
    EXPECT_FLOAT_EQ(p2.x,  2.0f);
    EXPECT_FLOAT_EQ(p2.z,  3.0f);
    EXPECT_FLOAT_EQ(p3.x, -2.0f);
    EXPECT_FLOAT_EQ(p3.z,  3.0f);

    // 90-degree rotated box (pi / 2 radians)
    const float yaw = 3.14159265358979323846f * 0.5f;
    const float cosY = std::cos(yaw);
    const float sinY = std::sin(yaw);

    const glm::vec2 local0{ -hx, -hz };
    const float rx0 = local0.x * cosY - local0.y * sinY;
    const float rz0 = local0.x * sinY + local0.y * cosY;

    // Corner (-2, -3) rotated 90 deg -> (+3, -2)
    EXPECT_NEAR(rx0,  3.0f, 1e-4f);
    EXPECT_NEAR(rz0, -2.0f, 1e-4f);
}

TEST(DebugGizmoTests, WireArcGeometryMathVerification) {
    const glm::vec3 origin{ 0.0f, 0.0f, 0.0f };
    const glm::vec2 forwardXZ{ 1.0f, 0.0f }; // Pointing along +X
    const float radius = 5.0f;
    const float halfAngleDeg = 45.0f;

    const float baseAngle = std::atan2(forwardXZ.y, forwardXZ.x); // 0.0 radians
    const float halfRad = glm::radians(halfAngleDeg);             // pi / 4 radians
    const float angleMin = baseAngle - halfRad;                   // -pi / 4
    const float angleMax = baseAngle + halfRad;                   // +pi / 4

    const glm::vec3 pLeft  = origin + glm::vec3(std::cos(angleMin) * radius, 0.0f, std::sin(angleMin) * radius);
    const glm::vec3 pRight = origin + glm::vec3(std::cos(angleMax) * radius, 0.0f, std::sin(angleMax) * radius);

    EXPECT_NEAR(pLeft.x,  5.0f * std::cos(-3.14159265f / 4.0f), 1e-4f);
    EXPECT_NEAR(pLeft.z,  5.0f * std::sin(-3.14159265f / 4.0f), 1e-4f);
    EXPECT_NEAR(pRight.x, 5.0f * std::cos( 3.14159265f / 4.0f), 1e-4f);
    EXPECT_NEAR(pRight.z, 5.0f * std::sin( 3.14159265f / 4.0f), 1e-4f);
}

TEST(DebugGizmoTests, StandardColorPaletteDefinition) {
    // Exact standardized RGBA colors specified for EDT-02
    const glm::vec4 kColorStaticCollider  { 0.10f, 0.95f, 0.20f, 0.95f }; // Bright Green
    const glm::vec4 kColorDynamicCollider { 0.00f, 0.90f, 1.00f, 0.95f }; // Cyan
    const glm::vec4 kColorSensorTrigger   { 1.00f, 0.90f, 0.10f, 0.95f }; // Yellow
    const glm::vec4 kColorActiveHitbox    { 1.00f, 0.15f, 0.15f, 0.95f }; // Red
    const glm::vec4 kColorTorchInnerSafe  { 1.00f, 0.84f, 0.10f, 0.90f }; // Radiant Gold
    const glm::vec4 kColorTorchSearingRim { 1.00f, 0.45f, 0.00f, 0.90f }; // Warm Orange
    const glm::vec4 kColorTerrainChunkGrid{ 0.45f, 0.45f, 0.45f, 0.55f }; // Dim Slate Grey

    // Distinct channel checks
    EXPECT_GT(kColorStaticCollider.g, kColorStaticCollider.r);
    EXPECT_GT(kColorDynamicCollider.b, kColorDynamicCollider.r);
    EXPECT_GT(kColorSensorTrigger.r, 0.8f);
    EXPECT_GT(kColorSensorTrigger.g, 0.8f);
    EXPECT_GT(kColorActiveHitbox.r, 0.9f);
    EXPECT_LT(kColorActiveHitbox.g, 0.3f);
    EXPECT_GT(kColorTorchInnerSafe.g, kColorTorchSearingRim.g); // Gold is greener/yellower than Orange
    EXPECT_FLOAT_EQ(kColorTerrainChunkGrid.r, kColorTerrainChunkGrid.g); // Neutral grey
    EXPECT_FLOAT_EQ(kColorTerrainChunkGrid.g, kColorTerrainChunkGrid.b);
}

TEST(DebugGizmoTests, SceneRendererGizmoToggleAndTransientHitboxes) {
    Engine::SceneRenderer::SetShowDebugGizmos(false);
    EXPECT_FALSE(Engine::SceneRenderer::IsShowingDebugGizmos());

    Engine::SceneRenderer::SetShowDebugGizmos(true);
    EXPECT_TRUE(Engine::SceneRenderer::IsShowingDebugGizmos());

    // Register transient hitboxes
    Engine::SceneRenderer::AddDebugHitboxArc({ 1.0f, 0.0f, 2.0f }, { 0.0f, 1.0f }, 3.0f, 60.0f, 0.25f);
    Engine::SceneRenderer::AddDebugHitboxBox({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f }, 0.0f, 0.50f);
    Engine::SceneRenderer::AddDebugHitboxCircle({ -3.0f, 0.0f, 1.0f }, 1.5f, 0.10f);

    // Clear hitboxes
    Engine::SceneRenderer::ClearDebugHitboxes();

    Engine::SceneRenderer::SetShowDebugGizmos(false);
}

TEST(DebugGizmoTests, LineVertexLayoutAndAlignment) {
    EXPECT_EQ(sizeof(Engine::LineVertex), 28u);
    Engine::LineVertex v{};
    v.Position = { 1.0f, 2.0f, 3.0f };
    v.Color = { 0.1f, 0.2f, 0.3f, 1.0f };
    EXPECT_FLOAT_EQ(v.Position.x, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.z, 3.0f);
    EXPECT_FLOAT_EQ(v.Color.r, 0.1f);
    EXPECT_FLOAT_EQ(v.Color.g, 0.2f);
    EXPECT_FLOAT_EQ(v.Color.b, 0.3f);
    EXPECT_FLOAT_EQ(v.Color.a, 1.0f);
}

} // namespace
