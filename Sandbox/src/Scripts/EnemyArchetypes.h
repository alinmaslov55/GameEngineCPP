#ifndef SANDBOX_ENEMY_ARCHETYPES_H
#define SANDBOX_ENEMY_ARCHETYPES_H

#include "Scripts/CombatDamageHelper.h"
#include "Scripts/PickupComponent.h"
#include "Scripts/TorchComponent.h"

#include <Engine/AI/Ballistics.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Sandbox {

struct EnemyWindupSpec {
    float DurationSec = 0.25f;
    Engine::SpriteRenderer::FlashMode Flash = Engine::SpriteRenderer::FlashMode::PureWhite;
};

struct EnemyAttackContext {
    Engine::GameObject& Self;
    Engine::Scene& Scene;
    Engine::Rigidbody2D* Rb = nullptr;
    const Engine::EnemyDefinition& Stats;

    float AttackRange = 1.45f;
    float HopRange = 5.5f;
    float LungeRange = 5.0f;
    float HopCooldown = 2.8f;
    float LungeCooldown = 3.5f;
    float WispDartCooldown = 3.2f;

    float& StateTimer;
    float& AttackCooldown;
    float& SpecialCooldown;
    bool& IsAirborneHop;
    glm::vec2& LandingSpotXZ;

    std::function<void(Engine::GameObject*, bool, bool)> ApplyHitFn;
    std::function<void(const glm::vec2&)> SpawnLandingFn;
    std::function<void()> TransitionToChaseFn;
};

/// @brief Non-Component strategy interface encapsulating per-archetype enemy behaviors (0 ComponentMask128 bits).
class IEnemyArchetype {
public:
    virtual ~IEnemyArchetype() = default;

    [[nodiscard]] virtual const char* GetTexturePath() const noexcept = 0;
    [[nodiscard]] virtual EnemyWindupSpec GetWindupSpec() const noexcept { return {}; }

    [[nodiscard]] virtual Engine::GameObject* SelectTarget(Engine::Scene& scene,
                                                           const glm::vec3& /*selfPos*/) const {
        return scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player);
    }

    [[nodiscard]] virtual float GetOrbitRadius() const noexcept { return 0.0f; }
    [[nodiscard]] virtual bool ShouldStartSpecial(float /*dist*/, float /*specialCooldown*/) const noexcept {
        return false;
    }

    [[nodiscard]] virtual glm::vec2 ModifyChaseTarget(Engine::Scene& /*scene*/,
                                                      const glm::vec2& /*selfXZ*/,
                                                      const glm::vec2& targetXZ,
                                                      float /*dist*/) const {
        return targetXZ;
    }

    virtual void OnEnterAttack(EnemyAttackContext& ctx,
                               Engine::GameObject* target,
                               const glm::vec2& /*selfXZ*/,
                               const glm::vec2& /*targetXZ*/) const {
        ctx.StateTimer = 0.20f;
        ctx.AttackCooldown = 1.0f;
        ctx.ApplyHitFn(target, false, true);
    }

    virtual void OnUpdateAttack(EnemyAttackContext& ctx, float /*dt*/) const {
        if (ctx.StateTimer <= 0.0f) {
            ctx.TransitionToChaseFn();
        }
    }

    virtual void OnKilled(Engine::Scene& scene, const glm::vec3& origin) const {
        PickupComponent::SpawnPickup(scene, PickupKind::AshEmber, origin, { 0.8f, 0.5f }, 4.8f);
    }
};

class GhoulArchetype final : public IEnemyArchetype {
public:
    [[nodiscard]] const char* GetTexturePath() const noexcept override {
        return "assets/diorama/ghoul.png";
    }
};

class ThrallArchetype final : public IEnemyArchetype {
public:
    [[nodiscard]] const char* GetTexturePath() const noexcept override {
        return "assets/diorama/thrall.png";
    }

    [[nodiscard]] Engine::GameObject* SelectTarget(Engine::Scene& scene,
                                                   const glm::vec3& selfPos) const override {
        Engine::GameObject* closestStruct = nullptr;
        float bestDistSq = 4.5f * 4.5f;
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (!goPtr || goPtr->IsPendingDestroy()) {
                continue;
            }
            const auto tag = goPtr->GetTag();
            if (tag == Engine::EntityTag::TacticalStructure || tag == Engine::EntityTag::SafeZoneBrazier) {
                const glm::vec3 sPos = goPtr->GetTransform()->GetWorldPosition();
                const float dx = sPos.x - selfPos.x;
                const float dz = sPos.z - selfPos.z;
                const float dSq = dx * dx + dz * dz;
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    closestStruct = goPtr.get();
                }
            }
        }
        return closestStruct ? closestStruct : scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player);
    }
};

