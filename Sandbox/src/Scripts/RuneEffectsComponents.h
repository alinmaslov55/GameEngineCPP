#ifndef SANDBOX_RUNE_EFFECTS_COMPONENTS_H
#define SANDBOX_RUNE_EFFECTS_COMPONENTS_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/StatusEffectsComponent.h"

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

/// @brief Expanding flat-ground shockwave ring VFX (VFX-01) spawned by Rune of Shockwave.
class ShockwaveRingComponent final : public Engine::Component {
public:
    float MaxRadius = 6.0f;
    float DurationSec = 0.5f;

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_Elapsed += deltaTime;
        const float dur = std::max(0.05f, DurationSec);
        const float t = std::clamp(m_Elapsed / dur, 0.0f, 1.0f);

        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        const float radius = 0.5f + (MaxRadius - 0.5f) * t;
        go->GetTransform()->scale = { radius * 2.0f, radius * 2.0f, 1.0f };
        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            sr->SetColor({ 1.0f, 0.72f, 0.32f, 0.85f * (1.0f - t) });
        }
        if (t >= 1.0f) {
            if (auto* scene = go->GetScene()) {
                scene->DestroyGameObject(go);
            }
        }
    }

private:
    float m_Elapsed = 0.0f;
};

/// @brief Holds an enemy in Chronos Stasis with SpectralBlue tint until unfreeze erupts queued impulses.
class ChronosStasisTrackerComponent final : public Engine::Component {
public:
    float RemainingSec = 4.0f;
    float BowlingImpulseThreshold = 10.0f;

    void Refresh(float durationSec) {
        if (!std::isfinite(durationSec) || durationSec <= 0.0f) {
            return;
        }
        RemainingSec = m_Released ? durationSec : std::max(RemainingSec, durationSec);
        m_Released = false;
        ApplyFrozenVisuals(true);
    }

    void Start() override {
        if (RemainingSec > 0.0f) {
            m_Released = false;
            ApplyFrozenVisuals(true);
        } else {
            m_Released = true;
        }
    }

    void OnPoolAcquire() override {
        RemainingSec = 0.0f;
        m_Released = true;
        ApplyFrozenVisuals(false);
    }

    void OnPoolRelease() override {
        RemainingSec = 0.0f;
        m_Released = true;
        ApplyFrozenVisuals(false);
    }

    void Update(float deltaTime) override {
        if (m_Released || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        RemainingSec -= deltaTime;
        if (RemainingSec <= 0.0f) {
            ReleaseStasis();
        }
    }

    void OnDestroy() override {
        if (!m_Released) {
            ApplyFrozenVisuals(false);
        }
    }

private:
    void ApplyFrozenVisuals(bool frozen) {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
            rb->SetStasisFrozen(frozen);
        }
        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            if (frozen) {
                sr->SetFlashMode(Engine::SpriteRenderer::FlashMode::SpectralBlue);
                sr->SetFlashAmount(0.75f);
            } else {
                sr->SetFlashAmount(0.0f);
            }
        }
    }

    void ReleaseStasis() {
        m_Released = true;
        auto* go = GetGameObject();
        if (!go) {
            return;
        }

        glm::vec2 queuedImpulse{ 0.0f };
        if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
            queuedImpulse = rb->GetQueuedStasisImpulseXZ();
            rb->SetStasisFrozen(false);
        }
        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            sr->SetFlashAmount(0.0f);
        }
        if (glm::length(queuedImpulse) >= BowlingImpulseThreshold) {
            if (auto* bowling = go->GetComponent<KineticBowlingComponent>()) {
                bowling->Launch(0.6f);
            }
        }
    }

    bool m_Released = false;
};

/// @brief 6s hallowed SafeLifeline circle spawned by Rune of Consecration: heals Player & burns/repels enemies.
class ConsecrationZoneComponent final : public Engine::Component {
public:
    float Radius = 8.0f;
    float DurationSec = 6.0f;
    float PlayerHealPerSec = 4.0f;
    float EnemyBurnDPS = 10.0f;
    float RepelImpulse = 8.0f;
    float TickInterval = 0.25f;

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_Elapsed += deltaTime;
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }

        if (m_Elapsed >= DurationSec) {
            scene->DestroyGameObject(go);
            return;
        }

        const glm::vec3 center = go->GetTransform()->GetWorldPosition();
        const float rSq = Radius * Radius;

        // Gentle continuous heal for Player inside the hallowed circle
        if (auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player)) {
            const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
            const float dx = pPos.x - center.x;
            const float dz = pPos.z - center.z;
            if ((dx * dx + dz * dz) <= rSq) {
                Sandbox::HealPlayerOrTarget(player, PlayerHealPerSec * deltaTime);
            }
        }

        // Periodic radiant burn + outward repulsion on Enemies
        m_TickTimer += deltaTime;
        if (m_TickTimer >= TickInterval) {
            const float tickDt = m_TickTimer;
            m_TickTimer = 0.0f;

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
                const glm::vec2 outDir = (dist > 1e-4f) ? (delta / dist) : glm::vec2(1.0f, 0.0f);

                if (auto* health = enemy->GetComponent<HealthComponent>()) {
                    DamageEvent evt;
                    evt.Amount = EnemyBurnDPS * tickDt;
                    evt.Source = go;
                    evt.ImpulseXZ = outDir * (RepelImpulse * tickDt * 4.0f);
                    evt.IsFire = true;
                    evt.IsRadiant = true;
                    evt.BypassInvuln = true;
                    health->ApplyDamage(evt);
                } else {
                    if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                        target->ApplyDamage(EnemyBurnDPS * tickDt, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
                    }
                    if (auto* rb = enemy->GetComponent<Engine::Rigidbody2D>()) {
                        rb->ApplyImpulseXZ(outDir * (RepelImpulse * tickDt * 4.0f));
                    }
                }
            }
        }
    }

private:
    float m_Elapsed = 0.0f;
    float m_TickTimer = 0.0f;
};

#endif // SANDBOX_RUNE_EFFECTS_COMPONENTS_H
