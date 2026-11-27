#ifndef SANDBOX_RUNE_STRATEGIES_H
#define SANDBOX_RUNE_STRATEGIES_H

#include "Scripts/CombatDamageHelper.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/RuneEffectsComponents.h"
#include "Scripts/StatusEffectsComponent.h"

#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <cmath>
#include <string_view>
#include <unordered_map>

namespace Sandbox {

struct RuneCastContext {
    Engine::Scene& Scene;
    Engine::GameObject& Caster;
    const Engine::RuneDefinition& Rune;
    const glm::vec3& Origin;
};

/// @brief Non-Component strategy interface for active Rune spells (0 ComponentMask128 bits).
class IRuneStrategy {
public:
    virtual ~IRuneStrategy() = default;
    virtual void Cast(const RuneCastContext& ctx) const = 0;
};

class IgnitionRuneStrategy final : public IRuneStrategy {
public:
    void Cast(const RuneCastContext& ctx) const override {
        const float r = ctx.Rune.Radius;
        const float rSq = r * r;
        ctx.Scene.IgniteAtXZ({ ctx.Origin.x, ctx.Origin.z }, r);

        for (Engine::GameObject* enemy : ctx.Scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - ctx.Origin.x;
            const float dz = ePos.z - ctx.Origin.z;
            if ((dx * dx + dz * dz) > rSq) {
                continue;
            }

            DamageEvent evt;
            evt.Amount = 10.0f;
            evt.Source = &ctx.Caster;
            evt.IsFire = true;
            StatusApplication status;
            status.BurnDurationSec = ctx.Rune.DurationSec;
            status.BurnDPS = 12.0f;
            CombatDamage::Apply(enemy, evt, status);
        }
    }
};

class ChronosRuneStrategy final : public IRuneStrategy {
public:
    void Cast(const RuneCastContext& ctx) const override {
        const float r = ctx.Rune.Radius;
        const float rSq = r * r;
        for (Engine::GameObject* enemy : ctx.Scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - ctx.Origin.x;
            const float dz = ePos.z - ctx.Origin.z;
            if ((dx * dx + dz * dz) > rSq) {
                continue;
            }

            auto* status = enemy->GetComponent<StatusEffectsComponent>();
            if (!status) {
                status = enemy->AddComponent<StatusEffectsComponent>();
            }
            if (!status->OnStasisBurst) {
                status->OnStasisBurst = [](Engine::GameObject& target, const glm::vec2&) {
                    if (auto* bowling = target.GetComponent<KineticBowlingComponent>()) {
                        bowling->Launch(0.6f);
                    }
                };
            }
            status->ApplyStasis(ctx.Rune.DurationSec);
        }
    }
};

class ShockwaveRuneStrategy final : public IRuneStrategy {
public:
    void Cast(const RuneCastContext& ctx) const override {
        const float r = ctx.Rune.Radius;
        const float rSq = r * r;
        constexpr float kShockwaveImpulse = 28.0f;

        for (Engine::GameObject* enemy : ctx.Scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const glm::vec2 delta{ ePos.x - ctx.Origin.x, ePos.z - ctx.Origin.z };
            const float distSq = glm::dot(delta, delta);
            if (distSq > rSq) {
                continue;
            }

            const glm::vec2 dir = SafeNormalizeXZ(delta);
            DamageEvent evt;
            evt.Amount = 15.0f;
            evt.Source = &ctx.Caster;
            evt.ImpulseXZ = dir * kShockwaveImpulse;
            evt.StunDuration = 0.35f;
            CombatDamage::Apply(enemy, evt);
            if (auto* bowling = enemy->GetComponent<KineticBowlingComponent>()) {
                bowling->Launch(0.6f);
            }
        }

        auto* ringGo = ctx.Scene.CreateGameObject("ShockwaveRing");
        ringGo->GetTransform()->position = { ctx.Origin.x, 0.02f, ctx.Origin.z };
        auto* sr = ringGo->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
        sr->SetColor({ 1.0f, 0.72f, 0.32f, 0.85f });
        sr->SetEmissive(1.2f);
        auto* ring = ringGo->AddComponent<ShockwaveRingComponent>();
        ring->MaxRadius = r;
        ring->DurationSec = ctx.Rune.DurationSec;

        if (auto* pc = ctx.Caster.GetComponent<PlayerController>()) {
            pc->TriggerImpactFeedback(0.42f, { 0.0f, -0.25f });
        }
    }
};

class ConsecrationRuneStrategy final : public IRuneStrategy {
public:
    void Cast(const RuneCastContext& ctx) const override {
        auto* zoneGo = ctx.Scene.CreateGameObject("ConsecrationZone");
        zoneGo->SetTag(Engine::EntityTag::SafeZoneBrazier);
        zoneGo->GetTransform()->position = { ctx.Origin.x, 0.02f, ctx.Origin.z };
        zoneGo->GetTransform()->scale = { ctx.Rune.Radius * 2.0f, ctx.Rune.Radius * 2.0f, 1.0f };

        auto* sr = zoneGo->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
        sr->SetColor({ 1.0f, 0.86f, 0.38f, 0.32f });
        sr->SetEmissive(0.75f);

        auto* light = zoneGo->AddComponent<Engine::PointLightComponent>();
        light->Color = { 1.0f, 0.84f, 0.36f };
        light->Intensity = 2.1f;
        light->Radius = ctx.Rune.Radius;
        light->Falloff = 1.6f;
        light->Priority = Engine::LightPriority::High_Tactical;
        light->Semantic = Engine::LightSemantic::SafeLifeline;

        auto* zone = zoneGo->AddComponent<ConsecrationZoneComponent>();
        zone->Radius = ctx.Rune.Radius;
        zone->DurationSec = ctx.Rune.DurationSec;
    }
};

class RuneRegistry {
public:
    [[nodiscard]] static const IRuneStrategy* FindStrategy(std::string_view runeId) {
        static const IgnitionRuneStrategy s_Ignite;
        static const ChronosRuneStrategy s_Chronos;
        static const ShockwaveRuneStrategy s_Shockwave;
        static const ConsecrationRuneStrategy s_Consecrate;

        static const std::unordered_map<std::string_view, const IRuneStrategy*> kStrategies = {
            { "rune_ignite", &s_Ignite },
            { "rune_chronos", &s_Chronos },
            { "rune_shockwave", &s_Shockwave },
            { "rune_consecrate", &s_Consecrate },
        };
        const auto it = kStrategies.find(runeId);
        return (it != kStrategies.end()) ? it->second : nullptr;
    }

    static bool TryGetFallbackDefinition(std::string_view runeId, Engine::RuneDefinition& outDef) {
        static const std::unordered_map<std::string_view, Engine::RuneDefinition> kFallbacks = {
            { "rune_ignite", { "rune_ignite", "Rune of Ignition", "", 15.0f, 4.0f, 5.0f, 3.5f, "" } },
            { "rune_chronos", { "rune_chronos", "Rune of Chronos", "", 20.0f, 8.0f, 7.0f, 4.0f, "" } },
            { "rune_shockwave", { "rune_shockwave", "Rune of Shockwave", "", 12.0f, 3.5f, 6.0f, 0.5f, "" } },
            { "rune_consecrate", { "rune_consecrate", "Rune of Consecration", "", 25.0f, 10.0f, 8.0f, 6.0f, "" } },
        };
        const auto it = kFallbacks.find(runeId);
        if (it == kFallbacks.end()) {
            return false;
        }
        outDef = it->second;
        return true;
    }
};

} // namespace Sandbox

#endif // SANDBOX_RUNE_STRATEGIES_H
