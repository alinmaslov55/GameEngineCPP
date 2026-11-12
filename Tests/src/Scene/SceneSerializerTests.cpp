#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/TerrainChunkManager.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Core/ResourceManager.h"
#include <box2d/box2d.h>
#include <filesystem>
#include <memory>
#include <string>

namespace {

TEST(SceneSerializerTest, SerializeAndDeserializeRoundTripPreservesSceneAndComponents) {
    auto sourceScene = std::make_shared<Engine::Scene>("TestSerializedLevel");
    sourceScene->SetOrthographicSize(2.75f);

    // Entity 1: Tilemap with mixed walkability + coexisting Splatmap TerrainComponent
    constexpr uint64_t kMapUUID = 111111111111ULL;
    auto* mapEntity = sourceScene->CreateGameObjectWithUUID(kMapUUID, "WorldMap");
    auto* map = mapEntity->AddComponent<Engine::Tilemap>(4, 3);
    map->SetTile(0, 0, 1, true);
    map->SetTile(1, 2, 2, false);
    map->SetTile(3, 1, 3, false);

    auto* terrain = mapEntity->AddComponent<Engine::TerrainComponent>(12.0f, 10.0f, 32u, 32u);
    terrain->SetBlendSharpness(0.74f);
    terrain->SetEdgeDarkening(0.22f);
    terrain->SetLayerTilingScale(2u, 0.62f);
    terrain->PaintWorldBrush(6.0f, 5.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 2u, 2.5f, 0.5f, 1.0f);
    const glm::u8vec4 expectedCenterWeights = terrain->GetSplatTexel(16u, 16u);
    EXPECT_EQ(expectedCenterWeights.b, 255u);

    // Entity 2: Dynamic actor with Transform, BoxCollider2D, ParticleSystem, CameraComponent
    constexpr uint64_t kHeroUUID = 222222222222ULL;
    auto* hero = sourceScene->CreateGameObjectWithUUID(kHeroUUID, "Hero");
    hero->GetTransform()->position = {3.5f, 1.25f, -2.0f};
    hero->GetTransform()->rotation = {0.0f, 45.0f, 15.0f};
    hero->GetTransform()->scale = {1.5f, 2.0f, 1.25f};

    auto* rb = hero->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->FixedRotation = true;
    rb->UseGravityY = true;
    rb->GravityY = -14.5f;
    rb->GroundLevelY = 0.25f;

    auto* box = hero->AddComponent<Engine::BoxCollider2D>();
    box->size = {0.8f, 0.9f};
    box->offset = {0.1f, -0.2f};
    box->Density = 2.5f;
    box->Friction = 0.4f;
    box->Restitution = 0.75f;
    box->RestitutionThreshold = 0.45f;

    auto* heroSprite = hero->AddComponent<Engine::SpriteRenderer>();
    heroSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::CylindricalY);
    heroSprite->SetAlphaCutoff(0.32f);
    heroSprite->SetCastShadows(false);
    heroSprite->SetReceiveLighting(true);
    heroSprite->SetEmissive(0.65f);
    heroSprite->SetWindSwayWeight(0.78f);
    heroSprite->SetWindPhaseOffset(1.85f);

    auto* heroSquash = hero->AddComponent<Engine::SquashStretchComponent>();
    heroSquash->Stiffness = 210.0f;
    heroSquash->Damping = 16.5f;
    heroSquash->WalkTiltDegrees = 6.2f;
    heroSquash->JumpLaunchStretch = 2.65f;

    auto* heroParticles = hero->AddComponent<Engine::ParticleSystem>(256);
    heroParticles->AutoEmit = true;
    heroParticles->EmitRate = 12.5f;
    heroParticles->SpawnOffset = { 0.1f, 1.2f, -0.3f };
    heroParticles->EmissiveParticles = true;

    auto* heroPointLight = hero->AddComponent<Engine::PointLightComponent>();
    heroPointLight->Color = { 1.0f, 0.55f, 0.22f };
    heroPointLight->Intensity = 2.15f;
    heroPointLight->Radius = 5.25f;
    heroPointLight->Falloff = 1.65f;
    heroPointLight->HeightOffset = 0.95f;
    heroPointLight->Priority = Engine::LightPriority::Critical_HeroTorch;
    heroPointLight->Semantic = Engine::LightSemantic::SafeLifeline;
    heroPointLight->CastShadows = true;
    heroPointLight->ShadowSoftness = 1.85f;
    heroPointLight->ShadowBias = 0.0031f;
    heroPointLight->Flicker = true;
    heroPointLight->FlickerSpeed = 9.0f;
    heroPointLight->FlickerAmount = 0.22f;

    auto* cam = hero->AddComponent<Engine::CameraComponent>();
    cam->FOV = 36.0f;
    cam->Distance = 18.5f;
    cam->Pitch = 42.0f;
    cam->Yaw = -25.0f;
    cam->FixedAspectRatio = true;

