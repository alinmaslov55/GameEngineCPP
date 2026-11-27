#ifndef SANDBOX_ENEMY_AI_CONTROLLER_H
#define SANDBOX_ENEMY_AI_CONTROLLER_H

#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PickupComponent.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/StatusEffectsComponent.h"
#include "Scripts/TorchComponent.h"

#include <Engine/AI/Ballistics.h>
#include <Engine/AI/StateMachine.h>
#include <Engine/AI/SteeringSystem.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

enum class EnemyAIState : int {
    Chase = 0,
    Windup = 1,
    Attack = 2,
    Stunned = 3,
    Dying = 4
};

class EnemyAIController final : public Engine::Component {
public:
    std::string EnemyId = "enemy_ghoul";
    float AttackRange = 1.45f;
    float HopRange = 5.5f;
    float LungeRange = 5.0f;
    float WispDartCooldown = 3.2f;
    float HopCooldown = 2.8f;
    float LungeCooldown = 3.5f;

    void Awake() override {
        ConfigureArchetype(EnemyId);
        RegisterStates();
        Transition(EnemyAIState::Chase);
    }

    void Start() override {
        if (EnemyId != m_Stats.Id) {
            ConfigureArchetype(EnemyId);
        }
    }

    void OnPoolAcquire() override {
        m_StateTimer = 0.0f;
        m_AttackCooldown = 0.0f;
        m_SpecialCooldown = 0.5f;
        m_IsAirborneHop = false;
        m_JackpotDropped = false;
        CleanupLandingIndicator();
        ConfigureArchetype(EnemyId);
        Transition(EnemyAIState::Chase);
    }

    void OnPoolRelease() override {
        CleanupLandingIndicator();
    }

    void OnDestroy() override {
        CleanupLandingIndicator();
    }

    bool ConfigureArchetype(const std::string& enemyId) {
        EnemyId = enemyId;
        m_Stats = ResolveEnemyDefinition(enemyId);

        auto* go = GetGameObject();
        if (!go) {
            return true;
        }
        go->SetTag(Engine::EntityTag::Enemy);
        go->SetLayer(Engine::EntityLayer::Enemy);

        if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
            rb->SetBaseMass(std::max(1.0f, m_Stats.Mass));
        }

        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
            const char* texPath = "assets/diorama/ghoul.png";
            if (enemyId == "enemy_thrall") texPath = "assets/diorama/thrall.png";
            else if (enemyId == "enemy_jiangshi") texPath = "assets/diorama/jiangshi.png";
            else if (enemyId == "enemy_wendigo") texPath = "assets/diorama/wendigo.png";
            else if (enemyId == "enemy_wisp") texPath = "assets/diorama/blight_wisp.png";
            if (auto tex = Engine::ResourceManager::Load<Engine::Texture>(texPath)) {
                sr->SetTexture(tex);
                sr->SetAlphaCutoff(0.18f);
            }
        }

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(m_Stats.Health);
        health->LightVulnerability = m_Stats.LightVulnerability;
        health->DissolveDuration = 0.45f;
        health->OnKilled = [this](const DamageEvent&) {
            OnEnemyKilled();
        };

        if (!go->GetComponent<StatusEffectsComponent>()) {
            go->AddComponent<StatusEffectsComponent>();
        }
        if (!go->GetComponent<KineticBowlingComponent>()) {
            go->AddComponent<KineticBowlingComponent>();
        }
        return true;
    }

    [[nodiscard]] const Engine::EnemyDefinition& GetStats() const noexcept { return m_Stats; }
    [[nodiscard]] EnemyAIState GetCurrentState() const noexcept {
        return static_cast<EnemyAIState>(m_StateMachine.GetCurrentState());
    }
    [[nodiscard]] bool HasLandingIndicator() const noexcept { return m_LandingIndicator != nullptr; }
    [[nodiscard]] const glm::vec2& GetLastLandingSpotXZ() const noexcept { return m_LandingSpotXZ; }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }

        auto* go = GetGameObject();
        if (!go) {
            return;
        }

        if (m_AttackCooldown > 0.0f) {
            m_AttackCooldown = std::max(0.0f, m_AttackCooldown - deltaTime);
        }
        if (m_SpecialCooldown > 0.0f) {
            m_SpecialCooldown = std::max(0.0f, m_SpecialCooldown - deltaTime);
        }

        if (auto* health = go->GetComponent<HealthComponent>(); health && health->IsDying()) {
            if (GetCurrentState() != EnemyAIState::Dying) {
                Transition(EnemyAIState::Dying);
            }
            EnemyAIController* ctx = this;
            m_StateMachine.Update(ctx, deltaTime);
            return;
        }

        const bool stunned = IsExternallyStunned(*go);
        if (stunned && GetCurrentState() != EnemyAIState::Stunned) {
            Transition(EnemyAIState::Stunned);
        } else if (!stunned && GetCurrentState() == EnemyAIState::Stunned) {
            Transition(EnemyAIState::Chase);
        }

        EnemyAIController* ctx = this;
        m_StateMachine.Update(ctx, deltaTime);
    }

