#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Audio/MusicDirector.h"
#include "Engine/Data/GameplayData.h"
#include "Engine/Data/SaveManager.h"
#include "Engine/Particles/DecalManager.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Scene/Components/PostProcessComponent.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Scene/AlphaRunScene.h"
#include "Scripts/AlphaHUDController.h"
#include "Scripts/ArenaStreamingDriver.h"
#include "Scripts/CombatTargetComponent.h"
#include "Scripts/DeployableStructures.h"
#include "Scripts/EnemyAIController.h"
#include "Scripts/HarvestableComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PickupComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/RuneCasterComponent.h"
#include "Scripts/RuneEffectsComponents.h"
#include "Scripts/RunProgressionComponent.h"
#include "Scripts/RunSessionManager.h"
#include "Scripts/RunSummaryController.h"
#include "Scripts/SanctuaryHubComponents.h"
#include "Scripts/CameraController.h"
#include "Scripts/SplinterProjectileComponent.h"
#include "Scripts/StatusEffectsComponent.h"
#include "Scripts/StructurePlacerComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WaveDirectorComponent.h"
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
    Engine::Time::Reset();
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
    Engine::Time::Reset();
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
    Engine::Time::Reset();
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
    Engine::Time::Reset();
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
    Engine::Time::Reset();
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
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, AlphaRunSceneCreatesSafeClearingAndNightLighting) {
    auto scene = Sandbox::AlphaRunScene::Create(1337ULL);
    ASSERT_NE(scene, nullptr);

    auto findByName = [](Engine::Scene& s, const char* name) -> Engine::GameObject* {
        for (const auto& go : s.GetGameObjects()) {
            if (go && go->GetName() == name) {
                return go.get();
            }
        }
        return nullptr;
    };

    auto* moonGo = findByName(*scene, "MoonLight");
    ASSERT_NE(moonGo, nullptr);
    auto* moonLight = moonGo->GetComponent<Engine::DirectionalLightComponent>();
    ASSERT_NE(moonLight, nullptr);
    EXPECT_FLOAT_EQ(moonLight->SunIntensity, Sandbox::AlphaRunScene::kNightMoonIntensity);

    auto* terrainGo = findByName(*scene, "Terrain");
    ASSERT_NE(terrainGo, nullptr);
    auto* chunkMgr = terrainGo->GetComponent<Engine::TerrainChunkManager>();
    ASSERT_NE(chunkMgr, nullptr);
    EXPECT_TRUE(chunkMgr->GetWorldGenConfig().Enabled);
    EXPECT_EQ(chunkMgr->GetWorldGenConfig().Seed, 1337ULL);
    EXPECT_FLOAT_EQ(chunkMgr->GetWorldGenConfig().OriginSafeRadius, 8.0f);
    EXPECT_EQ(chunkMgr->GetActiveChunkCount(), 49u);

    const auto originBiome = chunkMgr->SampleBiomeAtXZ(0.0f, 0.0f);
    const auto spawnBiome = chunkMgr->SampleBiomeAtXZ(0.0f, 2.5f);
    EXPECT_EQ(originBiome.DominantBiome, Engine::BiomeType::SanctuaryCobble);
    EXPECT_EQ(spawnBiome.DominantBiome, Engine::BiomeType::SanctuaryCobble);

    auto* brazierGo = findByName(*scene, "Sanctuary Brazier");
    ASSERT_NE(brazierGo, nullptr);
    auto* brazierLight = brazierGo->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(brazierLight, nullptr);
    EXPECT_EQ(brazierLight->Semantic, Engine::LightSemantic::SafeLifeline);
    EXPECT_TRUE(brazierLight->CastShadows);

    auto* playerGo = findByName(*scene, "Player");
    ASSERT_NE(playerGo, nullptr);
    auto* heroTorch = playerGo->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(heroTorch, nullptr);
    EXPECT_EQ(heroTorch->Priority, Engine::LightPriority::Critical_HeroTorch);
    EXPECT_TRUE(heroTorch->CastShadows);
    EXPECT_NE(playerGo->GetComponent<Engine::CameraComponent>(), nullptr);
    EXPECT_NE(findByName(*scene, "TorchSocket"), nullptr);
    EXPECT_NE(findByName(*scene, "WeaponSocket"), nullptr);
}

TEST(SandboxUnitTest, AlphaRunSceneStreamsBiomesOnPlayerMovement) {
    auto scene = Sandbox::AlphaRunScene::Create(2026ULL);
    ASSERT_NE(scene, nullptr);
    Sandbox::AlphaRunScene::BindRuntimeGameplayScripts(scene);

    auto findByName = [](Engine::Scene& s, const char* name) -> Engine::GameObject* {
        for (const auto& go : s.GetGameObjects()) {
            if (go && go->GetName() == name) {
                return go.get();
            }
        }
        return nullptr;
    };

    auto* terrainGo = findByName(*scene, "Terrain");
    auto* playerGo = findByName(*scene, "Player");
    ASSERT_NE(terrainGo, nullptr);
    ASSERT_NE(playerGo, nullptr);
    auto* chunkMgr = terrainGo->GetComponent<Engine::TerrainChunkManager>();
    ASSERT_NE(chunkMgr, nullptr);
    EXPECT_NE(playerGo->GetComponent<PlayerController>(), nullptr);
    EXPECT_NE(playerGo->GetComponent<WeaponController>(), nullptr);
    EXPECT_NE(playerGo->GetComponent<Sandbox::ArenaStreamingDriver>(), nullptr);

    scene->OnRuntimeStart();

    // Move Player 32m east (2 chunks at 16m/chunk) and step runtime
    playerGo->GetTransform()->position = { 32.0f, 0.0f, 0.0f };
    if (auto* rb = playerGo->GetComponent<Engine::Rigidbody2D>()) {
        rb->SetPositionXZ(32.0f, 0.0f);
    }
    scene->UpdateRuntime(1.0f / 60.0f);
    chunkMgr->FlushPendingGenerations();
    chunkMgr->DrainReadyPayloads();

    const Engine::ChunkCoord expectedCoord{ 2, 0 };
    EXPECT_EQ(chunkMgr->GetLastViewerChunk().cx, expectedCoord.cx);
    EXPECT_EQ(chunkMgr->GetLastViewerChunk().cz, expectedCoord.cz);
    const auto* streamedChunk = chunkMgr->GetChunk(expectedCoord);
    ASSERT_NE(streamedChunk, nullptr);
    EXPECT_TRUE(streamedChunk->State == Engine::ChunkLoadState::Resident ||
                streamedChunk->State == Engine::ChunkLoadState::ReadyForGpuUpload);

    scene->OnRuntimeStop();
}

