#include "Scene/AlphaRunScene.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/MusicDirector.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Data/GameplayData.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Components/PostProcessComponent.h"
#include "Engine/Scene/Components/SquashStretchComponent.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Scripts/AlphaHUDController.h"
#include "Scripts/ArenaStreamingDriver.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/RuneCasterComponent.h"
#include "Scripts/RunProgressionComponent.h"
#include "Scripts/RunSessionManager.h"
#include "Scripts/RunSummaryController.h"
#include "Scripts/SanctuaryHubComponents.h"
#include "Scripts/StructurePlacerComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WaveDirectorComponent.h"
#include "Scripts/WeaponController.h"

namespace Sandbox {

namespace {

void ConfigureArenaTerrainLayers(Engine::TerrainChunkManager& chunkMgr) {
    auto grassTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/grass_seamless.png");
    auto dirtTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/dirt_seamless.png");
    auto tarTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/tar_seamless.png");
    auto chalkTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/chalk_seamless.png");
    auto cobbleTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/cobble_seamless.png");

    // Layer 0: Blackwood Thickets (mossy forest floor)
    chunkMgr.SetLayerTexture(0, grassTex);
    chunkMgr.SetLayerTilingScale(0, 0.42f);
    chunkMgr.SetLayerGlossiness(0, 0.12f);

    // Layer 1: Ash Barrens (dry soot)
    chunkMgr.SetLayerTexture(1, dirtTex);
    chunkMgr.SetLayerTilingScale(1, 0.45f);
    chunkMgr.SetLayerGlossiness(1, 0.08f);

    // Layer 2: Tar Bogs (glossy black flammable pools)
    chunkMgr.SetLayerTexture(2, tarTex);
    chunkMgr.SetLayerTilingScale(2, 0.50f);
    chunkMgr.SetLayerGlossiness(2, 0.85f);

    // Layer 3: Chalk Highlands (slippery pale limestone)
    chunkMgr.SetLayerTexture(3, chalkTex);
    chunkMgr.SetLayerTilingScale(3, 0.48f);
    chunkMgr.SetLayerGlossiness(3, 0.25f);

    // Layer 4: Sanctuary Cobble (origin safe clearing & POI plazas)
    chunkMgr.SetLayerTexture(4, cobbleTex);
    chunkMgr.SetLayerTilingScale(4, 0.48f);
    chunkMgr.SetLayerGlossiness(4, 0.35f);

    chunkMgr.SetBlendSharpness(0.68f);
    chunkMgr.SetEdgeDarkening(0.18f);
}

void EnsureChildSocket(Engine::Scene& scene, Engine::GameObject& parent, const char* socketName) {
    for (Engine::Transform* child : parent.GetTransform()->GetChildren()) {
        if (child && child->GetGameObject() && child->GetGameObject()->GetName() == socketName) {
            return;
        }
    }
    Engine::GameObject* socketGo = scene.CreateGameObject(socketName);
    socketGo->GetTransform()->SetParent(parent.GetTransform(), false);
}

} // namespace

std::shared_ptr<Engine::Scene> AlphaRunScene::Create(uint64_t seed) {
    auto scene = std::make_shared<Engine::Scene>("AlphaRun");

    auto& cam = scene->GetCamera();
    cam.SetFOVDegrees(32.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);
    cam.SetDistance(15.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 1.5f });

    // 1. Nighttime Cool-Indigo Moonlight (SunIntensity = 0.25 so Torch & Braziers dominate)
    Engine::GameObject* moonObject = scene->CreateGameObject("MoonLight");
    moonObject->GetTransform()->position = { 0.0f, 12.0f, 0.0f };
    auto* moonLight = moonObject->AddComponent<Engine::DirectionalLightComponent>();
    moonLight->ElevationDegrees = 36.0f;
    moonLight->AzimuthDegrees = -35.0f;
    moonLight->SunColor = { 0.42f, 0.50f, 0.78f };
    moonLight->SunIntensity = kNightMoonIntensity;
    moonLight->AmbientColor = { 0.14f, 0.18f, 0.30f };
    moonLight->AmbientIntensity = 0.22f;
    moonLight->CastShadows = true;
    moonLight->ShadowSoftness = 1.45f;

