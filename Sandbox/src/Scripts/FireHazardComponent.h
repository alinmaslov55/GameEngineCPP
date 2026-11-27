#ifndef SANDBOX_FIRE_HAZARD_COMPONENT_H
#define SANDBOX_FIRE_HAZARD_COMPONENT_H

#include "Scripts/CombatDamageHelper.h"
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

            DamageEvent evt;
            evt.Amount = FireDPS * tickDt;
            evt.Source = go;
            evt.IsFire = true;
            evt.BypassInvuln = true;
            StatusApplication status;
            status.BurnDurationSec = BurnDurationSec;
            status.BurnDPS = BurnSpreadDPS;
            Sandbox::CombatDamage::Apply(enemy, evt, status);
        }
    }

private:
    float m_TickTimer = 0.0f;
};

#endif // SANDBOX_FIRE_HAZARD_COMPONENT_H