class JiangshiArchetype final : public IEnemyArchetype {
public:
    [[nodiscard]] const char* GetTexturePath() const noexcept override {
        return "assets/diorama/jiangshi.png";
    }

    [[nodiscard]] EnemyWindupSpec GetWindupSpec() const noexcept override {
        return { 0.35f, Engine::SpriteRenderer::FlashMode::PureWhite };
    }

    [[nodiscard]] bool ShouldStartSpecial(float dist, float specialCooldown) const noexcept override {
        return specialCooldown <= 0.0f && dist <= 5.5f && dist >= 1.8f;
    }

    void OnEnterAttack(EnemyAttackContext& ctx,
                       Engine::GameObject* target,
                       const glm::vec2& selfXZ,
                       const glm::vec2& targetXZ) const override {
        glm::vec2 targetVel{ 0.0f };
        if (target) {
            if (auto* tRb = target->GetComponent<Engine::Rigidbody2D>()) {
                targetVel = tRb->GetVelocityXZ();
            }
        }
        const float flightDur = 0.65f;
        const float gravityY = ctx.Rb ? ctx.Rb->GravityY : -22.0f;
        const auto sol = Engine::Ballistics::SolveInterceptHopArc(
            selfXZ, targetXZ, targetVel, ctx.HopRange, flightDur, gravityY, 0.75f);

        ctx.LandingSpotXZ = sol.LandingSpotXZ;
        ctx.IsAirborneHop = true;
        ctx.StateTimer = flightDur;
        ctx.SpecialCooldown = ctx.HopCooldown;
        if (ctx.Rb) {
            ctx.Rb->LaunchVertical(sol.InitialVelocityY);
            ctx.Rb->SetVelocityXZ(sol.VelocityXZ);
        }
        ctx.SpawnLandingFn(sol.LandingSpotXZ);
    }

    void OnUpdateAttack(EnemyAttackContext& ctx, float /*dt*/) const override {
        if (ctx.IsAirborneHop) {
            if (ctx.StateTimer <= 0.0f ||
                (ctx.Rb && ctx.Rb->IsGrounded(ctx.Self.GetTransform()->position.y) && ctx.StateTimer < 0.50f)) {
                const glm::vec3 center = ctx.Self.GetTransform()->GetWorldPosition();
                constexpr float kSlamRadiusSq = 1.8f * 1.8f;
                if (auto* player = ctx.Scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player)) {
                    const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
                    const float dx = pPos.x - center.x;
                    const float dz = pPos.z - center.z;
                    if ((dx * dx + dz * dz) <= kSlamRadiusSq) {
                        Sandbox::ApplyPlayerOrTargetDamage(player, &ctx.Self, ctx.Stats.Damage);
                    }
                }
                ctx.TransitionToChaseFn();
            }
            return;
        }
        if (ctx.StateTimer <= 0.0f) {
            ctx.TransitionToChaseFn();
        }
    }
};

class WispArchetype final : public IEnemyArchetype {
public:
    [[nodiscard]] const char* GetTexturePath() const noexcept override {
        return "assets/diorama/blight_wisp.png";
    }

    [[nodiscard]] EnemyWindupSpec GetWindupSpec() const noexcept override {
        return { 0.22f, Engine::SpriteRenderer::FlashMode::PureWhite };
    }

    [[nodiscard]] float GetOrbitRadius() const noexcept override { return 5.5f; }

    [[nodiscard]] bool ShouldStartSpecial(float dist, float specialCooldown) const noexcept override {
        return specialCooldown <= 0.0f && dist <= 7.0f;
    }