    // Entity 3: Multiple UIPanels + DirectionalLightComponent on HUD/Environment GameObject
    auto* hud = sourceScene->CreateGameObject("HUD");
    hud->AddComponent<Engine::UIPanel>(glm::vec2{20.0f, 40.0f}, glm::vec2{200.0f, 30.0f}, glm::vec4{0.2f, 0.3f, 0.4f, 0.9f});
    hud->AddComponent<Engine::UIPanel>(glm::vec2{24.0f, 44.0f}, glm::vec2{120.0f, 22.0f}, glm::vec4{0.1f, 0.9f, 0.2f, 1.0f});
    auto* sun = hud->AddComponent<Engine::DirectionalLightComponent>();
    sun->ElevationDegrees = 34.5f;
    sun->AzimuthDegrees = -52.0f;
    sun->SunColor = { 1.0f, 0.86f, 0.68f };
    sun->SunIntensity = 1.15f;
    sun->AmbientColor = { 0.32f, 0.38f, 0.64f };
    sun->AmbientIntensity = 0.58f;
    sun->CastShadows = true;
    sun->ShadowSoftness = 1.85f;
    sun->ShadowBias = 0.0024f;

    auto* pp = hud->AddComponent<Engine::PostProcessComponent>();
    pp->ApplyPreset(Engine::PostProcessComponent::Preset::BloodMoonRitual);
    pp->Exposure = 0.24f;
    pp->BloomIntensity = 0.67f;
    pp->BloomScatter = 0.81f;
    pp->VignetteIntensity = 0.44f;

    auto* windZone = hud->AddComponent<Engine::WindComponent>();
    windZone->ApplyPreset(Engine::WindComponent::Preset::AutumnGusts);
    windZone->PlayerInteractionRadius = 1.65f;
    windZone->PlayerPushStrength = 0.46f;

    // 1. Test In-Memory Round Trip (used by Play / Stop mode)
    Engine::SceneSerializer sourceSerializer(sourceScene);
    const std::string jsonPayload = sourceSerializer.SerializeToString();
    ASSERT_FALSE(jsonPayload.empty());

    auto clonedScene = std::make_shared<Engine::Scene>("Placeholder");
    Engine::SceneSerializer cloneSerializer(clonedScene);
    ASSERT_TRUE(cloneSerializer.DeserializeFromString(jsonPayload));

    EXPECT_EQ(clonedScene->GetName(), "TestSerializedLevel");
    EXPECT_NEAR(clonedScene->GetOrthographicSize(), 2.75f, 1e-5f);
    ASSERT_EQ(clonedScene->GetGameObjects().size(), 3u);

    auto* clonedMapObj = clonedScene->GetGameObjects()[0].get();
    EXPECT_EQ(clonedMapObj->GetUUID(), kMapUUID);
    auto* clonedMap = clonedMapObj->GetComponent<Engine::Tilemap>();
    ASSERT_NE(clonedMap, nullptr);
    EXPECT_EQ(clonedMap->GetWidth(), 4u);
    EXPECT_EQ(clonedMap->GetHeight(), 3u);
    EXPECT_EQ(clonedMap->GetTile(0, 0).id, 1u);
    EXPECT_TRUE(clonedMap->GetTile(0, 0).walkable);
    EXPECT_EQ(clonedMap->GetTile(1, 2).id, 2u);
    EXPECT_FALSE(clonedMap->GetTile(1, 2).walkable);
    EXPECT_EQ(clonedMap->GetTile(3, 1).id, 3u);
    EXPECT_FALSE(clonedMap->GetTile(3, 1).walkable);

    auto* clonedTerrain = clonedMapObj->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(clonedTerrain, nullptr);
    EXPECT_NEAR(clonedTerrain->GetWidth(), 12.0f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetDepth(), 10.0f, 1e-5f);
    EXPECT_EQ(clonedTerrain->GetSplatWidth(), 32u);
    EXPECT_EQ(clonedTerrain->GetSplatHeight(), 32u);
    EXPECT_NEAR(clonedTerrain->GetBlendSharpness(), 0.74f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetEdgeDarkening(), 0.22f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetLayer(2u).TilingScale, 0.62f, 1e-5f);
    EXPECT_EQ(clonedTerrain->GetSplatTexel(16u, 16u), expectedCenterWeights);

    auto* clonedHero = clonedScene->GetGameObjects()[1].get();
    EXPECT_EQ(clonedHero->GetUUID(), kHeroUUID);
    EXPECT_EQ(clonedHero->GetName(), "Hero");
    EXPECT_NEAR(clonedHero->GetTransform()->position.x, 3.5f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->position.y, 1.25f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->position.z, -2.0f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->rotation.y, 45.0f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->scale.y, 2.0f, 1e-5f);