    // 2. HDR Post-Processing & Night Breeze
    Engine::GameObject* postProcessObject = scene->CreateGameObject("Post Process Volume");
    auto* postProcess = postProcessObject->AddComponent<Engine::PostProcessComponent>();
    postProcess->ApplyPreset(Engine::PostProcessComponent::Preset::CultStorybook);

    Engine::GameObject* windObject = scene->CreateGameObject("Wind Zone");
    auto* windZone = windObject->AddComponent<Engine::WindComponent>();
    windZone->ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);

    // 3. Infinite Procedural Biome Terrain (TER-01/02/03)
    Engine::GameObject* terrainObject = scene->CreateGameObject("Terrain");
    auto* chunkMgr = terrainObject->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 3, 6);
    chunkMgr->AutoStreamWithCamera = true;
    ConfigureArenaTerrainLayers(*chunkMgr);

    Engine::WorldGenConfig worldConfig;
    worldConfig.Enabled = true;
    worldConfig.SpawnSceneEntities = true;
    worldConfig.Seed = seed;
    worldConfig.OriginSafeRadius = kOriginSafeRadiusMeters;
    worldConfig.PoiApronRadius = 3.5f;
    worldConfig.PoiSuperCellChunks = 3;
    chunkMgr->ConfigureProceduralWorld(worldConfig);

    // Pre-populate the initial 7x7 chunk window around the origin clearing
    chunkMgr->UpdateStreaming({ 0.0f, 0.0f, 0.0f });
    chunkMgr->FlushPendingGenerations();
    chunkMgr->DrainReadyPayloads();

    // 4. Central Sanctuary Brazier at (0, 0, 0) lighting the 8.0m Cobblestone clearing
    auto brazierTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/ritual_brazier.png");
    Engine::GameObject* brazier = scene->CreateGameObject("Sanctuary Brazier");
    brazier->SetTag(Engine::EntityTag::SafeZoneBrazier);
    brazier->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    brazier->GetTransform()->scale = { 1.35f, 1.55f, 1.0f };
    auto* brazierSprite = brazier->AddComponent<Engine::SpriteRenderer>();
    brazierSprite->SetTexture(brazierTex);
    brazierSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
    brazierSprite->SetAlphaCutoff(0.18f);
    brazierSprite->SetEmissive(0.45f);

    auto* brazierCollider = brazier->AddComponent<Engine::BoxCollider2D>();
    brazierCollider->size = { 0.85f, 0.55f };

    auto* brazierLight = brazier->AddComponent<Engine::PointLightComponent>();
    brazierLight->Color = { 1.0f, 0.56f, 0.20f };
    brazierLight->Intensity = 2.4f;
    brazierLight->Radius = 8.5f;
    brazierLight->Falloff = 1.6f;
    brazierLight->HeightOffset = 0.85f;
    brazierLight->Priority = Engine::LightPriority::High_Tactical;
    brazierLight->Semantic = Engine::LightSemantic::SafeLifeline;
    brazierLight->CastShadows = true;
    brazierLight->ShadowSoftness = 1.6f;
    brazierLight->ShadowBias = 0.0025f;
    brazierLight->Flicker = true;
    brazierLight->FlickerSpeed = 7.5f;
    brazierLight->FlickerAmount = 0.16f;

    // 5. Player ("Hero Lamb") inside the 8.0m safe clearing with 360° shadow-casting Torch
    auto heroTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/hero_lamb.png");
    Engine::GameObject* player = scene->CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 2.5f };
    player->GetTransform()->scale = { 1.25f, 1.35f, 1.0f };

    auto* playerSprite = player->AddComponent<Engine::SpriteRenderer>();
    playerSprite->SetTexture(heroTex);
    playerSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
    playerSprite->SetAlphaCutoff(0.18f);

    auto* heroTorch = player->AddComponent<Engine::PointLightComponent>();
    heroTorch->Color = { 1.0f, 0.58f, 0.22f };
    heroTorch->Intensity = 2.25f;
    heroTorch->Radius = 7.5f;
    heroTorch->Falloff = 1.7f;
    heroTorch->HeightOffset = 0.90f;
    heroTorch->Priority = Engine::LightPriority::Critical_HeroTorch;
    heroTorch->Semantic = Engine::LightSemantic::SafeLifeline;
    heroTorch->CastShadows = true;
    heroTorch->ShadowSoftness = 1.6f;
    heroTorch->ShadowBias = 0.0025f;
    heroTorch->Flicker = true;
    heroTorch->FlickerSpeed = 8.5f;
    heroTorch->FlickerAmount = 0.14f;

    player->AddComponent<Engine::ParticleSystem>(500);

    auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
    playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    playerRb->UseGravityY = true;
    playerRb->GroundLevelY = 0.0f;
    playerRb->UseTerrainFriction = true;

    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->size = { 0.55f, 0.45f };
    playerCollider->Friction = 0.0f;

    auto* playerAudio = player->AddComponent<Engine::AudioSource>();
    playerAudio->SetClip(Engine::ResourceManager::Load<Engine::AudioClip>("jump.wav"));

    player->AddComponent<Engine::SquashStretchComponent>();

    auto* camComp = player->AddComponent<Engine::CameraComponent>();
    camComp->FOV = 32.0f;
    camComp->Distance = 15.0f;
    camComp->Pitch = 40.0f;
    camComp->SmoothFollow = true;
    camComp->ResetToTarget(player->GetTransform()->position);

    EnsureChildSocket(*scene, *player, "TorchSocket");
    EnsureChildSocket(*scene, *player, "WeaponSocket");

    return scene;
}