    void OnEnterAttack(EnemyAttackContext& ctx,
                       Engine::GameObject* target,
                       const glm::vec2& selfXZ,
                       const glm::vec2& targetXZ) const override {
        const glm::vec2 dir = Sandbox::SafeNormalizeXZ(targetXZ - selfXZ);
        ctx.StateTimer = 0.35f;
        ctx.SpecialCooldown = ctx.WispDartCooldown;
        if (ctx.Rb) {
            ctx.Rb->SetVelocityXZ(dir * (ctx.Stats.MoveSpeed * 1.85f));
        }
        ctx.ApplyHitFn(target, true, false);
    }
};

class WendigoArchetype final : public IEnemyArchetype {
public:
    [[nodiscard]] const char* GetTexturePath() const noexcept override {
        return "assets/diorama/wendigo.png";
    }

    [[nodiscard]] EnemyWindupSpec GetWindupSpec() const noexcept override {
        return { 0.45f, Engine::SpriteRenderer::FlashMode::CrimsonImpact };
    }

    [[nodiscard]] bool ShouldStartSpecial(float dist, float specialCooldown) const noexcept override {
        return specialCooldown <= 0.0f && dist <= 5.0f && dist >= 1.6f;
    }

    [[nodiscard]] glm::vec2 ModifyChaseTarget(Engine::Scene& scene,
                                              const glm::vec2& selfXZ,
                                              const glm::vec2& targetXZ,
                                              float dist) const override {
        if (dist <= 2.2f) {
            return targetXZ;
        }
        const auto illum = scene.EvaluateIlluminationAtXZ(selfXZ);
        if (!illum.IsInSafeLight(0.15f)) {
            return targetXZ;
        }
        const glm::vec2 toTarget = Sandbox::SafeNormalizeXZ(targetXZ - selfXZ);
        const glm::vec2 perp{ -toTarget.y, toTarget.x };
        const glm::vec2 probeA = selfXZ + perp * 3.0f;
        const glm::vec2 probeB = selfXZ - perp * 3.0f;
        const float lumA = scene.EvaluateIlluminationAtXZ(probeA).SafeIntensity;
        const float lumB = scene.EvaluateIlluminationAtXZ(probeB).SafeIntensity;
        return (lumA <= lumB) ? probeA : probeB;
    }

    void OnEnterAttack(EnemyAttackContext& ctx,
                       Engine::GameObject* target,
                       const glm::vec2& selfXZ,
                       const glm::vec2& targetXZ) const override {
        const glm::vec2 dir = Sandbox::SafeNormalizeXZ(targetXZ - selfXZ);
        ctx.StateTimer = 0.30f;
        ctx.SpecialCooldown = ctx.LungeCooldown;
        if (ctx.Rb) {
            ctx.Rb->SetVelocityXZ(dir * 14.0f);
        }
        ctx.ApplyHitFn(target, false, false);
    }

    void OnKilled(Engine::Scene& scene, const glm::vec3& origin) const override {
        for (int i = 0; i < 5; ++i) {
            const float angle = 1.256637f * static_cast<float>(i);
            PickupComponent::SpawnPickup(
                scene, PickupKind::AshEmber, origin, { std::cos(angle) * 2.4f, std::sin(angle) * 2.4f }, 5.5f);
        }
        PickupComponent::SpawnPickup(scene, PickupKind::CrimsonBerry, origin, { 0.0f, 1.8f }, 6.0f);
    }
};

class EnemyArchetypeRegistry {
public:
    [[nodiscard]] static const IEnemyArchetype& Resolve(std::string_view enemyId,
                                                        std::string_view archetypeTag = {}) {
        static const GhoulArchetype s_Ghoul;
        static const ThrallArchetype s_Thrall;
        static const JiangshiArchetype s_Jiangshi;
        static const WispArchetype s_Wisp;
        static const WendigoArchetype s_Wendigo;

        static const std::unordered_map<std::string_view, const IEnemyArchetype*> kById = {
            { "enemy_ghoul", &s_Ghoul },
            { "enemy_thrall", &s_Thrall },
            { "enemy_jiangshi", &s_Jiangshi },
            { "enemy_wisp", &s_Wisp },
            { "enemy_wendigo", &s_Wendigo },
        };
        if (const auto it = kById.find(enemyId); it != kById.end()) {
            return *it->second;
        }
        if (archetypeTag == "Jumper") return s_Jiangshi;
        if (archetypeTag == "EliteStalker") return s_Wendigo;
        return s_Ghoul;
    }
};

} // namespace Sandbox

#endif // SANDBOX_ENEMY_ARCHETYPES_H