    auto* clonedRb = clonedHero->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(clonedRb, nullptr);
    EXPECT_EQ(clonedRb->Type, Engine::Rigidbody2D::BodyType::Dynamic);
    EXPECT_TRUE(clonedRb->FixedRotation);
    EXPECT_TRUE(clonedRb->UseGravityY);
    EXPECT_NEAR(clonedRb->GravityY, -14.5f, 1e-5f);
    EXPECT_NEAR(clonedRb->GroundLevelY, 0.25f, 1e-5f);

    auto* clonedBox = clonedHero->GetComponent<Engine::BoxCollider2D>();
    ASSERT_NE(clonedBox, nullptr);
    EXPECT_NEAR(clonedBox->Density, 2.5f, 1e-5f);
    EXPECT_NEAR(clonedBox->Friction, 0.4f, 1e-5f);
    EXPECT_NEAR(clonedBox->Restitution, 0.75f, 1e-5f);
    EXPECT_NEAR(clonedBox->RestitutionThreshold, 0.45f, 1e-5f);
    EXPECT_NEAR(clonedBox->size.x, 0.8f, 1e-5f);
    EXPECT_NEAR(clonedBox->offset.y, -0.2f, 1e-5f);

    auto* clonedSprite = clonedHero->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(clonedSprite, nullptr);
    EXPECT_EQ(clonedSprite->GetBillboardMode(), Engine::SpriteRenderer::BillboardMode::CylindricalY);
    EXPECT_NEAR(clonedSprite->GetAlphaCutoff(), 0.32f, 1e-5f);
    EXPECT_FALSE(clonedSprite->GetCastShadows());
    EXPECT_TRUE(clonedSprite->GetReceiveLighting());
    EXPECT_NEAR(clonedSprite->GetEmissive(), 0.65f, 1e-5f);
    EXPECT_NEAR(clonedSprite->GetWindSwayWeight(), 0.78f, 1e-5f);
    EXPECT_NEAR(clonedSprite->GetWindPhaseOffset(), 1.85f, 1e-5f);

    auto* clonedSquash = clonedHero->GetComponent<Engine::SquashStretchComponent>();
    ASSERT_NE(clonedSquash, nullptr);
    EXPECT_NEAR(clonedSquash->Stiffness, 210.0f, 1e-5f);
    EXPECT_NEAR(clonedSquash->Damping, 16.5f, 1e-5f);
    EXPECT_NEAR(clonedSquash->WalkTiltDegrees, 6.2f, 1e-5f);
    EXPECT_NEAR(clonedSquash->JumpLaunchStretch, 2.65f, 1e-5f);

    auto* clonedParticles = clonedHero->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(clonedParticles, nullptr);
    EXPECT_EQ(clonedParticles->GetParticles().size(), 256u);
    EXPECT_TRUE(clonedParticles->AutoEmit);
    EXPECT_NEAR(clonedParticles->EmitRate, 12.5f, 1e-5f);
    EXPECT_NEAR(clonedParticles->SpawnOffset.y, 1.2f, 1e-5f);
    EXPECT_TRUE(clonedParticles->EmissiveParticles);

    auto* clonedPointLight = clonedHero->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(clonedPointLight, nullptr);
    EXPECT_NEAR(clonedPointLight->Color.g, 0.55f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Intensity, 2.15f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Radius, 5.25f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Falloff, 1.65f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->HeightOffset, 0.95f, 1e-5f);
    EXPECT_EQ(clonedPointLight->Priority, Engine::LightPriority::Critical_HeroTorch);
    EXPECT_EQ(clonedPointLight->Semantic, Engine::LightSemantic::SafeLifeline);
    EXPECT_TRUE(clonedPointLight->CastShadows);
    EXPECT_NEAR(clonedPointLight->ShadowSoftness, 1.85f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->ShadowBias, 0.0031f, 1e-5f);
    EXPECT_TRUE(clonedPointLight->Flicker);
    EXPECT_NEAR(clonedPointLight->FlickerSpeed, 9.0f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->FlickerAmount, 0.22f, 1e-5f);

    auto* clonedCam = clonedHero->GetComponent<Engine::CameraComponent>();
    ASSERT_NE(clonedCam, nullptr);
    EXPECT_NEAR(clonedCam->FOV, 36.0f, 1e-5f);
    EXPECT_NEAR(clonedCam->Distance, 18.5f, 1e-5f);
    EXPECT_NEAR(clonedCam->Pitch, 42.0f, 1e-5f);
    EXPECT_NEAR(clonedCam->Yaw, -25.0f, 1e-5f);
    EXPECT_TRUE(clonedCam->FixedAspectRatio);

    auto* clonedHud = clonedScene->GetGameObjects()[2].get();
    const auto panels = clonedHud->GetComponents<Engine::UIPanel>();
    ASSERT_EQ(panels.size(), 2u);
    EXPECT_NEAR(panels[0]->size.x, 200.0f, 1e-5f);
    EXPECT_NEAR(panels[1]->size.x, 120.0f, 1e-5f);

