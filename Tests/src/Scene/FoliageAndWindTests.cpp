#include <gtest/gtest.h>
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"
#include <cmath>

namespace {

TEST(WindAndMotionTest, WindFieldRootLockProximityPartingAndVolumePreservingSpring) {
    Engine::WindComponent wind;
    wind.ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);

    // 1. Unit wind direction & traveling spatial phase variation across world XZ
    const glm::vec2 dirXZ = wind.GetWindDirectionXZ();
    EXPECT_NEAR(glm::length(dirXZ), 1.0f, 1e-5f);

    const float phaseOrigin = wind.ComputeSpatialPhase({ 0.0f, 0.0f, 0.0f }, 0.0f);
    const float phaseDownwind = wind.ComputeSpatialPhase({ 4.0f, 0.0f, 3.0f }, 0.0f);
    EXPECT_NE(phaseOrigin, phaseDownwind);

    // 2. Bottom standee/shadow vertices (topWeight == 0.0) MUST stay strictly pinned at 0.0m drift!
    for (float t : { 0.0f, 0.37f, 1.15f, 4.82f }) {
        EXPECT_FLOAT_EQ(wind.EvaluateSwayDisplacement(t, 0.0f, phaseDownwind, 0.25f), 0.0f);
    }

    // Top canopy vertices oscillate smoothly in time when enabled, and freeze wind wave in StillAir
    const float swayT0 = wind.EvaluateSwayDisplacement(0.20f, 0.85f, phaseDownwind);
    const float swayT1 = wind.EvaluateSwayDisplacement(1.05f, 0.85f, phaseDownwind);
    EXPECT_NE(swayT0, swayT1);
    EXPECT_GT(std::abs(swayT0) + std::abs(swayT1), 0.02f);

    wind.ApplyPreset(Engine::WindComponent::Preset::StillAir);
    EXPECT_FALSE(wind.Enabled);
    EXPECT_NEAR(wind.EvaluateSwayDisplacement(0.75f, 0.85f, phaseDownwind, 0.0f), 0.0f, 1e-6f);

    // 3. Interactive proximity parting pushes foliage away along SwayRightAxis inside radius & 0 outside
    wind.ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);
    const glm::vec3 bushAnchor = { 5.0f, 0.0f, 5.0f };
    const glm::vec3 camRightXZ = { 1.0f, 0.0f, 0.0f };

    // Player slightly to the left of the bush -> bush bends right (+X)
    const float bendRight = wind.EvaluateProximityBend(bushAnchor, { 4.55f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);
    // Player slightly to the right of the bush -> bush bends left (-X)
    const float bendLeft  = wind.EvaluateProximityBend(bushAnchor, { 5.45f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);
    // Player outside interaction radius -> zero bend
    const float bendFar   = wind.EvaluateProximityBend(bushAnchor, { 8.00f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);

    EXPECT_GT(bendRight, 0.05f);
    EXPECT_LT(bendLeft, -0.05f);
    EXPECT_FLOAT_EQ(bendFar, 0.0f);

    // 4. SquashStretchComponent: volume-preserving spring (sx * sqrt(sy) == 1) & physics scale isolation
    Engine::Scene testScene("MotionTestScene");
    Engine::GameObject* player = testScene.CreateGameObject("Player");
    player->GetTransform()->position = { 2.0f, 0.0f, 3.0f };
    player->GetTransform()->scale = { 1.25f, 1.35f, 1.0f };
    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->useGravity = true;
    auto* ss = player->AddComponent<Engine::SquashStretchComponent>();

    ss->TriggerImpulse(2.4f, 8.0f);
    ss->Update(1.0f / 60.0f);

    const glm::vec2 stretched = ss->GetScaleMultiplier();
    EXPECT_GT(stretched.y, 1.01f); // Stretched vertically
    EXPECT_LT(stretched.x, 0.99f); // Narrowed horizontally to conserve volume
    EXPECT_NEAR(stretched.x * std::sqrt(stretched.y), 1.0f, 1e-5f);
    // Underlying physics Transform::scale MUST remain completely untouched!
    EXPECT_FLOAT_EQ(player->GetTransform()->scale.x, 1.25f);
    EXPECT_FLOAT_EQ(player->GetTransform()->scale.y, 1.35f);

    // Settle spring over 1.5 seconds -> converges cleanly back to {1.0, 1.0} and 0 deg tilt
    for (int step = 0; step < 90; ++step) {
        ss->Update(1.0f / 60.0f);
    }
    const glm::vec2 settled = ss->GetScaleMultiplier();
    EXPECT_NEAR(settled.x, 1.0f, 1e-3f);
    EXPECT_NEAR(settled.y, 1.0f, 1e-3f);
    EXPECT_NEAR(ss->GetVisualTiltDegrees(), 0.0f, 1e-2f);

    // Simulate airborne fall -> ground touchdown -> auto-triggers downward landing squash (sy < 1.0)
    player->GetTransform()->position.y = 0.8f;
    ss->Update(1.0f / 60.0f);
    player->GetTransform()->position.y = 0.4f;
    ss->Update(1.0f / 60.0f);
    player->GetTransform()->position.y = 0.0f; // Touchdown!
    ss->Update(1.0f / 60.0f);
    ss->Update(1.0f / 60.0f);
    const glm::vec2 squashed = ss->GetScaleMultiplier();
    EXPECT_LT(squashed.y, 0.99f);
    EXPECT_GT(squashed.x, 1.005f);
    EXPECT_NEAR(squashed.x * std::sqrt(squashed.y), 1.0f, 1e-5f);
}

} // namespace