TEST(SandboxUnitTest, HarvestablePropShakesAndDropsBouncingPickupsOnFell) {
    Engine::Scene scene("HarvestableTestScene");

    auto* pine = scene.CreateGameObject("Procedural Pine");
    pine->SetTag(Engine::EntityTag::HarvestableTree);
    pine->SetLayer(Engine::EntityLayer::Environment);
    pine->GetTransform()->position = { 2.0f, 0.0f, 0.0f };
    auto* col = pine->AddComponent<Engine::BoxCollider2D>();
    col->size = { 0.55f, 0.45f };
    col->CategoryBits = Engine::PhysicsLayer::Environment;
    auto* harvestable = pine->AddComponent<HarvestableComponent>();
    harvestable->DefaultTreeHealth = 30.0f;

    scene.OnRuntimeStart();

    auto* target = pine->GetComponent<CombatTargetComponent>();
    ASSERT_NE(target, nullptr);
    target->ResetHealth(30.0f);

    // Chipping hit shakes and emits wood-chip particles without felling
    EXPECT_TRUE(target->ApplyDamage(10.0f));
    EXPECT_FALSE(harvestable->IsFelled());
    EXPECT_NE(pine->GetComponent<Engine::SquashStretchComponent>(), nullptr);
    EXPECT_NE(pine->GetComponent<Engine::ParticleSystem>(), nullptr);

    // Lethal hit fells tree and spawns 2-4 bouncing PickupComponent entities
    EXPECT_TRUE(target->ApplyDamage(25.0f));
    EXPECT_TRUE(harvestable->IsFelled());

    const auto pickups = scene.FindGameObjectsWithTag(Engine::EntityTag::ResourcePickup);
    EXPECT_GE(pickups.size(), 2u);
    EXPECT_LE(pickups.size(), 4u);
    for (Engine::GameObject* p : pickups) {
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->GetLayer(), Engine::EntityLayer::Pickup);
        auto* pCol = p->GetComponent<Engine::CircleCollider2D>();
        ASSERT_NE(pCol, nullptr);
        EXPECT_TRUE(pCol->IsSensor);
        EXPECT_EQ(pCol->CategoryBits, Engine::PhysicsLayer::Pickup);
        auto* pRb = p->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(pRb, nullptr);
        EXPECT_FLOAT_EQ(pRb->GroundRestitutionY, 0.45f);
        EXPECT_GE(pRb->VerticalVelocity, 4.5f);
        EXPECT_LE(pRb->VerticalVelocity, 6.5f);
    }

    // Dissolve finishes within 0.30s and removes felled tree
    for (int i = 0; i < 20; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_EQ(scene.FindGameObjectsWithTag(Engine::EntityTag::HarvestableTree).size(), 0u);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, PickupCollectionGrantsWoodAshAndHeal) {
    Engine::Scene scene("PickupCollectionTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->SetLayer(Engine::EntityLayer::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* col = player->AddComponent<Engine::CircleCollider2D>();
    col->Radius = 0.5f;
    col->CategoryBits = Engine::PhysicsLayer::Player;
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    stats->MaxHP = 100.0f;
    stats->HP = 60.0f;
    stats->AshAttractionRange = 6.0f;

    scene.OnRuntimeStart();

    // 1. WoodLog collected on overlap (+1 Wood)
    PickupComponent::SpawnPickup(scene, PickupKind::WoodLog, { 0.0f, 0.2f, 0.0f }, { 0.0f, 0.0f }, 4.5f);
    // 2. CrimsonBerry collected on overlap (+20 HP)
    PickupComponent::SpawnPickup(scene, PickupKind::CrimsonBerry, { 0.0f, 0.2f, 0.0f }, { 0.0f, 0.0f }, 4.5f);
    // 3. AshEmber spawned at 3.2m magnetically accelerates to player (+XP, +RunAshes)
    PickupComponent::SpawnPickup(scene, PickupKind::AshEmber, { 3.2f, 0.2f, 0.0f }, { 0.0f, 0.0f }, 4.5f);

    for (int i = 0; i < 90; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    EXPECT_EQ(stats->Wood, 1);
    EXPECT_FLOAT_EQ(stats->HP, 80.0f);
    EXPECT_GT(stats->XP, 0);
    EXPECT_GT(stats->RunAshes, 0);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, HealthComponentAppliesMitigationFlashSquashParticlesAndKnockback) {
    Engine::Scene scene("HealthImpactTestScene");

    auto* entity = scene.CreateGameObject("Cultist");
    entity->SetTag(Engine::EntityTag::Enemy);
    entity->SetLayer(Engine::EntityLayer::Enemy);
    auto* sr = entity->AddComponent<Engine::SpriteRenderer>();
    entity->AddComponent<Engine::SquashStretchComponent>();
    entity->AddComponent<Engine::ParticleSystem>(64);
    auto* rb = entity->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    entity->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto* status = entity->AddComponent<StatusEffectsComponent>();
    auto* health = entity->AddComponent<HealthComponent>();
    health->ResetHealth(100.0f);
    health->DamageReduction = 0.25f;
    health->KnockbackResistance = 0.50f;
    health->InvulnOnHitDuration = 0.20f;

    scene.OnRuntimeStart();

    DamageEvent evt;
    evt.Amount = 40.0f;
    evt.ImpulseXZ = { 20.0f, 0.0f };
    evt.StunDuration = 0.30f;
    evt.IsFire = true;

    EXPECT_TRUE(health->ApplyDamage(evt));
    EXPECT_FLOAT_EQ(health->CurrentHP, 70.0f); // 40 * (1 - 0.25) = 30 damage
    EXPECT_TRUE(status->IsStunned());
    EXPECT_TRUE(status->IsInvulnerable());
    EXPECT_FLOAT_EQ(sr->GetFlashAmount(), 1.0f);
    EXPECT_FLOAT_EQ(sr->GetCutoutFX().z, static_cast<float>(Engine::SpriteRenderer::FlashMode::CrimsonImpact));

    // Invulnerability blocks immediate repeat hit
    EXPECT_FALSE(health->ApplyDamage(evt));

    // Flash decays to 0 over 0.12s
    for (int i = 0; i < 8; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(sr->GetFlashAmount(), 0.0f);

    // Burn ticks through StatusEffectsComponent -> HealthComponent
    status->ApplyBurn(0.50f, 20.0f);
    EXPECT_TRUE(status->IsBurning());
    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_LT(health->CurrentHP, 65.0f);
    EXPECT_FALSE(status->IsBurning());

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, HealthComponentDeathSpawnsDecalDissolvesAndReleasesToPool) {
    Engine::Scene scene("HealthPoolDeathTestScene");

    auto* proto = scene.CreateGameObject("PooledGhoul");
    proto->SetTag(Engine::EntityTag::Enemy);
    proto->AddComponent<Engine::SpriteRenderer>();
    proto->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto prefab = Engine::Prefab::CreateFromGameObject(proto);
    scene.DestroyGameObject(proto);

    auto& poolMgr = scene.GetPoolManager();
    poolMgr.PrewarmPool("ghoul_pool", *prefab, 1u);
    auto* enemy = poolMgr.AcquirePooled("ghoul_pool", *prefab, { 1.0f, 0.0f, 2.0f });
    ASSERT_NE(enemy, nullptr);

    auto* sr = enemy->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(sr, nullptr);
    auto* health = enemy->AddComponent<HealthComponent>();
    health->ResetHealth(25.0f);
    health->DissolveDuration = 0.20f;
    health->PoolKey = "ghoul_pool";

    scene.OnRuntimeStart();

    DamageEvent lethal;
    lethal.Amount = 50.0f;
    EXPECT_TRUE(health->ApplyDamage(lethal));
    EXPECT_TRUE(health->IsDying());
    EXPECT_EQ(scene.GetDecalManager().GetActiveCount(), 1u);

    // Advance partway through dissolve
    for (int i = 0; i < 6; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_GT(sr->GetDissolveAmount(), 0.0f);
    EXPECT_LT(sr->GetDissolveAmount(), 1.0f);

    // Finish dissolve -> entity returned to pool
    for (int i = 0; i < 10; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_EQ(poolMgr.GetPoolFreeCount("ghoul_pool"), 1u);
    EXPECT_FALSE(enemy->IsActive());

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, TorchComponentFuelBurnRadiusAndStoke) {
    Engine::Scene scene("TorchFuelTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    auto* light = player->AddComponent<Engine::PointLightComponent>();
    light->Priority = Engine::LightPriority::Critical_HeroTorch;
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    stats->Wood = 3;
    player->AddComponent<Engine::ParticleSystem>(64);

    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();

    scene.OnRuntimeStart();

    for (int i = 0; i < 600; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    EXPECT_NEAR(torch->Fuel, 78.0f, 0.25f);
    EXPECT_GT(light->Radius, torch->MinRadius);
    EXPECT_LT(light->Radius, torch->MaxRadius);
    EXPECT_LT(light->Intensity, torch->MaxIntensity);

    EXPECT_TRUE(torch->StokeTorch());
    EXPECT_EQ(stats->Wood, 1);
    EXPECT_FLOAT_EQ(torch->Fuel, 100.0f);
    EXPECT_FALSE(torch->StokeTorch()); // Only 1 Wood left

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, TorchComponentSearingRimBurnsVulnerableEnemies) {
    Engine::Scene scene("TorchSearingRimTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->AddComponent<Engine::PointLightComponent>();
    player->AddComponent<PlayerStatsComponent>();
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->AuraDamage = 20.0f;
    torch->BaseBurnRate = 0.0f;

    auto makeEnemy = [&](const char* name, float xPos, float vuln) -> HealthComponent* {
        auto* e = scene.CreateGameObject(name);
        e->SetTag(Engine::EntityTag::Enemy);
        e->GetTransform()->position = { xPos, 0.0f, 0.0f };
        e->AddComponent<Engine::SpriteRenderer>();
        e->AddComponent<Engine::ParticleSystem>(32);
        auto* h = e->AddComponent<HealthComponent>();
        h->ResetHealth(100.0f);
        h->LightVulnerability = vuln;
        return h;
    };

    auto* innerHealth = makeEnemy("InnerEnemy", 2.5f, 2.0f); // < 0.75 * 9.5
    auto* rimHealth = makeEnemy("RimEnemy", 8.5f, 2.0f);     // in [7.125, 9.975]
    auto* farHealth = makeEnemy("FarEnemy", 12.0f, 2.0f);    // > 1.05 * 9.5

    scene.OnRuntimeStart();

    for (int i = 0; i < 30; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }

    EXPECT_FLOAT_EQ(innerHealth->CurrentHP, 100.0f);
    EXPECT_LT(rimHealth->CurrentHP, 90.0f); // 20 DPS * 0.5s * 2.0 vuln = 20 dmg
    EXPECT_FLOAT_EQ(farHealth->CurrentHP, 100.0f);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, TorchComponentAshFrenzyAndDarknessDread) {
    Engine::Scene scene("TorchFrenzyDreadTestScene");

    auto* ppGo = scene.CreateGameObject("PostProcess");
    auto* pp = ppGo->AddComponent<Engine::PostProcessComponent>();
    pp->ApplyPreset(Engine::PostProcessComponent::Preset::CultStorybook);

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    auto* light = player->AddComponent<Engine::PointLightComponent>();
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    stats->HP = 100.0f;
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;

    scene.OnRuntimeStart();

    // 1. Ash Frenzy (Fuel <= 20%)
    torch->Fuel = 15.0f;
    for (int i = 0; i < 6; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_TRUE(torch->IsAshFrenzy());
    EXPECT_FLOAT_EQ(stats->MoveSpeedMultiplier, 1.20f);
    EXPECT_FLOAT_EQ(stats->AttackSpeedMultiplier, 1.20f);
    EXPECT_GT(pp->VignetteColor.r, 0.4f);
    EXPECT_EQ(Engine::MusicDirector::GetCombatTier(), Engine::CombatMusicTier::AshFrenzyCombat);

    // 2. Darkness Dread (Fuel == 0%) deals 6 HP/s
    torch->Fuel = 0.0f;
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_TRUE(torch->IsDarknessDread());
    EXPECT_FLOAT_EQ(light->Radius, torch->DreadRadius);
    EXPECT_NEAR(stats->HP, 94.0f, 0.25f);

    // 3. Lit SafeLifeline brazier shelters player from Darkness Dread
    auto* brazier = scene.CreateGameObject("Sanctuary Brazier");
    brazier->SetTag(Engine::EntityTag::SafeZoneBrazier);
    auto* bLight = brazier->AddComponent<Engine::PointLightComponent>();
    bLight->Semantic = Engine::LightSemantic::SafeLifeline;
    bLight->Radius = 6.0f;
    bLight->Intensity = 2.0f;

    const float hpBeforeShelter = stats->HP;
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FLOAT_EQ(stats->HP, hpBeforeShelter);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, RuneCasterIgniteAndShockwaveSpendFuelAndTriggerEffects) {
    Engine::Scene scene("RuneIgniteShockwaveTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->AddComponent<Engine::PointLightComponent>();
    player->AddComponent<PlayerStatsComponent>();
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;
    auto* caster = player->AddComponent<RuneCasterComponent>();
    ASSERT_TRUE(caster->EquipRune("rune_ignite"));

    auto* enemy = scene.CreateGameObject("Cultist");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->GetTransform()->position = { 3.0f, 0.0f, 0.0f };
    auto* rb = enemy->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    enemy->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto* status = enemy->AddComponent<StatusEffectsComponent>();
    auto* health = enemy->AddComponent<HealthComponent>();
    health->ResetHealth(100.0f);

    scene.OnRuntimeStart();

    EXPECT_TRUE(caster->TryCastRune());
    EXPECT_FLOAT_EQ(torch->Fuel, 85.0f); // 100 - 15
    EXPECT_FALSE(caster->TryCastRune()); // Cooldown blocks immediate recast
    EXPECT_LT(health->CurrentHP, 95.0f);
    EXPECT_TRUE(status->IsBurning());

    // Advance past 4.0s cooldown and cast Rune of Shockwave
    for (int i = 0; i < 250; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    const float fuelBeforeShockwave = torch->Fuel;
    ASSERT_TRUE(caster->EquipRune("rune_shockwave"));
    EXPECT_TRUE(caster->TryCastRune());
    EXPECT_FLOAT_EQ(torch->Fuel, fuelBeforeShockwave - 12.0f);

    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_GT(rb->GetVelocityXZ().x, 0.5f);

    bool foundRing = false;
    for (const auto& go : scene.GetGameObjects()) {
        if (go && go->GetComponent<ShockwaveRingComponent>() != nullptr) {
            foundRing = true;
            break;
        }
    }
    EXPECT_TRUE(foundRing);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, RuneCasterChronosStasisQueuesImpulsesUntilUnfreeze) {
    Engine::Scene scene("RuneChronosTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->AddComponent<Engine::PointLightComponent>();
    player->AddComponent<PlayerStatsComponent>();
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;
    auto* caster = player->AddComponent<RuneCasterComponent>();
    ASSERT_TRUE(caster->EquipRune("rune_chronos"));

    auto* enemy = scene.CreateGameObject("FrozenGhoul");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->GetTransform()->position = { 3.0f, 0.0f, 0.0f };
    auto* sr = enemy->AddComponent<Engine::SpriteRenderer>();
    auto* rb = enemy->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.5f;
    enemy->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    enemy->AddComponent<KineticBowlingComponent>();

    scene.OnRuntimeStart();

    EXPECT_TRUE(caster->TryCastRune());
    EXPECT_FLOAT_EQ(torch->Fuel, 80.0f); // 100 - 20
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_TRUE(rb->IsStasisFrozen());
    EXPECT_FLOAT_EQ(sr->GetCutoutFX().z, static_cast<float>(Engine::SpriteRenderer::FlashMode::SpectralBlue));

    // Queue impulse while frozen
    rb->ApplyImpulseXZ(20.0f, 0.0f);
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_TRUE(rb->IsStasisFrozen());
    EXPECT_NEAR(glm::length(rb->GetVelocityXZ()), 0.0f, 0.05f);

    // Advance past 4.0s stasis duration -> queued impulse erupts
    for (int i = 0; i < 190; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FALSE(rb->IsStasisFrozen());
    EXPECT_GT(rb->GetVelocityXZ().x, 1.0f);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, RuneCasterConsecrationHealsPlayerAndBurnsEnemies) {
    Engine::Scene scene("RuneConsecrateTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->AddComponent<Engine::PointLightComponent>();
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    stats->HP = 70.0f;
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;
    auto* caster = player->AddComponent<RuneCasterComponent>();
    ASSERT_TRUE(caster->EquipRune("rune_consecrate"));

    auto* enemy = scene.CreateGameObject("ShadowFoe");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->GetTransform()->position = { 4.0f, 0.0f, 0.0f };
    auto* health = enemy->AddComponent<HealthComponent>();
    health->ResetHealth(100.0f);

    scene.OnRuntimeStart();

    EXPECT_TRUE(caster->TryCastRune());
    EXPECT_FLOAT_EQ(torch->Fuel, 75.0f); // 100 - 25

    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_GT(stats->HP, 73.0f);
    EXPECT_LT(health->CurrentHP, 95.0f);

    // Advance past 6.0s duration -> ConsecrationZone expires
    for (int i = 0; i < 320; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    bool foundZone = false;
    for (const auto& go : scene.GetGameObjects()) {
        if (go && go->GetComponent<ConsecrationZoneComponent>() != nullptr) {
            foundZone = true;
            break;
        }
    }
    EXPECT_FALSE(foundZone);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, StructurePlacerValidatesClearanceDeductsWoodAndSpawnsAll4Structures) {
    Engine::Scene scene("StructurePlacerTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    player->AddComponent<PlayerController>();
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    stats->Wood = 50;
    auto* placer = player->AddComponent<StructurePlacerComponent>();

    // Solid tree trunk at 2.2m in front of player blocks placement
    auto* tree = scene.CreateGameObject("BlockingTree");
    tree->SetTag(Engine::EntityTag::HarvestableTree);
    tree->SetLayer(Engine::EntityLayer::Environment);
    tree->GetTransform()->position = { 0.0f, 0.0f, 2.2f };
    auto* treeCol = tree->AddComponent<Engine::BoxCollider2D>();
    treeCol->size = { 0.6f, 0.6f };
    treeCol->CategoryBits = Engine::PhysicsLayer::Environment;

    scene.OnRuntimeStart();

    EXPECT_EQ(placer->TryPlaceStructure(Engine::RadialWheelItem::SplinterBarricade), nullptr);
    EXPECT_EQ(stats->Wood, 50);

    // Move tree away and place all 4 structures (4 + 10 + 15 + 20 = 49 Wood)
    scene.DestroyGameObject(tree);
    scene.UpdateRuntime(1.0f / 60.0f);

    auto* bar = placer->TryPlaceStructure(Engine::RadialWheelItem::SplinterBarricade);
    ASSERT_NE(bar, nullptr);
    EXPECT_NE(bar->GetComponent<SplinterBarricadeComponent>(), nullptr);
    EXPECT_EQ(stats->Wood, 46);
    scene.DestroyGameObject(bar);
    scene.UpdateRuntime(1.0f / 60.0f);

    auto* flare = placer->TryPlaceStructure(Engine::RadialWheelItem::FlarePeg);
    ASSERT_NE(flare, nullptr);
    EXPECT_NE(flare->GetComponent<FlarePegComponent>(), nullptr);
    EXPECT_EQ(stats->Wood, 36);
    scene.DestroyGameObject(flare);
    scene.UpdateRuntime(1.0f / 60.0f);

    auto* thumper = placer->TryPlaceStructure(Engine::RadialWheelItem::Thumper);
    ASSERT_NE(thumper, nullptr);
    EXPECT_NE(thumper->GetComponent<ThumperComponent>(), nullptr);
    EXPECT_EQ(stats->Wood, 21);
    scene.DestroyGameObject(thumper);
    scene.UpdateRuntime(1.0f / 60.0f);

    auto* siphon = placer->TryPlaceStructure(Engine::RadialWheelItem::TarSiphon);
    ASSERT_NE(siphon, nullptr);
    EXPECT_NE(siphon->GetComponent<TarSiphonComponent>(), nullptr);
    EXPECT_EQ(stats->Wood, 1);

    // Insufficient wood (1 < 4) blocks next placement
    EXPECT_EQ(placer->TryPlaceStructure(Engine::RadialWheelItem::SplinterBarricade), nullptr);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, DeployableStructuresExecuteBarricadeFlareThumperAndTarSiphon) {
    Engine::Scene scene("DeployableBehaviorsTestScene");

    // 1. SplinterBarricade shatters into 5-7 projectiles at 0 HP
    auto* barGo = scene.CreateGameObject("Barricade");
    barGo->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    barGo->AddComponent<SplinterBarricadeComponent>();

    // 2. FlarePeg emits 7.5m SafeLifeline light
    auto* flareGo = scene.CreateGameObject("Flare");
    flareGo->GetTransform()->position = { 10.0f, 0.0f, 0.0f };
    auto* flare = flareGo->AddComponent<FlarePegComponent>();
    flare->RemainingLifetime = 0.25f;

    // 3. Thumper + Enemy within 2.2m
    auto* thumperGo = scene.CreateGameObject("Thumper");
    thumperGo->GetTransform()->position = { 20.0f, 0.0f, 0.0f };
    auto* thumper = thumperGo->AddComponent<ThumperComponent>();
    auto* enemy = scene.CreateGameObject("Enemy");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->GetTransform()->position = { 21.2f, 0.0f, 0.0f };
    auto* eRb = enemy->AddComponent<Engine::Rigidbody2D>();
    eRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    eRb->UseGravityY = true;
    enemy->AddComponent<Engine::CircleCollider2D>()->Radius = 0.4f;

    // 4. TarSiphon spawns AshEmber every 3.0s
    auto* siphonGo = scene.CreateGameObject("TarSiphon");
    siphonGo->GetTransform()->position = { -10.0f, 0.0f, 0.0f };
    auto* siphon = siphonGo->AddComponent<TarSiphonComponent>();
    siphon->CycleInterval = 0.20f;

    scene.OnRuntimeStart();

    // Shatter barricade
    DamageEvent lethal;
    lethal.Amount = 200.0f;
    lethal.ImpulseXZ = { 10.0f, 0.0f };
    EXPECT_TRUE(barGo->GetComponent<HealthComponent>()->ApplyDamage(lethal));
    const auto splinters = scene.FindGameObjectsWithTag(Engine::EntityTag::Projectile);
    EXPECT_GE(splinters.size(), 5u);
    EXPECT_LE(splinters.size(), 7u);

    // Check FlarePeg light
    auto* flareLight = flareGo->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(flareLight, nullptr);
    EXPECT_FLOAT_EQ(flareLight->Radius, 7.5f);
    EXPECT_EQ(flareLight->Semantic, Engine::LightSemantic::SafeLifeline);

    // Trigger Thumper blast
    thumper->TriggerBlast();
    EXPECT_GT(thumper->GetCooldownRemaining(), 2.0f);
    EXPECT_GE(eRb->VerticalVelocity, 5.5f);
    EXPECT_GE(scene.GetDecalManager().GetActiveCount(), 1u);

    // Step 0.30s -> Flare expires, TarSiphon spawns AshEmber, Enemy moves backward
    for (int i = 0; i < 20; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_GT(eRb->GetVelocityXZ().x, 1.0f);
    EXPECT_GE(scene.FindGameObjectsWithTag(Engine::EntityTag::ResourcePickup).size(), 1u);

    bool flareAlive = false;
    for (const auto& go : scene.GetGameObjects()) {
        if (go.get() == flareGo && !go->IsPendingDestroy()) {
            flareAlive = true;
            break;
        }
    }
    EXPECT_FALSE(flareAlive);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, TorchDashAndStokeIgniteContiguousTarAndBurnCrossingEnemies) {
    auto scene = Sandbox::AlphaRunScene::Create(42ULL);
    ASSERT_NE(scene, nullptr);
    Sandbox::AlphaRunScene::BindRuntimeGameplayScripts(scene);

    Engine::GameObject* player = nullptr;
    Engine::GameObject* torchSocket = nullptr;
    for (const auto& go : scene->GetGameObjects()) {
        if (go && go->GetName() == "Player") {
            player = go.get();
        } else if (go && go->GetName() == "TorchSocket") {
            torchSocket = go.get();
        }
    }
    ASSERT_NE(player, nullptr);
    ASSERT_NE(torchSocket, nullptr);
    auto* stats = player->GetComponent<PlayerStatsComponent>();
    auto* torch = torchSocket->GetComponent<TorchComponent>();
    ASSERT_NE(stats, nullptr);
    ASSERT_NE(torch, nullptr);
    stats->Wood = 4;

    auto* enemy = scene->CreateGameObject("TarCrosser");
    enemy->SetTag(Engine::EntityTag::Enemy);
    enemy->GetTransform()->position = { 3.0f, 0.0f, 2.5f };
    auto* eStatus = enemy->AddComponent<StatusEffectsComponent>();
    auto* eHealth = enemy->AddComponent<HealthComponent>();
    eHealth->ResetHealth(100.0f);

    scene->OnRuntimeStart();

    // Paint contiguous Tar strip from Player (0, 2.5) to Enemy (3.0, 2.5)
    scene->PaintMaterialWorld({ 0.5f, 2.5f }, 2u, 1.8f, 1.0f);
    scene->PaintMaterialWorld({ 2.0f, 2.5f }, 2u, 1.8f, 1.0f);
    scene->PaintMaterialWorld({ 3.2f, 2.5f }, 2u, 1.5f, 1.0f);

    // Stoking over Tar ignites it, spreads at 10 Hz across contiguous Tar, and burns crossing enemy (14 DPS)
    EXPECT_TRUE(torch->StokeTorch());
    for (int i = 0; i < 65; ++i) {
        scene->UpdateRuntime(1.0f / 60.0f);
    }

    EXPECT_TRUE(scene->IsBurningAtXZ(3.0f, 2.5f));
    EXPECT_LT(eHealth->CurrentHP, 96.0f);
    EXPECT_TRUE(eStatus->IsBurning());

    scene->OnRuntimeStop();
}

TEST(SandboxUnitTest, BurningBodySpreadsFireToTarAndScorchesToLayer5) {
    Engine::Scene scene("BurningBodyTarTestScene");

    auto* terrainGo = scene.CreateGameObject("Terrain");
    auto* chunkMgr = terrainGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 1, 2);
    chunkMgr->AutoStreamWithCamera = false;
    chunkMgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    chunkMgr->FlushPendingGenerations();
    chunkMgr->DrainReadyPayloads();

    auto* runner = scene.CreateGameObject("BurningRunner");
    runner->SetTag(Engine::EntityTag::Enemy);
    runner->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* status = runner->AddComponent<StatusEffectsComponent>();
    status->ApplyBurn(1.0f, 5.0f);

    scene.OnRuntimeStart();

    // Paint flammable Tar at (0, 0)
    scene.PaintMaterialWorld({ 0.0f, 0.0f }, 2u, 2.5f, 1.0f);

    // Burning body ignites the Tar beneath it
    for (int i = 0; i < 20; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_TRUE(scene.IsBurningAtXZ(0.0f, 0.0f));

    // Stop burning body and advance past 4.5s MaxBurnDuration -> scorches Tar into Scorched Ash (Layer 5)
    status->BurnTimer = 0.0f;
    status->BurnDPS = 0.0f;
    for (int i = 0; i < 300; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_FALSE(scene.IsBurningAtXZ(0.0f, 0.0f));
    EXPECT_LT(chunkMgr->SampleSurfaceAtXZ(0.0f, 0.0f).Flammability, 0.35f);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, EnemyAIStatesYieldDuringStunAndDissolveOnDeath) {
    Engine::Scene scene("EnemyAIStateTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };

    auto* ghoul = scene.CreateGameObject("Ghoul");
    ghoul->GetTransform()->position = { 6.0f, 0.0f, 0.0f };
    auto* sr = ghoul->AddComponent<Engine::SpriteRenderer>();
    auto* rb = ghoul->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    ghoul->AddComponent<Engine::CircleCollider2D>()->Radius = 0.40f;
    auto* ai = ghoul->AddComponent<EnemyAIController>();
    ai->EnemyId = "enemy_ghoul";

    scene.OnRuntimeStart();
    EXPECT_FLOAT_EQ(ai->GetStats().Health, 35.0f);
    EXPECT_FLOAT_EQ(ai->GetStats().Mass, 25.0f);
    EXPECT_EQ(ai->GetCurrentState(), EnemyAIState::Chase);

    for (int i = 0; i < 15; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_LT(ghoul->GetTransform()->position.x, 5.9f);

    // Knockback + Stun yields velocity control to Box2D
    auto* health = ghoul->GetComponent<HealthComponent>();
    ASSERT_NE(health, nullptr);
    const float xBeforeKnockback = ghoul->GetTransform()->position.x;
    DamageEvent kbEvt;
    kbEvt.Amount = 10.0f;
    kbEvt.ImpulseXZ = { 80.0f, 0.0f };
    kbEvt.StunDuration = 0.50f;
    EXPECT_TRUE(health->ApplyDamage(kbEvt));
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(ai->GetCurrentState(), EnemyAIState::Stunned);
    for (int i = 0; i < 10; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_GT(ghoul->GetTransform()->position.x, xBeforeKnockback);

    // Lethal damage transitions to Dying with 0.45s RND-01 dissolve
    DamageEvent lethal;
    lethal.Amount = 100.0f;
    EXPECT_TRUE(health->ApplyDamage(lethal));
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(ai->GetCurrentState(), EnemyAIState::Dying);
    for (int i = 0; i < 12; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_GT(sr->GetDissolveAmount(), 0.2f);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, EnemyAIThrallPrioritizesBarricadeAndJiangshiHopsOverIt) {
    Engine::Scene scene("ThrallAndJiangshiTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    player->AddComponent<PlayerStatsComponent>();

    auto* bar = scene.CreateGameObject("Splinter Barricade");
    bar->GetTransform()->position = { 2.5f, 0.0f, 0.0f };
    bar->AddComponent<SplinterBarricadeComponent>();
    auto* barHealth = bar->GetComponent<HealthComponent>();
    ASSERT_NE(barHealth, nullptr);

    auto* thrall = scene.CreateGameObject("Thrall");
    thrall->GetTransform()->position = { 3.5f, 0.0f, 0.0f };
    auto* tRb = thrall->AddComponent<Engine::Rigidbody2D>();
    tRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    thrall->AddComponent<Engine::CircleCollider2D>()->Radius = 0.45f;
    auto* thrallAI = thrall->AddComponent<EnemyAIController>();
    thrallAI->ConfigureArchetype("enemy_thrall");

    auto* jiangshi = scene.CreateGameObject("Jiangshi");
    jiangshi->GetTransform()->position = { 4.2f, 0.0f, 1.0f };
    auto* jRb = jiangshi->AddComponent<Engine::Rigidbody2D>();
    jRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    jRb->UseGravityY = true;
    jiangshi->AddComponent<Engine::CircleCollider2D>()->Radius = 0.40f;
    auto* jiangshiAI = jiangshi->AddComponent<EnemyAIController>();
    jiangshiAI->ConfigureArchetype("enemy_jiangshi");
    jiangshiAI->HopCooldown = 0.1f;

    scene.OnRuntimeStart();
    EXPECT_FLOAT_EQ(thrallAI->GetStats().Health, 50.0f);
    EXPECT_FLOAT_EQ(jiangshiAI->GetStats().Health, 65.0f);

    bool jiangshiWentAirborne = false;
    bool sawLandingRing = false;
    for (int i = 0; i < 120; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
        if (jRb->VerticalVelocity > 1.0f) {
            jiangshiWentAirborne = true;
        }
        if (jiangshiAI->HasLandingIndicator()) {
            sawLandingRing = true;
        }
    }

    EXPECT_LT(barHealth->CurrentHP, 120.0f);
    EXPECT_TRUE(jiangshiWentAirborne);
    EXPECT_TRUE(sawLandingRing);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, EnemyAIWispSiphonsTorchFuelAndWendigoDropsAshJackpot) {
    Engine::Scene scene("WispAndWendigoTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    player->AddComponent<Engine::PointLightComponent>();
    player->AddComponent<PlayerStatsComponent>();
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;

    auto* wisp = scene.CreateGameObject("Wisp");
    wisp->GetTransform()->position = { 5.5f, 0.0f, 0.0f };
    auto* wRb = wisp->AddComponent<Engine::Rigidbody2D>();
    wRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    wisp->AddComponent<Engine::CircleCollider2D>()->Radius = 0.35f;
    auto* wispAI = wisp->AddComponent<EnemyAIController>();
    wispAI->ConfigureArchetype("enemy_wisp");

    auto* wendigo = scene.CreateGameObject("Wendigo");
    wendigo->GetTransform()->position = { 8.0f, 0.0f, 0.0f };
    auto* eRb = wendigo->AddComponent<Engine::Rigidbody2D>();
    eRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    wendigo->AddComponent<Engine::CircleCollider2D>()->Radius = 0.60f;
    auto* wendigoAI = wendigo->AddComponent<EnemyAIController>();
    wendigoAI->ConfigureArchetype("enemy_wendigo");

    scene.OnRuntimeStart();
    EXPECT_FLOAT_EQ(wispAI->GetStats().Health, 20.0f);
    EXPECT_FLOAT_EQ(wispAI->GetStats().LightVulnerability, 2.0f);
    EXPECT_FLOAT_EQ(wendigoAI->GetStats().Health, 180.0f);
    EXPECT_FLOAT_EQ(wendigoAI->GetStats().Mass, 90.0f);

    // Step 1.0s -> Wisp darts in and siphons -8 Torch Fuel
    for (int i = 0; i < 60; ++i) {
        scene.UpdateRuntime(1.0f / 60.0f);
    }
    EXPECT_LE(torch->Fuel, 92.0f);

    // Defeat Apex Wendigo -> drops guaranteed 5 AshEmber + 1 CrimsonBerry jackpot
    DamageEvent lethal;
    lethal.Amount = 300.0f;
    EXPECT_TRUE(wendigo->GetComponent<HealthComponent>()->ApplyDamage(lethal));
    EXPECT_GE(scene.FindGameObjectsWithTag(Engine::EntityTag::ResourcePickup).size(), 6u);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, WaveDirectorProgressesPhasesSpawnsPooledInDarknessAndTriggersDawn) {
    Engine::Scene scene("WaveDirectorTestScene");

    auto* moonGo = scene.CreateGameObject("MoonLight");
    auto* moon = moonGo->AddComponent<Engine::DirectionalLightComponent>();
    moon->SunIntensity = 0.25f;

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* torchLight = player->AddComponent<Engine::PointLightComponent>();
    torchLight->Radius = 7.5f;
    torchLight->Intensity = 2.2f;
    torchLight->Semantic = Engine::LightSemantic::SafeLifeline;

    auto* dirGo = scene.CreateGameObject("WaveDirector");
    auto* director = dirGo->AddComponent<WaveDirectorComponent>();
    bool dawnFired = false;
    director->OnDawnVictory = [&]() { dawnFired = true; };

    scene.OnRuntimeStart();
    scene.UpdateRuntime(1.0f / 60.0f);

    EXPECT_EQ(director->GetCurrentPhase(), NightPhase::Dusk);

    // Pooled enemy spawns outside SafeLifeline light & outside camera frustum (>= 11.5m)
    auto* ghoul = director->SpawnPooledEnemy("enemy_ghoul");
    ASSERT_NE(ghoul, nullptr);
    const glm::vec3 gPos = ghoul->GetTransform()->GetWorldPosition();
    EXPECT_FALSE(scene.EvaluateIlluminationAtXZ({ gPos.x, gPos.z }).IsInSafeLight(0.05f));
    EXPECT_GE(glm::length(glm::vec2(gPos.x, gPos.z)), 11.0f);

    // 1:15 -> Deep Night dims MoonLight
    director->AdvanceClockForTest(76.0f);
    EXPECT_EQ(director->GetCurrentPhase(), NightPhase::DeepNight);
    EXPECT_LT(moon->SunIntensity, 0.20f);

    // 2:45 -> Umbral Surge spawns Apex Wendigo boss
    director->AdvanceClockForTest(166.0f);
    EXPECT_EQ(director->GetCurrentPhase(), NightPhase::UmbralSurge);
    ASSERT_NE(director->GetActiveBoss(), nullptr);
    EXPECT_EQ(director->GetActiveBoss()->GetComponent<EnemyAIController>()->EnemyId, "enemy_wendigo");

    // 4:15 -> Eclipse Climax
    director->AdvanceClockForTest(256.0f);
    EXPECT_EQ(director->GetCurrentPhase(), NightPhase::EclipseClimax);

    // 5:00 -> Dawn Break triggers golden sunrise & dissolves remaining enemies
    director->AdvanceClockForTest(300.0f);
    EXPECT_EQ(director->GetCurrentPhase(), NightPhase::DawnBreak);
    EXPECT_TRUE(director->IsVictory());
    EXPECT_TRUE(dawnFired);
    EXPECT_GE(moon->SunIntensity, 1.0f);
    EXPECT_TRUE(ghoul->GetComponent<HealthComponent>()->IsDying());

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, RunProgressionDrafts3DistinctWeightedCardsAndAppliesSynergies) {
    Engine::Time::Reset();
    EXPECT_EQ(RunProgressionComponent::ComputeThresholdForLevel(1), 15);
    EXPECT_EQ(RunProgressionComponent::ComputeThresholdForLevel(2), 35);
    EXPECT_EQ(RunProgressionComponent::ComputeThresholdForLevel(3), 65);
    EXPECT_EQ(RunProgressionComponent::ComputeThresholdForLevel(4), 105);
    EXPECT_FLOAT_EQ(RunProgressionComponent::GetRarityWeight("Common"), 60.0f);
    EXPECT_FLOAT_EQ(RunProgressionComponent::GetRarityWeight("Rare"), 28.0f);
    EXPECT_FLOAT_EQ(RunProgressionComponent::GetRarityWeight("Epic"), 10.0f);
    EXPECT_FLOAT_EQ(RunProgressionComponent::GetRarityWeight("Legendary"), 2.0f);

    if (!Engine::GameplayData::IsInitialized()) {
        Engine::GameplayData::Init("Sandbox/assets/data");
    }

    Engine::Scene scene("RunProgressionTestScene");

    auto* player = scene.CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->AddComponent<Engine::PointLightComponent>();
    auto* stats = player->AddComponent<PlayerStatsComponent>();
    auto* weapon = player->AddComponent<WeaponController>();
    auto* prog = player->AddComponent<RunProgressionComponent>();
    auto* socket = scene.CreateGameObject("TorchSocket");
    socket->GetTransform()->SetParent(player->GetTransform(), false);
    auto* torch = socket->AddComponent<TorchComponent>();
    torch->BaseBurnRate = 0.0f;

    scene.OnRuntimeStart();

    // Grant 15 XP -> triggers Level 1 draft, pauses TimeScale to 0.0f, creates 3 distinct UI cards
    stats->AddXP(15);
    scene.UpdateRuntime(1.0f / 60.0f);

    EXPECT_TRUE(prog->IsDraftOpen());
    EXPECT_EQ(prog->GetLevel(), 1);
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.0f);

    const auto& cards = prog->GetCurrentCards();
    ASSERT_EQ(cards.size(), 3u);
    EXPECT_NE(cards[0].Id, cards[1].Id);
    EXPECT_NE(cards[1].Id, cards[2].Id);
    EXPECT_NE(cards[0].Id, cards[2].Id);

    const auto& buttons = prog->GetCardButtons();
    ASSERT_EQ(buttons.size(), 3u);
    auto* btn0 = buttons[0]->GetComponent<Engine::UIButtonComponent>();
    auto* btn1 = buttons[1]->GetComponent<Engine::UIButtonComponent>();
    ASSERT_NE(btn0, nullptr);
    ASSERT_NE(btn1, nullptr);
    EXPECT_TRUE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, scene));
    EXPECT_TRUE(btn0->IsFocused());
    EXPECT_TRUE(Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, scene));
    EXPECT_TRUE(btn1->IsFocused());

    // Click focused card -> closes draft and restores TimeScale to 1.0f
    btn1->Click();
    EXPECT_FALSE(prog->IsDraftOpen());
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 1.0f);

    // Verify standout synergy cards apply cleanly
    const float baseArc = weapon->GetEquippedWeapon().SwingArcDeg;
    prog->ApplyBranchCard({ "branch_cleaving_momentum", "Cleaving Momentum", "", "Rare", {}, "SwingArcDeg", 30.0f, "" });
    EXPECT_FLOAT_EQ(weapon->GetEquippedWeapon().SwingArcDeg, baseArc + 30.0f);

    prog->ApplyBranchCard({ "branch_ignited_edges", "Ignited Edges", "", "Rare", {}, "BurnDamageOnHit", 6.0f, "" });
    EXPECT_FLOAT_EQ(stats->BurnDamageOnHit, 6.0f);

    prog->ApplyBranchCard({ "branch_eternal_beacon", "Eternal Beacon", "", "Legendary", {}, "BeaconRegenRate", 2.0f, "" });
    torch->Fuel = 50.0f;
    scene.UpdateRuntime(1.0f / 60.0f);
    EXPECT_GT(torch->Fuel, 50.0f);

    scene.OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, AlphaHUDUpdatesBarsClockMinimapAndBossBar) {
    Engine::Time::Reset();
    auto scene = Sandbox::AlphaRunScene::Create(777ULL);
    ASSERT_NE(scene, nullptr);
    Sandbox::AlphaRunScene::BindRuntimeGameplayScripts(scene);

    AlphaHUDController* hud = nullptr;
    WaveDirectorComponent* director = nullptr;
    Engine::GameObject* player = nullptr;
    TorchComponent* torch = nullptr;

    for (const auto& go : scene->GetGameObjects()) {
        if (!go) continue;
        if (auto* h = go->GetComponent<AlphaHUDController>()) hud = h;
        if (auto* d = go->GetComponent<WaveDirectorComponent>()) director = d;
        if (go->GetName() == "Player") player = go.get();
        if (auto* t = go->GetComponent<TorchComponent>()) torch = t;
    }
    ASSERT_NE(hud, nullptr);
    ASSERT_NE(director, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_NE(torch, nullptr);

    auto* stats = player->GetComponent<PlayerStatsComponent>();
    ASSERT_NE(stats, nullptr);
    stats->HP = 50.0f;
    stats->Wood = 12;
    stats->RunAshes = 40;
    torch->Fuel = 15.0f; // Ash Frenzy
    torch->BaseBurnRate = 0.0f;

    scene->OnRuntimeStart();
    scene->UpdateRuntime(1.0f / 60.0f);

    ASSERT_NE(hud->GetHealthBar(), nullptr);
    ASSERT_NE(hud->GetTorchBar(), nullptr);
    ASSERT_NE(hud->GetRuneCooldownPanel(), nullptr);
    ASSERT_NE(hud->GetMinimap(), nullptr);
    ASSERT_NE(hud->GetRadialWheel(), nullptr);

    EXPECT_FLOAT_EQ(hud->GetHealthBar()->fillAmount, 0.50f);
    EXPECT_FLOAT_EQ(hud->GetTorchBar()->fillAmount, 0.15f);
    EXPECT_GT(hud->GetTorchBar()->color.r, 0.8f);
    EXPECT_LT(hud->GetTorchBar()->color.g, 0.4f); // Crimson Ash Frenzy tint
    EXPECT_EQ(hud->GetPhaseLabel()->text, "Dusk");
    EXPECT_NE(hud->GetResourcesLabel()->text.find("Wood: 12"), std::string::npos);

    // MinimapFogSystem explored origin and registered Sanctuary Brazier pin
    ASSERT_NE(hud->GetMinimap()->fogSystem, nullptr);
    EXPECT_TRUE(hud->GetMinimap()->fogSystem->IsDiscovered({ 0.0f, 2.5f }));
    EXPECT_GE(hud->GetMinimap()->fogSystem->GetActivePins().size(), 1u);

    // Advance WaveDirector to Umbral Surge -> Apex Wendigo Boss Bar appears
    director->AdvanceClockForTest(166.0f);
    scene->UpdateRuntime(1.0f / 60.0f);
    EXPECT_EQ(hud->GetPhaseLabel()->text, "Umbral Surge");
    EXPECT_FLOAT_EQ(hud->GetBossBar()->fillAmount, 1.0f);

    scene->OnRuntimeStop();
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, SanctuaryHubStationsSwapLoadoutPurchaseRootsAndTransitionToRun) {
    Engine::Time::Reset();
    const auto tempDir = std::filesystem::temp_directory_path() / "umbral_gam14_hub_test";
    std::filesystem::remove_all(tempDir);
    Engine::SaveManager::SetSaveDirectoryOverride(tempDir);

    auto& session = RunSessionManager::Get();
    session.Profile = Engine::PlayerProfileData{};
    session.Profile.BankedAshes = 250;
    session.SaveProfile();

    auto hub = Sandbox::AlphaRunScene::CreateSanctuaryHubScene();
    ASSERT_NE(hub, nullptr);
    Engine::SceneManager::LoadScene(hub);
    hub->OnRuntimeStart();

    SanctuaryStationComponent* weaponRack = nullptr;
    SanctuaryStationComponent* runePedestal = nullptr;
    SanctuaryStationComponent* rootAltar = nullptr;
    SanctuaryStationComponent* umbralGate = nullptr;
    TitleOverlayComponent* titleOverlay = nullptr;
    Engine::GameObject* player = nullptr;

    for (const auto& go : hub->GetGameObjects()) {
        if (!go) continue;
        if (go->GetName() == "Player") player = go.get();
        if (auto* s = go->GetComponent<SanctuaryStationComponent>()) {
            switch (s->Kind) {
                case SanctuaryStationKind::WeaponRack:   weaponRack = s; break;
                case SanctuaryStationKind::RunePedestal: runePedestal = s; break;
                case SanctuaryStationKind::RootAltar:    rootAltar = s; break;
                case SanctuaryStationKind::UmbralGate:   umbralGate = s; break;
                case SanctuaryStationKind::TitleOverlay: titleOverlay = s; break;
            }
        }
    }
    ASSERT_NE(player, nullptr);
    ASSERT_NE(titleOverlay, nullptr);
    ASSERT_NE(weaponRack, nullptr);
    ASSERT_NE(runePedestal, nullptr);
    ASSERT_NE(rootAltar, nullptr);
    ASSERT_NE(umbralGate, nullptr);

    titleOverlay->DismissTitle();
    EXPECT_FALSE(titleOverlay->IsOpen());

    // Weapon Rack & Rune Pedestal cycle loadout
    EXPECT_TRUE(weaponRack->Interact());
    EXPECT_EQ(session.SelectedWeaponId, "ashwood_halberd");
    EXPECT_TRUE(runePedestal->Interact());
    EXPECT_EQ(session.SelectedRuneId, "rune_chronos");

    // Root Altar purchases Iron Sinew (100 Ashes -> 150 remaining)
    rootAltar->ActiveRootId = "root_iron_sinew";
    EXPECT_TRUE(rootAltar->Interact());
    EXPECT_EQ(session.GetRootRank("root_iron_sinew"), 1u);
    EXPECT_EQ(session.Profile.BankedAshes, 150u);

    // Northern Umbral Gate triggers 0.45s SceneManager::TransitionToScene
    EXPECT_TRUE(umbralGate->Interact());
    EXPECT_TRUE(Engine::SceneManager::IsTransitioning());
    for (int i = 0; i < 35; ++i) {
        Engine::SceneManager::UpdateTransition(1.0f / 60.0f);
    }
    auto activeRun = Engine::SceneManager::GetActiveScene();
    ASSERT_NE(activeRun, nullptr);
    EXPECT_EQ(activeRun->GetName(), "AlphaRun");

    Engine::SceneManager::Shutdown();
    Engine::SaveManager::ClearSaveDirectoryOverride();
    std::filesystem::remove_all(tempDir);
    Engine::Time::Reset();
}

TEST(SandboxUnitTest, RunSummaryBanksAshesOnDeathOrDawnAndTransitionsBackToSanctuary) {
    Engine::Time::Reset();
    const auto tempDir = std::filesystem::temp_directory_path() / "umbral_gam14_summary_test";
    std::filesystem::remove_all(tempDir);
    Engine::SaveManager::SetSaveDirectoryOverride(tempDir);

    auto& session = RunSessionManager::Get();
    session.Profile = Engine::PlayerProfileData{};
    session.Profile.BankedAshes = 50;
    session.SaveProfile();

    auto runScene = Sandbox::AlphaRunScene::Create(99ULL);
    Sandbox::AlphaRunScene::BindRuntimeGameplayScripts(runScene);
    Engine::SceneManager::LoadScene(runScene);
    runScene->OnRuntimeStart();

    RunSummaryController* summary = nullptr;
    Engine::GameObject* player = nullptr;
    for (const auto& go : runScene->GetGameObjects()) {
        if (!go) continue;
        if (auto* s = go->GetComponent<RunSummaryController>()) summary = s;
        if (go->GetName() == "Player") player = go.get();
    }
    ASSERT_NE(summary, nullptr);
    ASSERT_NE(player, nullptr);

    auto* stats = player->GetComponent<PlayerStatsComponent>();
    ASSERT_NE(stats, nullptr);
    stats->RunAshes = 75;
    stats->HP = 0.0f;

    runScene->UpdateRuntime(1.0f / 60.0f);
    EXPECT_TRUE(summary->IsSummaryOpen());
    EXPECT_FLOAT_EQ(Engine::Time::GetTimeScale(), 0.0f);

    Engine::PlayerProfileData reloaded;
    ASSERT_TRUE(Engine::SaveManager::LoadProfile(0, reloaded));
    EXPECT_EQ(reloaded.BankedAshes, 125u); // 50 + 75

    // Return to Sanctuary transitions back cleanly
    summary->TriggerReturnToSanctuary();
    EXPECT_TRUE(Engine::SceneManager::IsTransitioning());
    for (int i = 0; i < 35; ++i) {
        Engine::SceneManager::UpdateTransition(1.0f / 60.0f);
    }
    auto hubScene = Engine::SceneManager::GetActiveScene();
    ASSERT_NE(hubScene, nullptr);
    EXPECT_EQ(hubScene->GetName(), "SanctuaryHub");

    Engine::SceneManager::Shutdown();
    Engine::SaveManager::ClearSaveDirectoryOverride();
    std::filesystem::remove_all(tempDir);
    Engine::Time::Reset();
}

} // namespace