    auto* clonedSun = clonedHud->GetComponent<Engine::DirectionalLightComponent>();
    ASSERT_NE(clonedSun, nullptr);
    EXPECT_NEAR(clonedSun->ElevationDegrees, 34.5f, 1e-5f);
    EXPECT_NEAR(clonedSun->AzimuthDegrees, -52.0f, 1e-5f);
    EXPECT_NEAR(clonedSun->SunColor.b, 0.68f, 1e-5f);
    EXPECT_NEAR(clonedSun->SunIntensity, 1.15f, 1e-5f);
    EXPECT_NEAR(clonedSun->AmbientColor.b, 0.64f, 1e-5f);
    EXPECT_NEAR(clonedSun->AmbientIntensity, 0.58f, 1e-5f);
    EXPECT_TRUE(clonedSun->CastShadows);
    EXPECT_NEAR(clonedSun->ShadowSoftness, 1.85f, 1e-5f);
    EXPECT_NEAR(clonedSun->ShadowBias, 0.0024f, 1e-5f);

    auto* clonedPP = clonedHud->GetComponent<Engine::PostProcessComponent>();
    ASSERT_NE(clonedPP, nullptr);
    EXPECT_TRUE(clonedPP->Enabled);
    EXPECT_TRUE(clonedPP->BloomEnabled);
    EXPECT_EQ(clonedPP->ToneMapping, Engine::PostProcessComponent::ToneMapMode::ACESFilmic);
    EXPECT_NEAR(clonedPP->Exposure, 0.24f, 1e-5f);
    EXPECT_NEAR(clonedPP->BloomIntensity, 0.67f, 1e-5f);
    EXPECT_NEAR(clonedPP->BloomScatter, 0.81f, 1e-5f);
    EXPECT_NEAR(clonedPP->Contrast, 1.16f, 1e-5f);
    EXPECT_NEAR(clonedPP->VignetteIntensity, 0.44f, 1e-5f);
    EXPECT_NEAR(clonedPP->ShadowTint.r, 0.88f, 1e-5f);
    EXPECT_NEAR(clonedPP->HighlightTint.r, 1.10f, 1e-5f);

    auto* clonedWind = clonedHud->GetComponent<Engine::WindComponent>();
    ASSERT_NE(clonedWind, nullptr);
    EXPECT_TRUE(clonedWind->Enabled);
    EXPECT_NEAR(clonedWind->Strength, 0.28f, 1e-5f);
    EXPECT_NEAR(clonedWind->Gustiness, 0.82f, 1e-5f);
    EXPECT_NEAR(clonedWind->PlayerInteractionRadius, 1.65f, 1e-5f);
    EXPECT_NEAR(clonedWind->PlayerPushStrength, 0.46f, 1e-5f);

    // 2. Test Disk File Round Trip (including external PNG splatmap persistence)
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_serializer_terrain_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path prevAssetRoot = Engine::ResourceManager::GetAssetRoot();
    Engine::ResourceManager::SetAssetRoot(tempDir);

    ASSERT_TRUE(sourceSerializer.Serialize("roundtrip.scene"));

    auto diskScene = std::make_shared<Engine::Scene>();
    Engine::SceneSerializer diskSerializer(diskScene);
    ASSERT_TRUE(diskSerializer.Deserialize("roundtrip.scene"));
    EXPECT_EQ(diskScene->GetName(), "TestSerializedLevel");
    ASSERT_EQ(diskScene->GetGameObjects().size(), 3u);

    auto* diskTerrain = diskScene->GetGameObjects()[0]->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(diskTerrain, nullptr);
    EXPECT_EQ(diskTerrain->GetSplatTexel(16u, 16u), expectedCenterWeights);
    EXPECT_FALSE(diskTerrain->HasUnsavedMemoryEdits());

    Engine::ResourceManager::SetAssetRoot(prevAssetRoot);
    std::filesystem::remove_all(tempDir, ec);
}

