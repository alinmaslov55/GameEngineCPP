#ifndef SANDBOX_SPLINTER_PROJECTILE_COMPONENT_H
#define SANDBOX_SPLINTER_PROJECTILE_COMPONENT_H

#include "Scripts/CombatDamageHelper.h"
#include "Scripts/CombatTargetComponent.h"

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
