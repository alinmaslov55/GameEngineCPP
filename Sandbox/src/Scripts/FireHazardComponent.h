#ifndef SANDBOX_FIRE_HAZARD_COMPONENT_H
#define SANDBOX_FIRE_HAZARD_COMPONENT_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/StatusEffectsComponent.h"

#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>

#include <algorithm>
#include <cmath>

/// @brief Applies 14 DPS fire damage and burning-body ignition to enemies crossing active TerrainFireGrid cells.
class FireHazardComponent final : public Engine::Component {
public:
    float FireDPS = 14.0f;
    float BurnDurationSec = 2.5f;
    float BurnSpreadDPS = 6.0f;
    float TickInterval = 0.25f;

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_TickTimer += deltaTime;
        if (m_TickTimer < TickInterval) {
            return;
        }
        const float tickDt = m_TickTimer;
        m_TickTimer = 0.0f;

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        for (Engine::GameObject* enemy : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            if (!scene->IsBurningAtXZ(ePos.x, ePos.z)) {
                continue;
            }

            if (auto* health = enemy->GetComponent<HealthComponent>()) {
                DamageEvent evt;
                evt.Amount = FireDPS * tickDt;
                evt.Source = go;
                evt.IsFire = true;
                evt.BypassInvuln = true;
                health->ApplyDamage(evt);
            } else if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                target->ApplyDamage(FireDPS * tickDt, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
                target->ApplyBurn(BurnDurationSec, BurnSpreadDPS);
            }

            if (auto* status = enemy->GetComponent<StatusEffectsComponent>()) {
                status->ApplyBurn(BurnDurationSec, BurnSpreadDPS);
            }
        }
    }

private:
    float m_TickTimer = 0.0f;
};

#endif // SANDBOX_FIRE_HAZARD_COMPONENT_H