TEST(SceneSerializerTest, TransformSnapshotPreviousAndMidAirVelocityRoundTrip) {
    auto sourceScene = std::make_shared<Engine::Scene>("MidAirSaveScene");

    // 1. Hierarchical non-origin Transform state
    auto* parent = sourceScene->CreateGameObjectWithUUID(10101ULL, "Parent");
    parent->GetTransform()->position = { 12.5f, 3.25f, -7.5f };
    parent->GetTransform()->rotation = { 10.0f, 45.0f, 5.0f };
    parent->GetTransform()->scale = { 2.0f, 1.5f, 1.0f };

    auto* child = sourceScene->CreateGameObjectWithUUID(20202ULL, "Child");
    child->GetTransform()->position = { 1.0f, 2.0f, 0.0f };
    child->GetTransform()->SetParent(parent->GetTransform(), /*keepWorldTransform=*/false);

    // 2. Active mid-air Dynamic Rigidbody2D (XZ + vertical velocities) saved mid-flight
    sourceScene->OnRuntimeStart();

    auto* jumper = sourceScene->CreateGameObjectWithUUID(30303ULL, "MidAirJumper");
    jumper->GetTransform()->position = { 2.0f, 4.0f, -1.0f };
    auto* rb = jumper->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->LinearDamping = 0.0f;
    rb->UseGravityY = true;
    rb->GravityY = -10.0f;
    rb->GroundLevelY = 0.0f;
    rb->AirborneClearanceHeight = 0.75f;
    auto* col = jumper->AddComponent<Engine::BoxCollider2D>();
    col->size = { 1.0f, 1.0f };
    sourceScene->RegisterRuntimeBody(jumper);

    rb->SetVelocityXZ({ 6.0f, -3.0f });
    rb->LaunchVertical(9.5f);

    const std::string savedJson = Engine::SceneSerializer(sourceScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());
    sourceScene->OnRuntimeStop();

    // Deserialize into fresh scene
    auto loadedScene = std::make_shared<Engine::Scene>("LoadedMidAirScene");
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));

    // Transform prevPosition/prevRotation must match loaded values immediately (zero frame-0 origin snap)
    auto* loadedParent = loadedScene->FindGameObjectByUUID(10101ULL);
    auto* loadedChild = loadedScene->FindGameObjectByUUID(20202ULL);
    ASSERT_NE(loadedParent, nullptr);
    ASSERT_NE(loadedChild, nullptr);

    const glm::vec3 interpPos0 = loadedParent->GetTransform()->GetInterpolatedPosition(0.0f);
    const glm::vec3 interpPosHalf = loadedParent->GetTransform()->GetInterpolatedPosition(0.5f);
    const glm::vec3 interpRot0 = loadedParent->GetTransform()->GetInterpolatedRotation(0.0f);
    EXPECT_NEAR(interpPos0.x, 12.5f, 1e-4f);
    EXPECT_NEAR(interpPos0.y, 3.25f, 1e-4f);
    EXPECT_NEAR(interpPos0.z, -7.5f, 1e-4f);
    EXPECT_NEAR(interpPosHalf.x, 12.5f, 1e-4f);
    EXPECT_NEAR(interpRot0.y, 45.0f, 1e-4f);
    EXPECT_GT(glm::length(loadedChild->GetTransform()->GetWorldPosition() - loadedParent->GetTransform()->position), 0.1f);

    // Mid-air Rigidbody2D XZ & Y velocities must survive save/load and continue airborne arc cleanly
    auto* loadedJumper = loadedScene->FindGameObjectByUUID(30303ULL);
    ASSERT_NE(loadedJumper, nullptr);
    auto* loadedRb = loadedJumper->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(loadedRb, nullptr);
    EXPECT_NEAR(loadedRb->InitialLinearVelocityXZ.x, 6.0f, 1e-3f);
    EXPECT_NEAR(loadedRb->InitialLinearVelocityXZ.y, -3.0f, 1e-3f);
    EXPECT_NEAR(loadedRb->VerticalVelocity, 9.5f, 1e-4f);
    EXPECT_NEAR(loadedJumper->GetTransform()->GetInterpolatedPosition(0.0f).y, 4.0f, 1e-4f);

    loadedScene->OnRuntimeStart();
    ASSERT_TRUE(loadedRb->RuntimeBody.IsValid());
    const b2Vec2 b2Vel = b2Body_GetLinearVelocity(b2LoadBodyId(loadedRb->RuntimeBody.Value));
    EXPECT_NEAR(b2Vel.x, 6.0f, 1e-3f);
    EXPECT_NEAR(b2Vel.y, -3.0f, 1e-3f);
    EXPECT_TRUE(loadedRb->IsAirbornePassThroughActive());

    // Step 1/60s: entity continues upward from y = 4.0f and moves along +X, -Z
    loadedScene->UpdateRuntime(1.0f / 60.0f);
    EXPECT_GT(loadedJumper->GetTransform()->position.y, 4.0f);
    EXPECT_GT(loadedJumper->GetTransform()->position.x, 2.0f);
    EXPECT_LT(loadedJumper->GetTransform()->position.z, -1.0f);

    loadedScene->OnRuntimeStop();
}

