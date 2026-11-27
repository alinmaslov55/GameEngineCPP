#ifndef SANDBOX_PICKUP_COMPONENT_H
#define SANDBOX_PICKUP_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityPoolManager.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Prefab.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

enum class PickupKind : uint8_t {
    WoodLog = 0,
    AshEmber = 1,
    CrimsonBerry = 2
};

class PickupComponent;

namespace Sandbox {

struct PickupSpec {
    PickupKind Kind = PickupKind::WoodLog;
    const char* Name = "WoodLogPickup";
    const char* PoolName = "pool_pickup_wood";
    const char* TexturePath = "assets/diorama/wood_log.png";
    const char* SfxPath = "assets/audio/sfx_pickup_wood.wav";
    glm::vec4 Tint = { 0.58f, 0.38f, 0.20f, 1.0f };
    glm::vec3 Scale = { 0.42f, 0.42f, 1.0f };
    float Emissive = 0.0f;
    int DefaultAmount = 1;
    int DefaultSecondaryAmount = 0;
    float DefaultHealAmount = 0.0f;
    bool MagnetizeToPlayer = false;
    void (*ApplyEffect)(Engine::GameObject& player, PlayerStatsComponent& stats, const PickupComponent& pickup) = nullptr;
};

} // namespace Sandbox

class PickupComponent final : public Engine::Component {
public:
    PickupKind Kind = PickupKind::WoodLog;
    int Amount = 1;
    int SecondaryAmount = 5; // Used by AshEmber for RunAshes alongside XP
    float HealAmount = 20.0f;
    float MagnetDelay = 0.25f;
    float MagnetAcceleration = 32.0f;
    float MaxMagnetSpeed = 14.0f;

    [[nodiscard]] static const Sandbox::PickupSpec& GetSpec(PickupKind kind) noexcept {
        static const std::array<Sandbox::PickupSpec, 3> kSpecs = { {
            Sandbox::PickupSpec{
                .Kind = PickupKind::WoodLog,
                .Name = "WoodLogPickup",
                .PoolName = "pool_pickup_wood",
                .TexturePath = "assets/diorama/wood_log.png",
                .SfxPath = "assets/audio/sfx_pickup_wood.wav",
                .Tint = { 0.58f, 0.38f, 0.20f, 1.0f },
                .Scale = { 0.42f, 0.42f, 1.0f },
                .Emissive = 0.0f,
                .DefaultAmount = 1,
                .DefaultSecondaryAmount = 0,
                .DefaultHealAmount = 0.0f,
                .MagnetizeToPlayer = false,
                .ApplyEffect = [](Engine::GameObject&, PlayerStatsComponent& stats, const PickupComponent& p) {
                    stats.AddWood(p.Amount);
                }
            },
            Sandbox::PickupSpec{
                .Kind = PickupKind::AshEmber,
                .Name = "AshEmberPickup",
                .PoolName = "pool_pickup_ash",
                .TexturePath = "assets/diorama/ash_ember.png",
                .SfxPath = "assets/audio/sfx_pickup_ash.wav",
                .Tint = { 1.0f, 0.62f, 0.22f, 1.0f },
                .Scale = { 0.34f, 0.34f, 1.0f },
                .Emissive = 0.85f,
                .DefaultAmount = 10,
                .DefaultSecondaryAmount = 5,
                .DefaultHealAmount = 0.0f,
                .MagnetizeToPlayer = true,
                .ApplyEffect = [](Engine::GameObject&, PlayerStatsComponent& stats, const PickupComponent& p) {
                    stats.AddXP(p.Amount);
                    stats.AddRunAshes(p.SecondaryAmount);
                }
            },
            Sandbox::PickupSpec{
                .Kind = PickupKind::CrimsonBerry,
                .Name = "CrimsonBerryPickup",
                .PoolName = "pool_pickup_berry",
                .TexturePath = "assets/diorama/crimson_heart.png",
                .SfxPath = "assets/audio/sfx_pickup_ash.wav",
                .Tint = { 0.92f, 0.16f, 0.24f, 1.0f },
                .Scale = { 0.38f, 0.38f, 1.0f },
                .Emissive = 0.0f,
                .DefaultAmount = 1,
                .DefaultSecondaryAmount = 0,
                .DefaultHealAmount = 20.0f,
                .MagnetizeToPlayer = false,
                .ApplyEffect = [](Engine::GameObject& player, PlayerStatsComponent&, const PickupComponent& p) {
                    Sandbox::HealPlayerOrTarget(&player, p.HealAmount);
                }
            }
        } };
        const size_t idx = static_cast<size_t>(kind);
        return kSpecs[idx < kSpecs.size() ? idx : 0u];
    }