void AlphaRunScene::BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene) {
    if (!runtimeScene) {
        return;
    }

    if (!Engine::GameplayData::IsInitialized()) {
        Engine::GameplayData::Init("Sandbox/assets/data");
    }

    bool hasWaveDirector = false;
    bool hasAlphaHUD = false;
    for (const auto& goPtr : runtimeScene->GetGameObjects()) {
        if (!goPtr) {
            continue;
        }
        if (goPtr->GetComponent<WaveDirectorComponent>()) {
            hasWaveDirector = true;
        }
        if (goPtr->GetComponent<AlphaHUDController>()) {
            hasAlphaHUD = true;
        }
        if (goPtr->GetName() == "Terrain") {
            if (auto* chunkMgr = goPtr->GetComponent<Engine::TerrainChunkManager>()) {
                chunkMgr->AutoStreamWithCamera = false;
            }
        }
        if (goPtr->GetName() == "Player") {
            if (auto* rb = goPtr->GetComponent<Engine::Rigidbody2D>()) {
                rb->UseTerrainFriction = true;
            }
            if (!goPtr->GetComponent<PlayerController>()) {
                goPtr->AddComponent<PlayerController>();
            }
            if (!goPtr->GetComponent<PlayerStatsComponent>()) {
                goPtr->AddComponent<PlayerStatsComponent>();
            }
            if (!goPtr->GetComponent<WeaponController>()) {
                goPtr->AddComponent<WeaponController>();
            }
            if (!goPtr->GetComponent<RuneCasterComponent>()) {
                goPtr->AddComponent<RuneCasterComponent>();
            }
            if (!goPtr->GetComponent<StructurePlacerComponent>()) {
                goPtr->AddComponent<StructurePlacerComponent>();
            }
            if (!goPtr->GetComponent<RunProgressionComponent>()) {
                goPtr->AddComponent<RunProgressionComponent>();
            }
            if (!goPtr->GetComponent<Engine::CameraComponent>()) {
                auto* camComp = goPtr->AddComponent<Engine::CameraComponent>();
                camComp->FOV = 32.0f;
                camComp->Distance = 15.0f;
                camComp->Pitch = 40.0f;
                camComp->SmoothFollow = true;
                camComp->ResetToTarget(goPtr->GetTransform()->GetWorldPosition());
            }
            if (!goPtr->GetComponent<ArenaStreamingDriver>()) {
                goPtr->AddComponent<ArenaStreamingDriver>();
            }

            EnsureChildSocket(*runtimeScene, *goPtr, "TorchSocket");
            EnsureChildSocket(*runtimeScene, *goPtr, "WeaponSocket");
            for (Engine::Transform* child : goPtr->GetTransform()->GetChildren()) {
                if (child && child->GetGameObject() && child->GetGameObject()->GetName() == "TorchSocket") {
                    if (!child->GetGameObject()->GetComponent<TorchComponent>()) {
                        child->GetGameObject()->AddComponent<TorchComponent>();
                    }
                    break;
                }
            }

            RunSessionManager::Get().ApplyRootUpgradesToPlayer(*goPtr);
        }
    }

    if (!hasWaveDirector) {
        auto* directorGo = runtimeScene->CreateGameObject("WaveDirector");
        directorGo->AddComponent<WaveDirectorComponent>();
    }
    if (!hasAlphaHUD) {
        auto* hudGo = runtimeScene->CreateGameObject("AlphaHUD");
        hudGo->AddComponent<AlphaHUDController>();
    }

    BindHarvestableProps(*runtimeScene);
    if (Engine::AudioManager::IsInitialized()) {
        Engine::MusicDirector::PlaySynchronizedStems({
            "assets/audio/mus_run_dread.ogg",
            "assets/audio/mus_run_combat.ogg",
            "assets/audio/mus_run_frenzy.ogg"
        }, 1.2f);
    }
}