TEST(SceneSerializerTest, SandboxScenesLoadAndSimulateFeatureScenarios) {
    const std::filesystem::path prevAssetRoot = Engine::ResourceManager::GetAssetRoot();
    struct AssetRootGuard {
        std::filesystem::path prev;
        ~AssetRootGuard() {
            if (!prev.empty()) {
                Engine::ResourceManager::SetAssetRoot(prev);
            }
        }
    } assetRootGuard{ prevAssetRoot };

    const std::filesystem::path sandboxRoot = Engine::ResourceManager::DiscoverAssetRoot("Sandbox");
    ASSERT_TRUE(std::filesystem::exists(sandboxRoot / "Village.scene"));
    ASSERT_TRUE(std::filesystem::exists(sandboxRoot / "assets" / "level.scene"));

    auto findByName = [](Engine::Scene& scene, const std::string& name) -> Engine::GameObject* {
        for (const auto& go : scene.GetGameObjects()) {
            if (go->GetName() == name) {
                return go.get();
            }
        }
        return nullptr;
    };

    // 1. Verify rewritten Sandbox/assets/level.scene (Engine Feature Verification Arena)
    {
        auto levelScene = std::make_shared<Engine::Scene>("LevelLoader");
        ASSERT_TRUE(Engine::SceneSerializer(levelScene).Deserialize("assets/level.scene"));
        EXPECT_GE(levelScene->GetGameObjects().size(), 25u);

        auto* player = levelScene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(player->GetName(), "Player");
        EXPECT_EQ(player->GetLayer(), Engine::EntityLayer::Player);

        auto* torchSocket = findByName(*levelScene, "Player Torch Socket");
        ASSERT_NE(torchSocket, nullptr);
        EXPECT_EQ(torchSocket->GetTransform()->GetParent(), player->GetTransform());

        auto* shrine = findByName(*levelScene, "Hierarchy Shrine Parent");
        ASSERT_NE(shrine, nullptr);
        EXPECT_EQ(shrine->GetTransform()->GetChildren().size(), 2u);

        auto* stasisRelic = findByName(*levelScene, "Stasis MidAir Relic");
        ASSERT_NE(stasisRelic, nullptr);
        auto* stasisRb = stasisRelic->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(stasisRb, nullptr);
        EXPECT_TRUE(stasisRb->IsStasisFrozen());
        EXPECT_FLOAT_EQ(stasisRb->GetStasisCachedVerticalVelocity(), 5.5f);
        const glm::vec3 frozenPos = stasisRelic->GetTransform()->position;

        auto* dropPickup = findByName(*levelScene, "Bouncing MidAir Drop");
        ASSERT_NE(dropPickup, nullptr);
        auto* dropRb = dropPickup->GetComponent<Engine::Rigidbody2D>();
        ASSERT_NE(dropRb, nullptr);
        EXPECT_GT(dropRb->VerticalVelocity, 0.0f);

        auto* tilemapGo = findByName(*levelScene, "Arena Tilemap");
        ASSERT_NE(tilemapGo, nullptr);
        auto* tilemap = tilemapGo->GetComponent<Engine::Tilemap>();
        ASSERT_NE(tilemap, nullptr);
        EXPECT_FALSE(tilemap->GetTile(0, 0).walkable);

        auto* terrainGo = findByName(*levelScene, "Arena Terrain");
        ASSERT_NE(terrainGo, nullptr);
        EXPECT_NE(terrainGo->GetComponent<Engine::TerrainComponent>(), nullptr);

        // Verify simulation runtime: stasis stays locked, bouncing drop integrates, player jump clears LowObstacle
        levelScene->OnRuntimeStart();
        player->GetComponent<Engine::Rigidbody2D>()->LaunchVertical(5.0f);

        bool sawAirbornePassThrough = false;
        constexpr float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i) {
            levelScene->UpdateRuntime(dt);
            if (player->GetComponent<Engine::Rigidbody2D>()->IsAirbornePassThroughActive()) {
                sawAirbornePassThrough = true;
            }
        }
        EXPECT_TRUE(sawAirbornePassThrough);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.x, frozenPos.x);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.y, frozenPos.y);
        EXPECT_FLOAT_EQ(stasisRelic->GetTransform()->position.z, frozenPos.z);
        EXPECT_NE(dropPickup->GetTransform()->position.y, 3.0f);

        // Spatial query check over LowObstacle barrier
        std::vector<Engine::GameObject*> lowHits;
        levelScene->OverlapCircleXZ({ 7.5f, 9.6f }, 1.2f, Engine::PhysicsLayer::LowObstacle, lowHits);
        EXPECT_FALSE(lowHits.empty());

        levelScene->OnRuntimeStop();
    }

    // 2. Verify rewritten Sandbox/Village.scene (Enhanced Cult Diorama Clearing)
    {
        auto villageScene = std::make_shared<Engine::Scene>("VillageLoader");
        ASSERT_TRUE(Engine::SceneSerializer(villageScene).Deserialize("Village.scene"));
        EXPECT_GE(villageScene->GetGameObjects().size(), 40u);

        auto* player = villageScene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(player->GetLayer(), Engine::EntityLayer::Player);

        auto* shrine = villageScene->FindFirstGameObjectWithTag(Engine::EntityTag::Shrine);
        ASSERT_NE(shrine, nullptr);
        EXPECT_GE(shrine->GetTransform()->GetChildren().size(), 1u);

        auto* shrineSensor = findByName(*villageScene, "Shrine Sanctuary Sensor");
        ASSERT_NE(shrineSensor, nullptr);
        auto* sensorCol = shrineSensor->GetComponent<Engine::CircleCollider2D>();
        ASSERT_NE(sensorCol, nullptr);
        EXPECT_TRUE(sensorCol->IsSensor);

        auto* westBarricade = findByName(*villageScene, "Village Barricade West");
        ASSERT_NE(westBarricade, nullptr);
        EXPECT_EQ(westBarricade->GetLayer(), Engine::EntityLayer::LowObstacle);
        EXPECT_NE(westBarricade->GetComponent<Engine::CapsuleCollider2D>(), nullptr);

        auto* stasisShard = findByName(*villageScene, "Chronos Stasis Shard");
        ASSERT_NE(stasisShard, nullptr);
        EXPECT_TRUE(stasisShard->GetComponent<Engine::Rigidbody2D>()->IsStasisFrozen());

        villageScene->OnRuntimeStart();
        player->GetComponent<Engine::Rigidbody2D>()->LaunchVertical(5.0f);
        bool villageVaultActive = false;
        for (int i = 0; i < 60; ++i) {
            villageScene->UpdateRuntime(1.0f / 60.0f);
            if (player->GetComponent<Engine::Rigidbody2D>()->IsAirbornePassThroughActive()) {
                villageVaultActive = true;
            }
        }
        EXPECT_TRUE(villageVaultActive);
        villageScene->OnRuntimeStop();
    }
}

