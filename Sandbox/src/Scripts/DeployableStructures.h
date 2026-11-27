#ifndef SANDBOX_DEPLOYABLE_STRUCTURES_H
#define SANDBOX_DEPLOYABLE_STRUCTURES_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PickupComponent.h"
#include "Scripts/SplinterProjectileComponent.h"

#include <Engine/Particles/DecalManager.h>
#include <Engine/Physics/Collision2DInfo.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

/// @brief 4 Wood, 120 HP: LowObstacle palisade dealing contact splinter damage and shattering into 5-7 splinters.
class SplinterBarricadeComponent final : public Engine::Component {
public:
    float MaxHP = 120.0f;
    float ContactDamage = 10.0f;
    int ShatterSplinterCount = 6;

    void Awake() override {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        go->SetTag(Engine::EntityTag::TacticalStructure);
        go->SetLayer(Engine::EntityLayer::LowObstacle);

        if (!go->GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go->AddComponent<Engine::BoxCollider2D>();
            col->size = { 1.1f, 0.55f };
            col->CategoryBits = Engine::PhysicsLayer::LowObstacle;
        }

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(MaxHP);
        health->DestroyOnDeath = true;
        health->OnKilled = [this](const DamageEvent& evt) {
            SpawnShatterSplinters(evt.ImpulseXZ);
        };
    }

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        auto* other = info.Other;
        if (!other || other->IsPendingDestroy() || other->GetTag() != Engine::EntityTag::Enemy) {
            return;
        }
        if (auto* enemyHealth = other->GetComponent<HealthComponent>()) {
            DamageEvent evt;
            evt.Amount = ContactDamage;
            evt.Source = GetGameObject();
            evt.ImpulseXZ = -info.NormalXZ * 4.0f;
            enemyHealth->ApplyDamage(evt);
        } else if (auto* target = other->GetComponent<CombatTargetComponent>()) {
            target->ApplyDamage(ContactDamage, 0.1f, Engine::SpriteRenderer::FlashMode::PureWhite);
        }
    }

    void SpawnShatterSplinters(const glm::vec2& impactDir) {
        if (m_Shattered) {
            return;
        }
        m_Shattered = true;

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }

        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        const float dirLen = glm::length(impactDir);
        const glm::vec2 baseDir = (std::isfinite(dirLen) && dirLen > 1e-4f)
            ? (impactDir / dirLen)
            : glm::vec2(1.0f, 0.0f);
        const float baseAngle = std::atan2(baseDir.y, baseDir.x);

        const int count = std::clamp(ShatterSplinterCount, 5, 7);
        constexpr float kSpreadRad = 0.65f;
        for (int i = 0; i < count; ++i) {
            const float t = (count > 1) ? (static_cast<float>(i) / static_cast<float>(count - 1)) : 0.5f;
            const float angle = baseAngle + (t - 0.5f) * 2.0f * kSpreadRad;
            const glm::vec2 dir{ std::cos(angle), std::sin(angle) };

            auto* splinter = scene->CreateGameObject("BarricadeSplinter");
            splinter->SetTag(Engine::EntityTag::Projectile);
            splinter->SetLayer(Engine::EntityLayer::Projectile);
            splinter->GetTransform()->position = { origin.x + dir.x * 0.35f, 0.35f, origin.z + dir.y * 0.35f };

            auto* rb = splinter->AddComponent<Engine::Rigidbody2D>();
            rb->Type = Engine::Rigidbody2D::BodyType::Kinematic;
            rb->IsBullet = true;

            auto* col = splinter->AddComponent<Engine::CircleCollider2D>();
            col->Radius = 0.22f;
            col->IsSensor = true;
            col->CategoryBits = Engine::PhysicsLayer::Projectile;
            col->MaskBits = Engine::PhysicsLayer::Enemy;

            auto* proj = splinter->AddComponent<SplinterProjectileComponent>();
            proj->DirectionXZ = dir;
        }
    }

private:
    bool m_Shattered = false;
};

/// @brief 10 Wood, 80 HP: Emits a 7.5m SafeLifeline PointLightComponent for 35s.
class FlarePegComponent final : public Engine::Component {
public:
    float MaxHP = 80.0f;
    float LightRadius = 7.5f;
    float RemainingLifetime = 35.0f;

    void Awake() override {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        go->SetTag(Engine::EntityTag::SafeZoneBrazier);

        if (!go->GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go->AddComponent<Engine::BoxCollider2D>();
            col->size = { 0.45f, 0.45f };
            col->CategoryBits = Engine::PhysicsLayer::Environment;
        }

        auto* light = go->GetComponent<Engine::PointLightComponent>();
        if (!light) {
            light = go->AddComponent<Engine::PointLightComponent>();
        }
        light->Color = { 1.0f, 0.78f, 0.36f };
        light->Intensity = 2.2f;
        light->Radius = LightRadius;
        light->Falloff = 1.6f;
        light->Priority = Engine::LightPriority::High_Tactical;
        light->Semantic = Engine::LightSemantic::SafeLifeline;
        light->CastShadows = true;

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(MaxHP);
        health->DestroyOnDeath = true;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        RemainingLifetime -= deltaTime;
        if (RemainingLifetime <= 0.0f) {
            if (auto* go = GetGameObject()) {
                if (auto* scene = go->GetScene()) {
                    scene->DestroyGameObject(go);
                }
            }
        }
    }
};