    static Engine::GameObject* SpawnPickup(Engine::Scene& scene,
                                           PickupKind kind,
                                           const glm::vec3& originPos,
                                           const glm::vec2& scatterVelXZ,
                                           float launchVerticalVel) {
        const auto& spec = GetSpec(kind);
        static std::array<std::shared_ptr<Engine::Prefab>, 3> s_Prefabs;
        const size_t specIdx = static_cast<size_t>(kind) < 3u ? static_cast<size_t>(kind) : 0u;
        if (!s_Prefabs[specIdx]) {
            nlohmann::json entityJson = {
                { "Name", spec.Name },
                { "Components", {
                    { "SpriteRenderer", nlohmann::json::object() },
                    { "Rigidbody2D", {
                        { "Type", "Dynamic" },
                        { "UseGravityY", true },
                        { "GroundLevelY", 0.0f },
                        { "GroundRestitutionY", 0.45f },
                        { "LinearDamping", 3.5f }
                    } },
                    { "CircleCollider2D", {
                        { "Radius", 0.40f },
                        { "IsSensor", true },
                        { "CategoryBits", Engine::PhysicsLayer::Pickup },
                        { "MaskBits", Engine::PhysicsLayer::Player }
                    } }
                } }
            };
            s_Prefabs[specIdx] = std::make_shared<Engine::Prefab>(std::move(entityJson));
        }

        auto& poolMgr = scene.GetPoolManager();
        if (poolMgr.GetPoolTotalCount(spec.PoolName) == 0u) {
            poolMgr.PrewarmPool(spec.PoolName, *s_Prefabs[specIdx], 4u);
        }

        const glm::vec3 spawnPos{ originPos.x, std::max(0.15f, originPos.y), originPos.z };
        Engine::GameObject* go = poolMgr.AcquirePooled(spec.PoolName, *s_Prefabs[specIdx], spawnPos);
        if (!go) {
            go = scene.CreateGameObject(spec.Name);
            go->AddComponent<Engine::SpriteRenderer>();
            go->AddComponent<Engine::Rigidbody2D>();
            go->AddComponent<Engine::CircleCollider2D>();
            go->GetTransform()->position = spawnPos;
        }

        go->SetTag(Engine::EntityTag::ResourcePickup);
        go->SetLayer(Engine::EntityLayer::Pickup);
        go->GetTransform()->scale = spec.Scale;

        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
            sr->SetColor(spec.Tint);
            sr->SetEmissive(spec.Emissive);
            if (auto tex = Engine::ResourceManager::Load<Engine::Texture>(spec.TexturePath)) {
                sr->SetTexture(tex);
                sr->SetAlphaCutoff(0.18f);
            }
        }

        if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
            rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
            rb->UseGravityY = true;
            rb->GroundLevelY = 0.0f;
            rb->GroundRestitutionY = 0.45f;
            rb->LinearDamping = 3.5f;
            rb->LaunchVertical(std::clamp(launchVerticalVel, 4.5f, 6.5f));
            rb->SetVelocityXZ(scatterVelXZ);
        }

        if (auto* col = go->GetComponent<Engine::CircleCollider2D>()) {
            col->Radius = 0.40f;
            col->IsSensor = true;
            col->CategoryBits = Engine::PhysicsLayer::Pickup;
            col->MaskBits = Engine::PhysicsLayer::Player;
        }

        auto* pickup = go->GetComponent<PickupComponent>();
        if (!pickup) {
            pickup = go->AddComponent<PickupComponent>();
        }
        pickup->Kind = spec.Kind;
        pickup->Amount = spec.DefaultAmount;
        pickup->SecondaryAmount = spec.DefaultSecondaryAmount;
        pickup->HealAmount = spec.DefaultHealAmount;
        pickup->m_Age = 0.0f;
        pickup->m_Collected = false;
        return go;
    }

    void OnPoolAcquire() override {
        m_Age = 0.0f;
        m_Collected = false;
    }

    void OnPoolRelease() override {
        m_Collected = true;
    }

    void Update(float deltaTime) override {
        if (m_Collected || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_Age += deltaTime;

        const auto& spec = GetSpec(Kind);
        if (!spec.MagnetizeToPlayer || m_Age < MagnetDelay) {
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

        const auto& spec = GetSpec(Kind);
        if (spec.ApplyEffect) {
            spec.ApplyEffect(*other, *stats, *this);
        }
        if (spec.SfxPath) {
            Sandbox::PlaySfxCue(spec.SfxPath);
        }

        m_Collected = true;
        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene()) {
                if (go->IsPooled()) {
                    scene->GetPoolManager().ReleaseToPool(go);
                } else {
                    scene->DestroyGameObject(go);
                }
            }
        }
    }

private:
    float m_Age = 0.0f;
    bool m_Collected = false;
};

#endif // SANDBOX_PICKUP_COMPONENT_H