TEST(SceneSerializerTest, PointLightSemanticRoundTripAndDefaultFallback) {
    auto srcScene = std::make_shared<Engine::Scene>("SemanticTestScene");
    auto* wisp = srcScene->CreateGameObjectWithUUID(70707ULL, "WispLure");
    auto* wispLight = wisp->AddComponent<Engine::PointLightComponent>();
    wispLight->Semantic = Engine::LightSemantic::DeceptiveHazard;
    wispLight->Radius = 4.25f;
    wispLight->Intensity = 1.4f;

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedWisp = loadedScene->FindGameObjectByUUID(70707ULL);
    ASSERT_NE(loadedWisp, nullptr);
    auto* loadedLight = loadedWisp->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(loadedLight, nullptr);
    EXPECT_EQ(loadedLight->Semantic, Engine::LightSemantic::DeceptiveHazard);

    // Legacy scene payload omitting "Semantic" must default to NeutralShrine
    const std::string legacyJson = R"({
        "SceneName": "LegacyPointLight",
        "GameObjects": [
            {
                "UUID": 80808,
                "Name": "OldBrazier",
                "Components": {
                    "PointLight": {
                        "Color": [1.0, 0.5, 0.2],
                        "Intensity": 1.5,
                        "Radius": 5.0,
                        "Falloff": 1.8
                    }
                }
            }
        ]
    })";
    auto legacyScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(legacyScene).DeserializeFromString(legacyJson));
    auto* legacyGo = legacyScene->FindGameObjectByUUID(80808ULL);
    ASSERT_NE(legacyGo, nullptr);
    auto* legacyPl = legacyGo->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(legacyPl, nullptr);
    EXPECT_EQ(legacyPl->Semantic, Engine::LightSemantic::NeutralShrine);
}