/// @brief 15 Wood, 75 HP: 2.5s reset sensor trap launching enemies in 2.2m backward + airborne.
class ThumperComponent final : public Engine::Component {
public:
    float MaxHP = 75.0f;
    float BlastRadius = 2.2f;
    float LaunchImpulseXZ = 24.0f;
    float LaunchVerticalVel = 6.0f;
    float ResetCooldown = 2.5f;

    void Awake() override {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        go->SetTag(Engine::EntityTag::TacticalStructure);

        if (!go->GetComponent<Engine::CircleCollider2D>()) {
            auto* col = go->AddComponent<Engine::CircleCollider2D>();
            col->Radius = 1.2f;
            col->IsSensor = true;
            col->CategoryBits = Engine::PhysicsLayer::LowObstacle;
            col->MaskBits = Engine::PhysicsLayer::Enemy;
        }
        if (!go->GetComponent<Engine::SquashStretchComponent>()) {
            go->AddComponent<Engine::SquashStretchComponent>();
        }

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(MaxHP);
        health->DestroyOnDeath = true;
    }

    void OnTriggerEnter2D(Engine::GameObject* other) override {
        if (m_CooldownTimer > 0.0f || !other || other->IsPendingDestroy()) {
            return;
        }
        if (other->GetTag() == Engine::EntityTag::Enemy) {
            TriggerBlast();
        }
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (m_CooldownTimer > 0.0f) {
            m_CooldownTimer = std::max(0.0f, m_CooldownTimer - deltaTime);
        }
    }

    [[nodiscard]] float GetCooldownRemaining() const noexcept { return m_CooldownTimer; }

    void TriggerBlast() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene || m_CooldownTimer > 0.0f) {
            return;
        }

        m_CooldownTimer = ResetCooldown;
        const glm::vec3 center = go->GetTransform()->GetWorldPosition();
        const float rSq = BlastRadius * BlastRadius;

        for (Engine::GameObject* enemy : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const glm::vec2 delta{ ePos.x - center.x, ePos.z - center.z };
            const float distSq = glm::dot(delta, delta);
            if (distSq > rSq) {
                continue;
            }

            const float dist = std::sqrt(distSq);
            const glm::vec2 dir = (dist > 1e-4f) ? (delta / dist) : glm::vec2(1.0f, 0.0f);

            if (auto* rb = enemy->GetComponent<Engine::Rigidbody2D>()) {
                rb->LaunchVertical(LaunchVerticalVel);
                rb->ApplyImpulseXZ(dir * LaunchImpulseXZ);
            }
            if (auto* bowling = enemy->GetComponent<KineticBowlingComponent>()) {
                bowling->Launch(0.6f);
            }
        }

        if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
            ss->TriggerImpulse(3.2f, 12.0f);
        }

        Engine::GroundDecal decal;
        decal.Position = { center.x, 0.0f, center.z };
        decal.SizeXZ = { BlastRadius * 1.4f, BlastRadius * 1.4f };
        decal.Color = { 0.35f, 0.30f, 0.25f, 0.70f };
        scene->GetDecalManager().SpawnDecal(decal);
    }

private:
    float m_CooldownTimer = 0.0f;
};

/// @brief 20 Wood, 100 HP: Paints 3.2m flammable Tar (layer 2) & spawns 1 AshEmber every 3s.
class TarSiphonComponent final : public Engine::Component {
public:
    float MaxHP = 100.0f;
    float TarRadius = 3.2f;
    float CycleInterval = 3.0f;

    void Awake() override {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        go->SetTag(Engine::EntityTag::TacticalStructure);

        if (!go->GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go->AddComponent<Engine::BoxCollider2D>();
            col->size = { 0.75f, 0.75f };
            col->CategoryBits = Engine::PhysicsLayer::Environment;
        }

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(MaxHP);
        health->DestroyOnDeath = true;
    }

    void Start() override {
        PaintTarPool();
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_CycleTimer += deltaTime;
        if (m_CycleTimer >= CycleInterval) {
            m_CycleTimer -= CycleInterval;
            PaintTarPool();
            SpawnAshEmber();
        }
    }

private:
    void PaintTarPool() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }
        const glm::vec3 pos = go->GetTransform()->GetWorldPosition();
        scene->PaintMaterialWorld({ pos.x, pos.z }, 2u, TarRadius, 1.0f);
    }

    void SpawnAshEmber() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }
        const glm::vec3 pos = go->GetTransform()->GetWorldPosition();
        PickupComponent::SpawnPickup(*scene, PickupKind::AshEmber, pos, { 1.2f, 0.6f }, 5.0f);
    }

    float m_CycleTimer = 0.0f;
};

#endif // SANDBOX_DEPLOYABLE_STRUCTURES_H
