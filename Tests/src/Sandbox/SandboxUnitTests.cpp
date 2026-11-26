#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/CameraController.h"
#include "Utils/FilenameUtils.h"

namespace {

TEST(SandboxUnitTest, SanitizeFilenameHandlesInvalidCharsAndReservedNames) {
    EXPECT_EQ(Sandbox::SanitizeFilename("  My:Prop*Name?  .. "), "My_Prop_Name_");
    EXPECT_EQ(Sandbox::SanitizeFilename("a/b\\c<d>e|f\"g"), "a_b_c_d_e_f_g");
    EXPECT_EQ(Sandbox::SanitizeFilename("   ...  "), "GameObject");
    EXPECT_EQ(Sandbox::SanitizeFilename(""), "GameObject");
    EXPECT_EQ(Sandbox::SanitizeFilename("con"), "_con");
    EXPECT_EQ(Sandbox::SanitizeFilename("COM1.prefab"), "_COM1.prefab");
    EXPECT_EQ(Sandbox::SanitizeFilename("NUL"), "_NUL");
    EXPECT_EQ(Sandbox::SanitizeFilename("RitualBrazier_01"), "RitualBrazier_01");
}

TEST(SandboxUnitTest, PlayerControllerAwakeAndIdleFixedUpdate) {
    Engine::Scene scene("PlayerControllerTestScene");
    auto* player = scene.CreateGameObject("Player");
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    player->AddComponent<Engine::CircleCollider2D>();
    auto* controller = player->AddComponent<PlayerController>();

    scene.OnRuntimeStart();
    EXPECT_EQ(controller->GetMovementInput(), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(controller->m_DustProps.lifeTime, 0.45f);

    rb->SetVelocityXZ(4.0f, 0.0f);
    scene.UpdateRuntime(1.0f / 60.0f);
    // Idle braking reduces velocity toward 0
    EXPECT_LT(glm::length(rb->GetVelocityXZ()), 4.0f);
    scene.OnRuntimeStop();
}

TEST(SandboxUnitTest, CameraControllerDeadzoneAndSmoothFollow) {
    Engine::Scene scene("CameraControllerTestScene");
    auto* player = scene.CreateGameObject("Player");
    player->GetTransform()->position = { 5.0f, 0.0f, -3.0f };

    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 0.0f });

    auto* camCtrl = player->AddComponent<CameraController>();
    camCtrl->camera = &cam;
    camCtrl->deadzone = { 1.4f, 1.0f };
    camCtrl->smoothSpeed = 10.0f;

    camCtrl->Update(0.1f);
    const glm::vec3 focalAfter = cam.GetFocalPoint();
    EXPECT_GT(focalAfter.x, 0.0f);
    EXPECT_LT(focalAfter.z, 0.0f);
    EXPECT_FLOAT_EQ(focalAfter.y, 0.0f);
}