TEST(SceneSerializerTest, TerrainChunkManagerAndModifiedChunksRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("ChunkedTerrainSaveScene");
    auto* worldGo = srcScene->CreateGameObjectWithUUID(90909ULL, "InfiniteGround");
    auto* mgr = worldGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 2, 4);
    mgr->MaxGpuUploadsPerFrame = 3;
    mgr->AutoStreamWithCamera = false;
    mgr->SetBlendSharpness(0.72f);
    mgr->SetEdgeDarkening(0.24f);
    mgr->SetLayerTilingScale(2u, 0.77f);

    Engine::WorldGenConfig wg;
    wg.Enabled = true;
    wg.SpawnSceneEntities = false;
    wg.Seed = 424242ULL;
    wg.OriginSafeRadius = 7.25f;
    wg.PoiApronRadius = 4.0f;
    wg.PoiSuperCellChunks = 3;
    mgr->ConfigureProceduralWorld(wg);

    mgr->UpdateStreaming({ 16.0f, 0.0f, 16.0f });
    mgr->FlushPendingGenerations();
    mgr->PaintWorldBrush(16.0f, 16.0f, /*layerIndex=*/2u, /*radiusWorld=*/2.5f, /*falloff=*/0.6f, /*strength=*/1.0f);

    const auto* src00 = mgr->GetChunk({ 0, 0 });
    ASSERT_NE(src00, nullptr);
    const glm::u8vec4 expectedCorner = src00->GetSplatTexel(64u, 64u);
    EXPECT_GT(expectedCorner.b, 200u);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(90909ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedMgr = loadedGo->GetComponent<Engine::TerrainChunkManager>();
    ASSERT_NE(loadedMgr, nullptr);
    EXPECT_FLOAT_EQ(loadedMgr->ChunkSizeMeters, 16.0f);
    EXPECT_EQ(loadedMgr->SplatResolution, 65u);
    EXPECT_EQ(loadedMgr->ActiveRadiusChunks, 2);
    EXPECT_EQ(loadedMgr->CacheRetentionRadius, 4);
    EXPECT_EQ(loadedMgr->MaxGpuUploadsPerFrame, 3u);
    EXPECT_FALSE(loadedMgr->AutoStreamWithCamera);
    EXPECT_NEAR(loadedMgr->GetBlendSharpness(), 0.72f, 1e-5f);
    EXPECT_NEAR(loadedMgr->GetEdgeDarkening(), 0.24f, 1e-5f);
    EXPECT_NEAR(loadedMgr->GetLayer(2u).TilingScale, 0.77f, 1e-5f);

    const auto& loadedWg = loadedMgr->GetWorldGenConfig();
    EXPECT_TRUE(loadedWg.Enabled);
    EXPECT_FALSE(loadedWg.SpawnSceneEntities);
    EXPECT_EQ(loadedWg.Seed, 424242ULL);
    EXPECT_NEAR(loadedWg.OriginSafeRadius, 7.25f, 1e-5f);
    EXPECT_NEAR(loadedWg.PoiApronRadius, 4.0f, 1e-5f);
    EXPECT_EQ(loadedWg.PoiSuperCellChunks, 3);

    const auto* loaded00 = loadedMgr->GetChunk({ 0, 0 });
    const auto* loaded11 = loadedMgr->GetChunk({ 1, 1 });
    ASSERT_NE(loaded00, nullptr);
    ASSERT_NE(loaded11, nullptr);
    EXPECT_TRUE(loaded00->UserModified);
    EXPECT_EQ(loaded00->GetSplatTexel(64u, 64u), expectedCorner);
    EXPECT_EQ(loaded11->GetSplatTexel(0u, 0u), expectedCorner);
}

TEST(SceneSerializerTest, EightLayerTerrainAndSecondaryWeightsRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("EightLayerScene");
    auto* go = srcScene->CreateGameObjectWithUUID(77777ULL, "Terr8");
    auto* terrain = go->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 16u, 16u);

    terrain->SetLayerTilingScale(2u, 0.62f);
    terrain->SetLayerGlossiness(2u, 0.85f);
    terrain->SetLayerHeightMapPath(4u, "assets/diorama/terrain/cobble/tex_cobble_h.png");
    terrain->SetLayerNormalMapPath(4u, "assets/diorama/terrain/cobble/tex_cobble_n.png");
    terrain->SetLayerTilingScale(4u, 1.25f);
    terrain->SetLayerGlossiness(4u, 0.35f);
    terrain->SetLayerGlossiness(5u, 0.06f);

    terrain->PaintWorldBrush(8.0f, 8.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 4u, 3.0f, 0.6f, 1.0f);
    terrain->PaintWorldBrush(2.0f, 2.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 5u, 2.0f, 0.6f, 0.9f);
    const auto expectedCobbleCenter = terrain->GetSplatTexel8(8u, 8u);
    EXPECT_GT(expectedCobbleCenter[4], 200u);

    const std::string savedJson = Engine::SceneSerializer(srcScene).SerializeToString();
    ASSERT_FALSE(savedJson.empty());

    auto loadedScene = std::make_shared<Engine::Scene>();
    ASSERT_TRUE(Engine::SceneSerializer(loadedScene).DeserializeFromString(savedJson));
    auto* loadedGo = loadedScene->FindGameObjectByUUID(77777ULL);
    ASSERT_NE(loadedGo, nullptr);
    auto* loadedTerrain = loadedGo->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(loadedTerrain, nullptr);

    EXPECT_NEAR(loadedTerrain->GetLayer(2u).Glossiness, 0.85f, 1e-4f);
    EXPECT_NEAR(loadedTerrain->GetLayer(4u).TilingScale, 1.25f, 1e-4f);
    EXPECT_NEAR(loadedTerrain->GetLayer(4u).Glossiness, 0.35f, 1e-4f);
    EXPECT_EQ(loadedTerrain->GetLayer(4u).HeightMapPath, "assets/diorama/terrain/cobble/tex_cobble_h.png");
    EXPECT_EQ(loadedTerrain->GetLayer(4u).NormalMapPath, "assets/diorama/terrain/cobble/tex_cobble_n.png");
    EXPECT_NEAR(loadedTerrain->GetLayer(5u).Glossiness, 0.06f, 1e-4f);
    EXPECT_EQ(loadedTerrain->GetSplatTexel8(8u, 8u), expectedCobbleCenter);
}

} // namespace

