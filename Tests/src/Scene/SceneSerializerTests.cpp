#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Core/ResourceManager.h"
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

    auto* box = hero->AddComponent<Engine::BoxCollider2D>();
    box->size = {0.8f, 0.9f};
    box->offset = {0.1f, -0.2f};
    box->density = 2.5f;
    box->friction = 0.4f;
    box->restitution = 0.75f;
    box->isDynamic = true;
    box->useGravity = true;
    box->gravity = -14.5f;
    box->groundLevel = 0.25f;

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

    auto* clonedBox = clonedHero->GetComponent<Engine::BoxCollider2D>();
    ASSERT_NE(clonedBox, nullptr);
    EXPECT_NEAR(clonedBox->density, 2.5f, 1e-5f);
    EXPECT_NEAR(clonedBox->friction, 0.4f, 1e-5f);
    EXPECT_NEAR(clonedBox->restitution, 0.75f, 1e-5f);
    EXPECT_TRUE(clonedBox->isDynamic);
    EXPECT_TRUE(clonedBox->useGravity);
    EXPECT_NEAR(clonedBox->gravity, -14.5f, 1e-5f);
    EXPECT_NEAR(clonedBox->groundLevel, 0.25f, 1e-5f);
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

} // namespace

