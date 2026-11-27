#ifndef SANDBOX_PICKUP_COMPONENT_H
#define SANDBOX_PICKUP_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class PickupKind : uint8_t {
    WoodLog = 0,
    AshEmber = 1,
    CrimsonBerry = 2
};

class PickupComponent final : public Engine::Component {
public:
    PickupKind Kind = PickupKind::WoodLog;
    int Amount = 1;
    int SecondaryAmount = 5; // Used by AshEmber for RunAshes alongside XP
    float HealAmount = 20.0f;
    float MagnetDelay = 0.25f;
    float MagnetAcceleration = 32.0f;
    float MaxMagnetSpeed = 14.0f;

    static Engine::GameObject* SpawnPickup(Engine::Scene& scene,
                                           PickupKind kind,
                                           const glm::vec3& originPos,
                                           const glm::vec2& scatterVelXZ,
                                           float launchVerticalVel) {
        const char* name = "WoodLogPickup";
        glm::vec4 tint{ 0.58f, 0.38f, 0.20f, 1.0f };
        glm::vec3 scale{ 0.42f, 0.42f, 1.0f };
        if (kind == PickupKind::AshEmber) {
            name = "AshEmberPickup";
            tint = { 1.0f, 0.62f, 0.22f, 1.0f };
            scale = { 0.34f, 0.34f, 1.0f };
        } else if (kind == PickupKind::CrimsonBerry) {
            name = "CrimsonBerryPickup";
            tint = { 0.92f, 0.16f, 0.24f, 1.0f };
            scale = { 0.38f, 0.38f, 1.0f };
        }

        auto* go = scene.CreateGameObject(name);
        go->SetTag(Engine::EntityTag::ResourcePickup);
        go->SetLayer(Engine::EntityLayer::Pickup);
        go->GetTransform()->position = { originPos.x, std::max(0.15f, originPos.y), originPos.z };
        go->GetTransform()->scale = scale;

        auto* sr = go->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
        sr->SetColor(tint);
        const char* texPath = "assets/diorama/wood_log.png";
        if (kind == PickupKind::AshEmber) {
            texPath = "assets/diorama/ash_ember.png";
            sr->SetEmissive(0.85f);
        } else if (kind == PickupKind::CrimsonBerry) {
            texPath = "assets/diorama/crimson_heart.png";
        }
        if (auto tex = Engine::ResourceManager::Load<Engine::Texture>(texPath)) {
            sr->SetTexture(tex);
            sr->SetAlphaCutoff(0.18f);
        }

        auto* rb = go->AddComponent<Engine::Rigidbody2D>();
        rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
        rb->UseGravityY = true;
        rb->GroundLevelY = 0.0f;
        rb->GroundRestitutionY = 0.45f;
        rb->LinearDamping = 3.5f;
        rb->LaunchVertical(std::clamp(launchVerticalVel, 4.5f, 6.5f));
        rb->SetVelocityXZ(scatterVelXZ);

        auto* col = go->AddComponent<Engine::CircleCollider2D>();
        col->Radius = 0.40f;
        col->IsSensor = true;
        col->CategoryBits = Engine::PhysicsLayer::Pickup;
        col->MaskBits = Engine::PhysicsLayer::Player;

        auto* pickup = go->AddComponent<PickupComponent>();
        pickup->Kind = kind;
        if (kind == PickupKind::AshEmber) {
            pickup->Amount = 10;          // +XP
            pickup->SecondaryAmount = 5;  // +RunAshes
        } else if (kind == PickupKind::CrimsonBerry) {
            pickup->Amount = 1;
            pickup->HealAmount = 20.0f;
        }
        return go;
    }

    void Update(float deltaTime) override {
        if (m_Collected || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_Age += deltaTime;

        if (Kind != PickupKind::AshEmber || m_Age < MagnetDelay) {
            return;
        }

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        if (!go || !scene || !rb) {
            return;
        }

        auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        if (!player || player->IsPendingDestroy()) {
            return;
        }

        float attractionRange = 5.5f;
        if (auto* stats = player->GetComponent<PlayerStatsComponent>()) {
            attractionRange = std::max(0.5f, stats->AshAttractionRange);
        }

        const glm::vec3 selfPos = go->GetTransform()->GetWorldPosition();
        const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
        const glm::vec2 toPlayer{ playerPos.x - selfPos.x, playerPos.z - selfPos.z };
        const float distSq = glm::dot(toPlayer, toPlayer);
        if (distSq <= 1e-6f || distSq > attractionRange * attractionRange) {
            return;
        }

        const float dist = std::sqrt(distSq);
        const glm::vec2 dir = toPlayer / dist;
        glm::vec2 vel = rb->GetVelocityXZ() + dir * (MagnetAcceleration * deltaTime);
        const float speed = glm::length(vel);
        if (std::isfinite(speed) && speed > MaxMagnetSpeed) {
            vel = (vel / speed) * MaxMagnetSpeed;
        }
        rb->SetVelocityXZ(vel);
    }

    void OnTriggerEnter2D(Engine::GameObject* other) override {
        if (m_Collected || !other || other->IsPendingDestroy()) {
            return;
        }
        if (other->GetTag() != Engine::EntityTag::Player) {
            return;
        }

        auto* stats = other->GetComponent<PlayerStatsComponent>();
        if (!stats) {
            stats = other->AddComponent<PlayerStatsComponent>();
        }

        switch (Kind) {
            case PickupKind::WoodLog:
                stats->AddWood(Amount);
                Sandbox::PlaySfxCue("assets/audio/sfx_pickup_wood.wav");
                break;
            case PickupKind::AshEmber:
                stats->AddXP(Amount);
                stats->AddRunAshes(SecondaryAmount);
                Sandbox::PlaySfxCue("assets/audio/sfx_pickup_ash.wav");
                break;
            case PickupKind::CrimsonBerry:
                stats->Heal(HealAmount);
                Sandbox::PlaySfxCue("assets/audio/sfx_pickup_ash.wav");
                break;
        }

        m_Collected = true;
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene()) {
                scene->DestroyGameObject(go);
            }
        }
    }

private:
    float m_Age = 0.0f;
    bool m_Collected = false;
};

#endif // SANDBOX_PICKUP_COMPONENT_H
