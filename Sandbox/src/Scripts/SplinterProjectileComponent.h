#ifndef SANDBOX_SPLINTER_PROJECTILE_COMPONENT_H
#define SANDBOX_SPLINTER_PROJECTILE_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/HealthComponent.h"

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Physics/PhysicsQueries.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

class SplinterProjectileComponent final : public Engine::Component {
public:
    glm::vec2 DirectionXZ{ 1.0f, 0.0f };
    float Speed = 14.0f;
    float Damage = 18.0f;
    float KnockbackImpulse = 6.0f;
    float Lifetime = 0.65f;
    int RemainingPierces = 3;

    static void SpawnBurst(Engine::Scene& scene,
                           const glm::vec3& origin,
                           const glm::vec2& impactDir,
                           int count = 6,
                           float spreadRad = 0.65f) {
        Engine::SoundPlayParams shatterParams;
        shatterParams.Bus = Engine::AudioBus::SFX;
        shatterParams.Volume = 0.95f;
        shatterParams.SpatializedXZ = true;
        shatterParams.WorldPos = origin;
        Sandbox::PlaySfxCue("assets/audio/sfx_barricade_shatter.wav", shatterParams);

        const glm::vec2 baseDir = Sandbox::SafeNormalizeXZ(impactDir);
        const float baseAngle = std::atan2(baseDir.y, baseDir.x);
        const int clampedCount = std::clamp(count, 5, 7);
        for (int i = 0; i < clampedCount; ++i) {
            const float t = (clampedCount > 1)
                ? (static_cast<float>(i) / static_cast<float>(clampedCount - 1))
                : 0.5f;
            const float angle = baseAngle + (t - 0.5f) * 2.0f * spreadRad;
            const glm::vec2 dir{ std::cos(angle), std::sin(angle) };

            auto* splinter = scene.CreateGameObject("BarricadeSplinter");
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

    void OnTriggerEnter2D(Engine::GameObject* other) override {
        HandleHit(other);
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        auto* go = GetGameObject();
        auto* transform = go ? go->GetTransform() : nullptr;
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !transform || !scene || go->IsPendingDestroy()) {
            return;
        }

        Lifetime -= deltaTime;
        if (Lifetime <= 0.0f || RemainingPierces <= 0) {
            scene->DestroyGameObject(go);
            return;
        }

        const float len = glm::length(DirectionXZ);
        const glm::vec2 dir = (std::isfinite(len) && len > 1e-4f) ? (DirectionXZ / len) : glm::vec2(1.0f, 0.0f);
        transform->position.x += dir.x * Speed * deltaTime;
        transform->position.z += dir.y * Speed * deltaTime;
        transform->SetDirty();

        if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
            if (rb->RuntimeBody.IsValid()) {
                rb->SetVelocityXZ(dir * Speed);
            }
        }
    }

private:
    void HandleHit(Engine::GameObject* other) {
        auto* self = GetGameObject();
        if (!self || !other || other == self || other->IsPendingDestroy() || RemainingPierces <= 0) {
            return;
        }
        if (other->GetTag() != Engine::EntityTag::Enemy) {
            return;
        }
        const uint64_t uuid = other->GetUUID();
        if (!m_HitUUIDs.insert(uuid).second) {
            return;
        }

        const glm::vec2 dir = Sandbox::SafeNormalizeXZ(DirectionXZ);
        Sandbox::ApplyImpactDamage(other, self, Damage, 0.15f, dir * KnockbackImpulse, false);

        --RemainingPierces;
        if (RemainingPierces <= 0) {
            if (auto* scene = self->GetScene()) {
                scene->DestroyGameObject(self);
            }
        }
    }

    std::unordered_set<uint64_t> m_HitUUIDs;
};

#endif // SANDBOX_SPLINTER_PROJECTILE_COMPONENT_H
