#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Data/GameplayData.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Scripts/CombatTargetComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/CameraController.h"
#include "Scripts/SplinterProjectileComponent.h"
#include "Scripts/WeaponController.h"
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

TEST(SandboxUnitTest, WeaponControllerDataDrivenStatsAndSwingLifecycle) {
    ASSERT_TRUE(Engine::GameplayData::Init("Sandbox/assets/data"));

    Engine::Scene scene("WeaponLifecycleTestScene");
    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    player->AddComponent<PlayerController>();
    auto* wc = player->AddComponent<WeaponController>();

    auto* enemy = scene.CreateGameObject("Ghoul");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->SetLayer(Engine::EntityLayer::Enemy);
    enemy->GetTransform()->position = { 0.0f, 0.0f, 1.5f };
    auto* enemyRb = enemy->AddComponent<Engine::Rigidbody2D>();
    enemyRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    enemy->AddComponent<Engine::CircleCollider2D>()->Radius = 0.4f;
    auto* enemyTarget = enemy->AddComponent<CombatTargetComponent>();
    enemyTarget->ResetHealth(100.0f);
    auto* enemyBowling = enemy->AddComponent<KineticBowlingComponent>();

    scene.OnRuntimeStart();

    EXPECT_TRUE(wc->EquipWeapon("weapon_halberd_t1"));
    EXPECT_FLOAT_EQ(wc->GetEquippedWeapon().Reach, 3.5f);
    EXPECT_TRUE(wc->EquipWeapon("weapon_sickle_t1"));
    EXPECT_FLOAT_EQ(wc->GetEquippedWeapon().AttackRate, 2.0f);
    EXPECT_TRUE(wc->EquipWeapon("weapon_maul_t1"));
    EXPECT_FLOAT_EQ(wc->GetEquippedWeapon().KnockbackImpulse, 22.0f);

    // Aim +Z toward the Ghoul at (0, 0, 1.5)
    wc->SetAimOverrideXZ({ 0.0f, 1.0f });
    EXPECT_TRUE(wc->RequestAttack());
    EXPECT_EQ(wc->GetSwingPhase(), WeaponController::SwingPhase::Windup);

    // Requesting during Windup buffers the follow-up swing
    EXPECT_FALSE(wc->RequestAttack());
    EXPECT_TRUE(wc->IsAttackBuffered());

    // Advance past Windup (~0.31s for Maul) to trigger ActiveCleave
    for (int i = 0; i < 22; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_EQ(wc->GetLastHitCount(), 1u);
    EXPECT_LT(enemyTarget->Health, 100.0f);
    EXPECT_TRUE(enemyBowling->IsLaunched());
    EXPECT_GT(enemyRb->GetVelocityXZ().y, 1.0f);

    scene.OnRuntimeStop();
    Engine::GameplayData::Shutdown();
}

TEST(SandboxUnitTest, KineticBowlingEnemyDominoAndWallSlam) {
    Engine::Scene scene("KineticBowlingDominoTestScene");

    auto* enemyA = scene.CreateGameObject("EnemyA");
    enemyA->SetTag(Engine::EntityTag::Enemy);
    enemyA->SetLayer(Engine::EntityLayer::Enemy);
    enemyA->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rbA = enemyA->AddComponent<Engine::Rigidbody2D>();
    rbA->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbA->LinearDamping = 0.0f;
    enemyA->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto* targetA = enemyA->AddComponent<CombatTargetComponent>();
    targetA->ResetHealth(200.0f);
    auto* bowlingA = enemyA->AddComponent<KineticBowlingComponent>();

    auto* enemyB = scene.CreateGameObject("EnemyB");
    enemyB->SetTag(Engine::EntityTag::Enemy);
    enemyB->SetLayer(Engine::EntityLayer::Enemy);
    enemyB->GetTransform()->position = { 1.4f, 0.0f, 0.0f };
    auto* rbB = enemyB->AddComponent<Engine::Rigidbody2D>();
    rbB->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    enemyB->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    enemyB->AddComponent<Engine::SpriteRenderer>();
    auto* targetB = enemyB->AddComponent<CombatTargetComponent>();
    targetB->ResetHealth(200.0f);
    enemyB->AddComponent<KineticBowlingComponent>();

    auto* tree = scene.CreateGameObject("PineTree");
    tree->SetTag(Engine::EntityTag::HarvestableTree);
    tree->SetLayer(Engine::EntityLayer::Environment);
    tree->GetTransform()->position = { -1.5f, 0.0f, 5.0f };
    tree->AddComponent<Engine::BoxCollider2D>()->size = { 0.8f, 0.8f };
    tree->AddComponent<Engine::SquashStretchComponent>();

    auto* enemyC = scene.CreateGameObject("EnemyC");
    enemyC->SetTag(Engine::EntityTag::Enemy);
    enemyC->SetLayer(Engine::EntityLayer::Enemy);
    enemyC->GetTransform()->position = { 0.0f, 0.0f, 5.0f };
    auto* rbC = enemyC->AddComponent<Engine::Rigidbody2D>();
    rbC->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rbC->LinearDamping = 0.0f;
    enemyC->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto* targetC = enemyC->AddComponent<CombatTargetComponent>();
    targetC->ResetHealth(200.0f);
    auto* bowlingC = enemyC->AddComponent<KineticBowlingComponent>();

    scene.OnRuntimeStart();

    // Launch EnemyA +X into EnemyB
    bowlingA->Launch(0.6f);
    rbA->SetVelocityXZ(14.0f, 0.0f);
    for (int i = 0; i < 15; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_LT(targetA->Health, 200.0f);
    EXPECT_LT(targetB->Health, 200.0f);
    EXPECT_GT(rbB->GetVelocityXZ().x, 1.0f);

    // Launch EnemyC -X into PineTree for wall-slam bonus damage + 0.6s stun
    bowlingC->Launch(0.6f);
    rbC->SetVelocityXZ(-16.0f, 0.0f);
    for (int i = 0; i < 15; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_LT(targetC->Health, 200.0f);
    EXPECT_TRUE(targetC->IsStunned());

    scene.OnRuntimeStop();
}

TEST(SandboxUnitTest, KineticBowlingBarricadeShatterSpawnsSplinters) {
    Engine::Scene scene("BarricadeShatterTestScene");

    auto* enemy = scene.CreateGameObject("LaunchedGhoul");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->SetLayer(Engine::EntityLayer::Enemy);
    enemy->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb = enemy->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    enemy->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    enemy->AddComponent<CombatTargetComponent>()->ResetHealth(200.0f);
    auto* bowling = enemy->AddComponent<KineticBowlingComponent>();

    auto* barricade = scene.CreateGameObject("Splinter Barricade");
    barricade->SetTag(Engine::EntityTag::TacticalStructure);
    barricade->SetLayer(Engine::EntityLayer::LowObstacle);
    barricade->GetTransform()->position = { 1.5f, 0.0f, 0.0f };
    barricade->AddComponent<Engine::BoxCollider2D>()->size = { 0.6f, 1.4f };
    auto* bTarget = barricade->AddComponent<CombatTargetComponent>();
    bTarget->ResetHealth(30.0f); // Shatters on single high-speed slam

    scene.OnRuntimeStart();
    bowling->Launch(0.6f);
    rb->SetVelocityXZ(14.0f, 0.0f);
    for (int i = 0; i < 15; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    const auto splinters = scene.FindGameObjectsWithTag(Engine::EntityTag::Projectile);
    EXPECT_GE(splinters.size(), 5u);
    EXPECT_LE(splinters.size(), 7u);

    scene.OnRuntimeStop();
}

} // namespace