private:
    void Transition(EnemyAIState next) {
        EnemyAIController* ctx = this;
        m_StateMachine.TransitionTo(next, ctx);
    }
    static Engine::EnemyDefinition ResolveEnemyDefinition(const std::string& id) {
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Enemies().Find(id)) {
                return *def;
            }
        }
        if (id == "enemy_thrall") {
            return { "enemy_thrall", "Rotting Thrall", "Swarm", 50.0f, 2.6f, 14.0f, 45.0f, 1.0f, "" };
        }
        if (id == "enemy_jiangshi") {
            return { "enemy_jiangshi", "Hopping Jiangshi", "Jumper", 65.0f, 4.2f, 18.0f, 35.0f, 1.5f, "" };
        }
        if (id == "enemy_wisp") {
            return { "enemy_wisp", "Blight Wisp", "Swarm", 20.0f, 5.0f, 12.0f, 10.0f, 2.0f, "" };
        }
        if (id == "enemy_wendigo") {
            return { "enemy_wendigo", "Apex Wendigo", "EliteStalker", 180.0f, 4.8f, 32.0f, 90.0f, 0.8f, "" };
        }
        return { "enemy_ghoul", "Flesheater Ghoul", "Swarm", 35.0f, 3.8f, 8.0f, 25.0f, 1.2f, "" };
    }

    static bool IsExternallyStunned(Engine::GameObject& go) noexcept {
        if (auto* status = go.GetComponent<StatusEffectsComponent>(); status && status->IsStunned()) {
            return true;
        }
        if (auto* bowling = go.GetComponent<KineticBowlingComponent>(); bowling && bowling->IsLaunched()) {
            return true;
        }
        return false;
    }

    void RegisterStates() {
        if (m_StatesRegistered) {
            return;
        }
        m_StatesRegistered = true;

        m_StateMachine.RegisterState(EnemyAIState::Chase, {
            .OnEnter = [](EnemyAIController* self) {
                self->m_StateTimer = 0.0f;
                self->m_IsAirborneHop = false;
            },
            .OnUpdate = [](EnemyAIController* self, float dt) {
                self->UpdateChase(dt);
            },
            .OnExit = nullptr
        });

        m_StateMachine.RegisterState(EnemyAIState::Windup, {
            .OnEnter = [](EnemyAIController* self) {
                self->m_StateTimer = self->ResolveWindupDuration();
                if (auto* go = self->GetGameObject()) {
                    if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                        sr->SetFlashMode(self->EnemyId == "enemy_wendigo"
                            ? Engine::SpriteRenderer::FlashMode::CrimsonImpact
                            : Engine::SpriteRenderer::FlashMode::PureWhite);
                        sr->SetFlashAmount(0.85f);
                    }
                    if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                        ss->TriggerImpulse(-2.8f, 10.0f);
                    }
                    if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
                        rb->SetVelocityXZ(0.0f, 0.0f);
                    }
                }
            },
            .OnUpdate = [](EnemyAIController* self, float dt) {
                self->m_StateTimer -= dt;
                if (self->m_StateTimer <= 0.0f) {
                    if (auto* go = self->GetGameObject()) {
                        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                            sr->SetFlashAmount(0.0f);
                        }
                    }
                    self->Transition(EnemyAIState::Attack);
                }
            },
            .OnExit = nullptr
        });

        m_StateMachine.RegisterState(EnemyAIState::Attack, {
            .OnEnter = [](EnemyAIController* self) {
                self->EnterAttack();
            },
            .OnUpdate = [](EnemyAIController* self, float dt) {
                self->UpdateAttack(dt);
            },
            .OnExit = [](EnemyAIController* self) {
                self->CleanupLandingIndicator();
                self->m_IsAirborneHop = false;
            }
        });

        m_StateMachine.RegisterState(EnemyAIState::Stunned, {
            .OnEnter = [](EnemyAIController* self) {
                self->CleanupLandingIndicator();
                self->m_IsAirborneHop = false;
            },
            .OnUpdate = [](EnemyAIController*, float) {
                // Intentionally yields velocity control to Box2D physics during knockback / bowling
            },
            .OnExit = nullptr
        });

        m_StateMachine.RegisterState(EnemyAIState::Dying, {
            .OnEnter = [](EnemyAIController* self) {
                self->CleanupLandingIndicator();
                if (auto* go = self->GetGameObject()) {
                    if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
                        rb->SetVelocityXZ(0.0f, 0.0f);
                    }
                }
            },
            .OnUpdate = [](EnemyAIController*, float) {},
            .OnExit = nullptr
        });
    }

    [[nodiscard]] float ResolveWindupDuration() const noexcept {
        if (EnemyId == "enemy_jiangshi") return 0.35f;
        if (EnemyId == "enemy_wendigo") return 0.45f;
        if (EnemyId == "enemy_wisp") return 0.22f;
        return 0.25f;
    }

    [[nodiscard]] Engine::GameObject* SelectTarget(Engine::Scene& scene, const glm::vec3& selfPos) const {
        Engine::GameObject* player = scene.FindFirstGameObjectWithTag(Engine::EntityTag::Player);
        if (EnemyId == "enemy_thrall") {
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
            if (closestStruct) {
                return closestStruct;
            }
        }
        return player;
    }

    void UpdateChase(float /*dt*/) {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        if (!go || !scene) {
            return;
        }

        const glm::vec3 selfPos = go->GetTransform()->GetWorldPosition();
        m_Target = SelectTarget(*scene, selfPos);
        if (!m_Target || m_Target->IsPendingDestroy()) {
            if (rb) rb->SetVelocityXZ(0.0f, 0.0f);
            return;
        }

        const glm::vec3 tPos = m_Target->GetTransform()->GetWorldPosition();
        const glm::vec2 selfXZ{ selfPos.x, selfPos.z };
        glm::vec2 targetXZ{ tPos.x, tPos.z };
        const float dist = glm::length(targetXZ - selfXZ);

        // Archetype special transitions
        if (EnemyId == "enemy_jiangshi" && m_SpecialCooldown <= 0.0f && dist <= HopRange && dist >= 1.8f) {
            Transition(EnemyAIState::Windup);
            return;
        }
        if (EnemyId == "enemy_wisp" && m_SpecialCooldown <= 0.0f && dist <= 7.0f) {
            Transition(EnemyAIState::Windup);
            return;
        }
        if (EnemyId == "enemy_wendigo" && m_SpecialCooldown <= 0.0f && dist <= LungeRange && dist >= 1.6f) {
            Transition(EnemyAIState::Windup);
            return;
        }
        if (dist <= AttackRange && m_AttackCooldown <= 0.0f) {
            Transition(EnemyAIState::Windup);
            return;
        }

        // Apex Wendigo shadow-stalking via Scene::EvaluateIlluminationAtXZ (LGT-03)
        if (EnemyId == "enemy_wendigo" && dist > 2.2f) {
            const auto illum = scene->EvaluateIlluminationAtXZ(selfXZ);
            if (illum.IsInSafeLight(0.15f)) {
                // Sidestep toward darker flank perpendicular to target
                const glm::vec2 toTarget = (dist > 1e-4f) ? ((targetXZ - selfXZ) / dist) : glm::vec2(1.0f, 0.0f);
                const glm::vec2 perp{ -toTarget.y, toTarget.x };
                const glm::vec2 probeA = selfXZ + perp * 3.0f;
                const glm::vec2 probeB = selfXZ - perp * 3.0f;
                const float lumA = scene->EvaluateIlluminationAtXZ(probeA).SafeIntensity;
                const float lumB = scene->EvaluateIlluminationAtXZ(probeB).SafeIntensity;
                targetXZ = (lumA <= lumB) ? probeA : probeB;
            }
        }

        m_NeighborScratch.clear();
        for (Engine::GameObject* other : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (other && other != go && !other->IsPendingDestroy()) {
                const glm::vec3 oPos = other->GetTransform()->GetWorldPosition();
                m_NeighborScratch.push_back({ oPos.x, oPos.z });
            }
        }

        Engine::SteeringParams sp;
        sp.MaxSpeed = m_Stats.MoveSpeed;
        sp.TargetOrbitRadius = (EnemyId == "enemy_wisp") ? 5.5f : 0.0f;
        const glm::vec2 velXZ = Engine::SteeringSystem::ComputeContextSteeringXZ(
            *scene, go, targetXZ, m_NeighborScratch, sp);

        if (rb) {
            rb->SetVelocityXZ(velXZ);
        } else {
            go->GetTransform()->position.x += velXZ.x * (1.0f / 60.0f);
            go->GetTransform()->position.z += velXZ.y * (1.0f / 60.0f);
        }
        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>(); sr && std::abs(velXZ.x) > 0.05f) {
            sr->SetFlipX(velXZ.x < 0.0f);
        }
    }

    void EnterAttack() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        if (!go || !scene) {
            Transition(EnemyAIState::Chase);
            return;
        }

        const glm::vec3 selfPos = go->GetTransform()->GetWorldPosition();
        m_Target = SelectTarget(*scene, selfPos);
        const glm::vec2 selfXZ{ selfPos.x, selfPos.z };
        const glm::vec2 targetXZ = (m_Target && !m_Target->IsPendingDestroy())
            ? glm::vec2(m_Target->GetTransform()->GetWorldPosition().x, m_Target->GetTransform()->GetWorldPosition().z)
            : (selfXZ + glm::vec2(1.0f, 0.0f));

        if (EnemyId == "enemy_jiangshi") {
            glm::vec2 targetVel{ 0.0f };
            if (m_Target) {
                if (auto* tRb = m_Target->GetComponent<Engine::Rigidbody2D>()) {
                    targetVel = tRb->GetVelocityXZ();
                }
            }
            const float flightDur = 0.65f;
            const float gravityY = rb ? rb->GravityY : -22.0f;
            const auto sol = Engine::Ballistics::SolveInterceptHopArc(
                selfXZ, targetXZ, targetVel, HopRange, flightDur, gravityY, 0.75f);

            m_LandingSpotXZ = sol.LandingSpotXZ;
            m_IsAirborneHop = true;
            m_StateTimer = flightDur;
            m_SpecialCooldown = HopCooldown;
            if (rb) {
                rb->LaunchVertical(sol.InitialVelocityY);
                rb->SetVelocityXZ(sol.VelocityXZ);
            }
            SpawnLandingIndicator(*scene, sol.LandingSpotXZ);
            return;
        }

        if (EnemyId == "enemy_wisp") {
            const glm::vec2 dir = glm::normalize(targetXZ - selfXZ + glm::vec2(1e-5f, 0.0f));
            m_StateTimer = 0.35f;
            m_SpecialCooldown = WispDartCooldown;
            if (rb) {
                rb->SetVelocityXZ(dir * (m_Stats.MoveSpeed * 1.85f));
            }
            ApplyAttackHit(m_Target, true);
            return;
        }

        if (EnemyId == "enemy_wendigo") {
            const glm::vec2 dir = glm::normalize(targetXZ - selfXZ + glm::vec2(1e-5f, 0.0f));
            m_StateTimer = 0.30f;
            m_SpecialCooldown = LungeCooldown;
            if (rb) {
                rb->SetVelocityXZ(dir * 14.0f);
            }
            ApplyAttackHit(m_Target, false);
            return;
        }

        // Standard Ghoul / Thrall melee strike
        m_StateTimer = 0.20f;
        m_AttackCooldown = 1.0f;
        ApplyAttackHit(m_Target, false);
    }

    void UpdateAttack(float dt) {
        m_StateTimer -= dt;
        if (m_IsAirborneHop) {
            auto* go = GetGameObject();
            auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
            if (m_StateTimer <= 0.0f || (rb && rb->IsGrounded(go->GetTransform()->position.y) && m_StateTimer < 0.50f)) {
                ExecuteJiangshiLandingSlam();
                Transition(EnemyAIState::Chase);
            }
            return;
        }
        if (m_StateTimer <= 0.0f) {
            Transition(EnemyAIState::Chase);
        }
    }

    void ApplyAttackHit(Engine::GameObject* target, bool siphonFuel) {
        auto* go = GetGameObject();
        if (!go || !target || target->IsPendingDestroy()) {
            return;
        }
        const glm::vec3 sPos = go->GetTransform()->GetWorldPosition();
        const glm::vec3 tPos = target->GetTransform()->GetWorldPosition();
        const glm::vec2 delta{ tPos.x - sPos.x, tPos.z - sPos.z };
        const float dist = glm::length(delta);
        if (dist > (AttackRange + 0.6f) && EnemyId != "enemy_wisp" && EnemyId != "enemy_wendigo") {
            return;
        }

        if (target->GetTag() == Engine::EntityTag::Player) {
            if (auto* stats = target->GetComponent<PlayerStatsComponent>()) {
                stats->TakeDamage(m_Stats.Damage);
            }
            if (siphonFuel) {
                for (Engine::Transform* child : target->GetTransform()->GetChildren()) {
                    if (child && child->GetGameObject()) {
                        if (auto* torch = child->GetGameObject()->GetComponent<TorchComponent>()) {
                            torch->Fuel = std::max(0.0f, torch->Fuel - 8.0f);
                            break;
                        }
                    }
                }
            }
        } else if (auto* targetHealth = target->GetComponent<HealthComponent>()) {
            DamageEvent evt;
            evt.Amount = m_Stats.Damage;
            evt.Source = go;
            targetHealth->ApplyDamage(evt);
        }
    }

    void ExecuteJiangshiLandingSlam() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }
        const glm::vec3 center = go->GetTransform()->GetWorldPosition();
        constexpr float kSlamRadius = 1.8f;
        const float rSq = kSlamRadius * kSlamRadius;

        if (auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player)) {
            const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
            const float dx = pPos.x - center.x;
            const float dz = pPos.z - center.z;
            if ((dx * dx + dz * dz) <= rSq) {
                if (auto* stats = player->GetComponent<PlayerStatsComponent>()) {
                    stats->TakeDamage(m_Stats.Damage);
                }
            }
        }
    }

    void SpawnLandingIndicator(Engine::Scene& scene, const glm::vec2& spotXZ) {
        CleanupLandingIndicator();
        m_LandingIndicator = scene.CreateGameObject("JiangshiLandingRing");
        m_LandingIndicator->GetTransform()->position = { spotXZ.x, 0.02f, spotXZ.y };
        m_LandingIndicator->GetTransform()->scale = { 3.6f, 3.6f, 1.0f };
        auto* sr = m_LandingIndicator->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
        sr->SetColor({ 0.95f, 0.20f, 0.18f, 0.55f });
        sr->SetEmissive(0.75f);
    }

    void CleanupLandingIndicator() {
        if (m_LandingIndicator) {
            if (auto* scene = m_LandingIndicator->GetScene()) {
                scene->DestroyGameObject(m_LandingIndicator);
            }
            m_LandingIndicator = nullptr;
        }
    }

    void OnEnemyKilled() {
        if (m_JackpotDropped) {
            return;
        }
        m_JackpotDropped = true;
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }
        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        if (EnemyId == "enemy_wendigo") {
            // Guaranteed Ash Jackpot + Crimson Berry
            for (int i = 0; i < 5; ++i) {
                const float angle = 1.256637f * static_cast<float>(i);
                PickupComponent::SpawnPickup(
                    *scene, PickupKind::AshEmber, origin, { std::cos(angle) * 2.4f, std::sin(angle) * 2.4f }, 5.5f);
            }
            PickupComponent::SpawnPickup(*scene, PickupKind::CrimsonBerry, origin, { 0.0f, 1.8f }, 6.0f);
        } else {
            PickupComponent::SpawnPickup(*scene, PickupKind::AshEmber, origin, { 0.8f, 0.5f }, 4.8f);
        }
    }

    Engine::EnemyDefinition m_Stats{ "enemy_ghoul", "Flesheater Ghoul", "Swarm", 35.0f, 3.8f, 8.0f, 25.0f, 1.2f, "" };
    Engine::StateMachine<EnemyAIController*> m_StateMachine;
    bool m_StatesRegistered = false;
    Engine::GameObject* m_Target = nullptr;
    Engine::GameObject* m_LandingIndicator = nullptr;
    glm::vec2 m_LandingSpotXZ{ 0.0f };
    bool m_IsAirborneHop = false;
    bool m_JackpotDropped = false;
    float m_StateTimer = 0.0f;
    float m_AttackCooldown = 0.0f;
    float m_SpecialCooldown = 0.5f;
    std::vector<glm::vec2> m_NeighborScratch;
};

#endif // SANDBOX_ENEMY_AI_CONTROLLER_H