TEST(SandboxUnitTest, PlayerControllerDodgeRollCooldownInvulnAndVaulting) {
    Engine::Scene scene("PlayerDodgeTestScene");
    auto* player = scene.CreateGameObject("Player");
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->UseGravityY = true;
    player->AddComponent<Engine::CircleCollider2D>()->Radius = 0.4f;
    auto* ps = player->AddComponent<Engine::ParticleSystem>(64);
    auto* controller = player->AddComponent<PlayerController>();

    scene.OnRuntimeStart();
    EXPECT_FALSE(controller->IsDodging());
    EXPECT_FALSE(controller->IsInvulnerable());

    EXPECT_TRUE(controller->TriggerDodgeRoll({ 1.0f, 0.0f }));
    EXPECT_TRUE(controller->IsDodging());
    EXPECT_TRUE(controller->IsInvulnerable());
    EXPECT_GT(controller->GetDodgeCooldownRemaining(), 0.5f);
    EXPECT_GT(rb->GetVelocityXZ().x, 10.0f);
    EXPECT_TRUE(rb->IsAirborne());
    EXPECT_EQ(ps->GetActiveParticleCount(), 6u);

    // Immediate re-dodge blocked by 0.65s cooldown
    EXPECT_FALSE(controller->TriggerDodgeRoll({ -1.0f, 0.0f }));

    // Advance past 0.22s dodge duration (still inside 0.65s cooldown)
    for (int i = 0; i < 16; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FALSE(controller->IsDodging());
    EXPECT_FALSE(controller->IsInvulnerable());
    EXPECT_GT(controller->GetDodgeCooldownRemaining(), 0.0f);
    EXPECT_FALSE(controller->TriggerDodgeRoll({ 0.0f, 1.0f }));

    // Advance past remaining 0.65s cooldown
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(controller->GetDodgeCooldownRemaining(), 0.0f);
    EXPECT_TRUE(controller->TriggerDodgeRoll({ 0.0f, 1.0f }));

    scene.OnRuntimeStop();
}

TEST(SandboxUnitTest, PlayerControllerSurfaceFootworkModulatesSpeedAndDamping) {
    Engine::Scene scene("PlayerSurfaceTestScene");
    auto* terrainGo = scene.CreateGameObject("ArenaTerrain");
    auto* terrain = terrainGo->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 32u, 32u);
    // Layer 2 = Tar (slows movement & increases damping), Layer 4 = Cobble (fast)
    terrain->FillLayer(2u);

    auto* player = scene.CreateGameObject("Player");
    player->GetTransform()->position = { 8.0f, 0.0f, 8.0f };
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    player->AddComponent<Engine::CircleCollider2D>()->Radius = 0.4f;
    auto* controller = player->AddComponent<PlayerController>();

    scene.OnRuntimeStart();
    scene.UpdateRuntime(1.0f / 60.0f);
    const float tarSpeed = controller->GetEffectiveMoveSpeed();
    const float tarDamping = rb->GetLinearDamping();

    terrain->FillLayer(4u);
    scene.UpdateRuntime(1.0f / 60.0f);
    const float cobbleSpeed = controller->GetEffectiveMoveSpeed();
    const float cobbleDamping = rb->GetLinearDamping();

    EXPECT_GT(cobbleSpeed, tarSpeed);
    EXPECT_GT(tarDamping, cobbleDamping);

    scene.OnRuntimeStop();
}

TEST(SandboxUnitTest, PlayerControllerAimSocketsAndCameraIntegration) {
    Engine::Scene scene("PlayerSocketsCameraTestScene");
    auto* player = scene.CreateGameObject("Player");
    auto* sr = player->AddComponent<Engine::SpriteRenderer>();
    auto* camComp = player->AddComponent<Engine::CameraComponent>();
    auto* controller = player->AddComponent<PlayerController>();

    auto* torchSocket = scene.CreateGameObject("TorchSocket");
    torchSocket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* weaponSocket = scene.CreateGameObject("WeaponSocket");
    weaponSocket->GetTransform()->SetParent(player->GetTransform(), false);

    // Facing left via dodge roll updates m_LastFacingXZ -> FlipX = true and negative X socket offset
    scene.OnRuntimeStart();
    EXPECT_TRUE(controller->TriggerDodgeRoll({ -1.0f, 0.0f }));
    scene.UpdateRuntime(1.0f / 60.0f);

    EXPECT_TRUE(sr->GetFlipX());
    EXPECT_LT(torchSocket->GetTransform()->position.x, -0.4f);
    EXPECT_LT(weaponSocket->GetTransform()->position.x, -0.5f);
    EXPECT_LT(camComp->GetTargetAimDirection().x, -0.5f);

    controller->TriggerImpactFeedback(0.45f, { 0.5f, -0.25f });
    EXPECT_GT(camComp->GetTrauma(), 0.4f);

    scene.OnRuntimeStop();
}

} // namespace