std::shared_ptr<Engine::Scene> AlphaRunScene::CreateSanctuaryHubScene() {
    auto hub = std::make_shared<Engine::Scene>("SanctuaryHub");
    if (!Engine::GameplayData::IsInitialized()) {
        Engine::GameplayData::Init("Sandbox/assets/data");
    }
    RunSessionManager::Get().LoadOrCreateProfile(0);

    auto* sunGo = hub->CreateGameObject("SanctuarySun");
    auto* sun = sunGo->AddComponent<Engine::DirectionalLightComponent>();
    sun->SunIntensity = 0.85f;
    sun->SunColor = { 1.0f, 0.90f, 0.72f };

    auto* player = hub->CreateGameObject("Player");
    player->SetTag(Engine::EntityTag::Player);
    player->GetTransform()->position = { 0.0f, 0.0f, 2.5f };
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    player->AddComponent<Engine::BoxCollider2D>()->size = { 0.55f, 0.45f };
    player->AddComponent<PlayerController>();
    player->AddComponent<PlayerStatsComponent>();
    player->AddComponent<WeaponController>();
    player->AddComponent<RuneCasterComponent>();

    auto addStation = [&](const char* name, SanctuaryStationKind kind, glm::vec3 pos) {
        auto* stGo = hub->CreateGameObject(name);
        stGo->GetTransform()->position = pos;
        auto* st = stGo->AddComponent<SanctuaryStationComponent>();
        st->Kind = kind;
        return stGo;
    };

    addStation("Weapon Rack", SanctuaryStationKind::WeaponRack, { -3.5f, 0.0f, 0.5f });
    addStation("Rune Pedestal", SanctuaryStationKind::RunePedestal, { 3.5f, 0.0f, 0.5f });
    addStation("Root Altar", SanctuaryStationKind::RootAltar, { 0.0f, 0.0f, -1.5f });
    addStation("Northern Umbral Gate", SanctuaryStationKind::UmbralGate, { 0.0f, 0.0f, -5.5f });

    auto* titleGo = hub->CreateGameObject("TitleOverlay");
    titleGo->AddComponent<TitleOverlayComponent>()->Kind = SanctuaryStationKind::TitleOverlay;

    if (Engine::AudioManager::IsInitialized()) {
        Engine::MusicDirector::PlaySingleTrack("assets/audio/mus_sanctuary_hub.ogg", 1.2f);
    }

    return hub;
}

bool AlphaRunScene::IsAlphaScene(const Engine::Scene& scene) {
    const std::string& name = scene.GetName();
    if (name == "AlphaRun" || name == "AlphaTest" || name == "SanctuaryHub") {
        return true;
    }
    for (const auto& go : scene.GetGameObjects()) {
        if (go && (go->GetName() == "Sanctuary Brazier" || go->GetName() == "WaveDirector")) {
            return true;
        }
    }
    return false;
}

bool AlphaRunScene::SaveAlphaTestScene(const std::string& filepath, uint64_t seed) {
    auto scene = Create(seed);
    if (!scene) {
        return false;
    }
    scene->SetName("AlphaTest");
    Engine::SceneSerializer serializer(scene);
    return serializer.Serialize(filepath);
}

} // namespace Sandbox
