#include "Scene/AlphaRunScene.h"
#include "Scene/ScenePrefabs.h"
#include "Scripts/RunTransitionService.h"

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

void EnsureTransitionFactoriesRegistered() {
    if (!RunTransitionService::HasFactories()) {
        RunTransitionService::RegisterFactories(
            [](uint64_t seed) {
                auto runScene = AlphaRunScene::Create(seed);
                AlphaRunScene::BindRuntimeGameplayScripts(runScene);
                return runScene;
            },
            []() {
                return AlphaRunScene::CreateSanctuaryHubScene();
            });
    }
}

struct AlphaTransitionRegistrar {
    AlphaTransitionRegistrar() { EnsureTransitionFactoriesRegistered(); }
};
static AlphaTransitionRegistrar s_AlphaTransitionRegistrar;

} // namespace

std::shared_ptr<Engine::Scene> AlphaRunScene::Create(uint64_t seed) {
    EnsureTransitionFactoriesRegistered();
    auto scene = std::make_shared<Engine::Scene>("AlphaRun");

    auto& cam = scene->GetCamera();
    cam.SetFOVDegrees(32.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);
    cam.SetDistance(15.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 1.5f });

    // 1. Nighttime Cool-Indigo Moonlight + HDR Post-Processing & Night Breeze
    AtmosphereConfig nightAtmos;
    nightAtmos.LightName = "MoonLight";
    nightAtmos.LightPosition = { 0.0f, 12.0f, 0.0f };
    nightAtmos.ElevationDegrees = 36.0f;
    nightAtmos.AzimuthDegrees = -35.0f;
    nightAtmos.SunColor = { 0.42f, 0.50f, 0.78f };
    nightAtmos.SunIntensity = kNightMoonIntensity;
    nightAtmos.AmbientColor = { 0.14f, 0.18f, 0.30f };
    nightAtmos.AmbientIntensity = 0.22f;
    nightAtmos.CastShadows = true;
    nightAtmos.ShadowSoftness = 1.45f;
    ScenePrefabs::CreateAtmosphere(*scene, nightAtmos);

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
    PlayerPrefabConfig playerCfg;
    playerCfg.Position = { 0.0f, 0.0f, 2.5f };
    playerCfg.UseTerrainFriction = true;
    ScenePrefabs::CreateDefaultPlayer(*scene, playerCfg);

    return scene;
}

void AlphaRunScene::BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene) {
    EnsureTransitionFactoriesRegistered();
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
            auto* stats = goPtr->GetComponent<PlayerStatsComponent>();
            if (!stats) {
                stats = goPtr->AddComponent<PlayerStatsComponent>();
            }
            auto* health = goPtr->GetComponent<HealthComponent>();
            if (!health) {
                health = goPtr->AddComponent<HealthComponent>();
                health->DestroyOnDeath = false;
                health->InvulnOnHitDuration = 0.25f;
                health->ResetHealth(stats->MaxHP);
            }
            Engine::GameObject* playerPtr = goPtr.get();
            health->OnDamaged = [playerPtr](const DamageEvent&, float) {
                if (auto* pStats = playerPtr->GetComponent<PlayerStatsComponent>()) {
                    if (auto* pHealth = playerPtr->GetComponent<HealthComponent>()) {
                        pStats->HP = pHealth->CurrentHP;
                        pStats->MaxHP = pHealth->MaxHP;
                    }
                }
            };
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

            ScenePrefabs::EnsureChildSocket(*runtimeScene, *goPtr, "TorchSocket");
            ScenePrefabs::EnsureChildSocket(*runtimeScene, *goPtr, "WeaponSocket");
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
    EnsureTransitionFactoriesRegistered();
    auto hub = std::make_shared<Engine::Scene>("SanctuaryHub");
    if (!Engine::GameplayData::IsInitialized()) {
        Engine::GameplayData::Init("Sandbox/assets/data");
    }
    RunSessionManager::Get().LoadOrCreateProfile(0);

    auto& cam = hub->GetCamera();
    cam.SetFOVDegrees(32.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);
    cam.SetDistance(15.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 1.5f });

    AtmosphereConfig hubAtmos;
    hubAtmos.LightName = "SanctuarySun";
    hubAtmos.LightPosition = { 0.0f, 10.0f, 0.0f };
    hubAtmos.SunColor = { 1.0f, 0.90f, 0.72f };
    hubAtmos.SunIntensity = 0.85f;
    hubAtmos.AmbientColor = { 0.34f, 0.38f, 0.56f };
    hubAtmos.AmbientIntensity = 0.50f;
    ScenePrefabs::CreateAtmosphere(*hub, hubAtmos);

    PlayerPrefabConfig hubPlayerCfg;
    hubPlayerCfg.Position = { 0.0f, 0.0f, 2.5f };
    hubPlayerCfg.UseTerrainFriction = false;
    Engine::GameObject* player = ScenePrefabs::CreateDefaultPlayer(*hub, hubPlayerCfg);
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

void AlphaRunScene::BindSanctuaryHubScripts(const std::shared_ptr<Engine::Scene>& runtimeScene) {
    EnsureTransitionFactoriesRegistered();
    if (!runtimeScene) {
        return;
    }
    if (!Engine::GameplayData::IsInitialized()) {
        Engine::GameplayData::Init("Sandbox/assets/data");
    }
    for (const auto& goPtr : runtimeScene->GetGameObjects()) {
        if (!goPtr || goPtr->GetName() != "Player") {
            continue;
        }
        if (!goPtr->GetComponent<PlayerController>()) goPtr->AddComponent<PlayerController>();
        if (!goPtr->GetComponent<PlayerStatsComponent>()) goPtr->AddComponent<PlayerStatsComponent>();
        if (!goPtr->GetComponent<WeaponController>()) goPtr->AddComponent<WeaponController>();
        if (!goPtr->GetComponent<RuneCasterComponent>()) goPtr->AddComponent<RuneCasterComponent>();
        ScenePrefabs::ConfigurePlayerVisualsAndCamera(*runtimeScene, *goPtr);
        break;
    }
    if (Engine::AudioManager::IsInitialized()) {
        Engine::MusicDirector::PlaySingleTrack("assets/audio/mus_sanctuary_hub.ogg", 1.2f);
    }
}

bool AlphaRunScene::IsSanctuaryHubScene(const Engine::Scene& scene) {
    if (scene.GetName() == "SanctuaryHub") {
        return true;
    }
    for (const auto& go : scene.GetGameObjects()) {
        if (go && go->GetComponent<SanctuaryStationComponent>()) {
            return true;
        }
    }
    return false;
}

bool AlphaRunScene::IsAlphaScene(const Engine::Scene& scene) {
    if (IsSanctuaryHubScene(scene)) {
        return false;
    }
    const std::string& name = scene.GetName();
    if (name == "AlphaRun" || name == "AlphaTest") {
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
