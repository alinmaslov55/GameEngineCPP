#ifndef SANDBOX_SCENE_PREFABS_H
#define SANDBOX_SCENE_PREFABS_H

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/Components/CameraComponent.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/Components/PostProcessComponent.h>
#include <Engine/Scene/Components/SquashStretchComponent.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

namespace Sandbox {

struct AtmosphereConfig {
    const char* LightName = "MoonLight";
    glm::vec3 LightPosition = { 0.0f, 12.0f, 0.0f };
    float ElevationDegrees = 36.0f;
    float AzimuthDegrees = -35.0f;
    glm::vec3 SunColor = { 0.42f, 0.50f, 0.78f };
    float SunIntensity = 0.25f;
    glm::vec3 AmbientColor = { 0.14f, 0.18f, 0.30f };
    float AmbientIntensity = 0.22f;
    bool CastShadows = true;
    float ShadowSoftness = 1.45f;
    bool AddPostProcess = true;
    bool AddWindZone = true;
};

struct PlayerPrefabConfig {
    glm::vec3 Position = { 0.0f, 0.0f, 2.5f };
    glm::vec3 Scale = { 1.25f, 1.35f, 1.0f };
    float TorchIntensity = 2.25f;
    bool UseTerrainFriction = true;
    bool AttachCamera = true;
    bool AttachSockets = true;
};

/// @brief Shared entity prefab builders for AlphaRun, SanctuaryHub, and DemoScene.
class ScenePrefabs {
public:
    static void EnsureChildSocket(Engine::Scene& scene, Engine::GameObject& parent, const char* socketName) {
        for (Engine::Transform* child : parent.GetTransform()->GetChildren()) {
            if (child && child->GetGameObject() && child->GetGameObject()->GetName() == socketName) {
                return;
            }
        }
        Engine::GameObject* socketGo = scene.CreateGameObject(socketName);
        socketGo->GetTransform()->SetParent(parent.GetTransform(), false);
    }

    static void ConfigurePlayerVisualsAndCamera(Engine::Scene& scene,
                                                Engine::GameObject& player,
                                                float torchIntensity = 2.25f,
                                                bool attachCamera = true,
                                                bool attachSockets = true) {
        if (!player.GetComponent<Engine::SpriteRenderer>()) {
            auto heroTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/hero_lamb.png");
            auto* playerSprite = player.AddComponent<Engine::SpriteRenderer>();
            playerSprite->SetTexture(heroTex);
            playerSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
            playerSprite->SetAlphaCutoff(0.18f);
        }

        if (!player.GetComponent<Engine::PointLightComponent>()) {
            auto* heroTorch = player.AddComponent<Engine::PointLightComponent>();
            heroTorch->Color = { 1.0f, 0.58f, 0.22f };
            heroTorch->Intensity = torchIntensity;
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
        }

        if (!player.GetComponent<Engine::SquashStretchComponent>()) {
            player.AddComponent<Engine::SquashStretchComponent>();
        }

        if (attachCamera && !player.GetComponent<Engine::CameraComponent>()) {
            auto* camComp = player.AddComponent<Engine::CameraComponent>();
            camComp->FOV = 32.0f;
            camComp->Distance = 15.0f;
            camComp->Pitch = 40.0f;
            camComp->SmoothFollow = true;
            camComp->ResetToTarget(player.GetTransform()->position);
        }

        if (attachSockets) {
            EnsureChildSocket(scene, player, "TorchSocket");
            EnsureChildSocket(scene, player, "WeaponSocket");
        }
    }

    static Engine::GameObject* CreateAtmosphere(Engine::Scene& scene, const AtmosphereConfig& cfg = {}) {
        Engine::GameObject* lightObj = scene.CreateGameObject(cfg.LightName);
        lightObj->GetTransform()->position = cfg.LightPosition;
        auto* dirLight = lightObj->AddComponent<Engine::DirectionalLightComponent>();
        dirLight->ElevationDegrees = cfg.ElevationDegrees;
        dirLight->AzimuthDegrees = cfg.AzimuthDegrees;
        dirLight->SunColor = cfg.SunColor;
        dirLight->SunIntensity = cfg.SunIntensity;
        dirLight->AmbientColor = cfg.AmbientColor;
        dirLight->AmbientIntensity = cfg.AmbientIntensity;
        dirLight->CastShadows = cfg.CastShadows;
        dirLight->ShadowSoftness = cfg.ShadowSoftness;

        if (cfg.AddPostProcess) {
            Engine::GameObject* postProcessObject = scene.CreateGameObject("Post Process Volume");
            auto* postProcess = postProcessObject->AddComponent<Engine::PostProcessComponent>();
            postProcess->ApplyPreset(Engine::PostProcessComponent::Preset::CultStorybook);
        }

        if (cfg.AddWindZone) {
            Engine::GameObject* windObject = scene.CreateGameObject("Wind Zone");
            auto* windZone = windObject->AddComponent<Engine::WindComponent>();
            windZone->ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);
        }

        return lightObj;
    }

    static Engine::GameObject* CreateDefaultPlayer(Engine::Scene& scene, const PlayerPrefabConfig& cfg = {}) {
        Engine::GameObject* player = scene.CreateGameObject("Player");
        player->SetTag(Engine::EntityTag::Player);
        player->GetTransform()->position = cfg.Position;
        player->GetTransform()->scale = cfg.Scale;

        player->AddComponent<Engine::ParticleSystem>(500);

        auto* playerRb = player->AddComponent<Engine::Rigidbody2D>();
        playerRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
        playerRb->UseGravityY = true;
        playerRb->GroundLevelY = 0.0f;
        playerRb->UseTerrainFriction = cfg.UseTerrainFriction;

        auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
        playerCollider->size = { 0.55f, 0.45f };
        playerCollider->Friction = 0.0f;

        auto* playerAudio = player->AddComponent<Engine::AudioSource>();
        playerAudio->SetClip(Engine::ResourceManager::Load<Engine::AudioClip>("jump.wav"));

        ConfigurePlayerVisualsAndCamera(scene, *player, cfg.TorchIntensity, cfg.AttachCamera, cfg.AttachSockets);
        return player;
    }
};

} // namespace Sandbox

#endif // SANDBOX_SCENE_PREFABS_H
