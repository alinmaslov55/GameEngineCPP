#ifndef SANDBOX_ENEMY_AI_CONTROLLER_H
#define SANDBOX_ENEMY_AI_CONTROLLER_H

#include "Scripts/CombatDamageHelper.h"
#include "Scripts/EnemyArchetypes.h"
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
        m_Archetype = &Sandbox::EnemyArchetypeRegistry::Resolve(enemyId, m_Stats.Archetype);

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
            const char* texPath = m_Stats.SpriteSheet.empty()
                ? m_Archetype->GetTexturePath()
                : m_Stats.SpriteSheet.c_str();
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
                const auto windup = self->ActiveArchetype().GetWindupSpec();
                self->m_StateTimer = windup.DurationSec;
                if (auto* go = self->GetGameObject()) {
                    if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                        sr->SetFlashMode(windup.Flash);
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

    [[nodiscard]] const Sandbox::IEnemyArchetype& ActiveArchetype() const noexcept {
        if (!m_Archetype) {
            return Sandbox::EnemyArchetypeRegistry::Resolve(EnemyId, m_Stats.Archetype);
        }
        return *m_Archetype;
    }

    void UpdateChase(float dt) {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* rb = go ? go->GetComponent<Engine::Rigidbody2D>() : nullptr;
        if (!go || !scene) {
            return;
        }

        const auto& archetype = ActiveArchetype();
        const glm::vec3 selfPos = go->GetTransform()->GetWorldPosition();
        m_Target = archetype.SelectTarget(*scene, selfPos);
        if (!m_Target || m_Target->IsPendingDestroy()) {
            if (rb) rb->SetVelocityXZ(0.0f, 0.0f);
            return;
        }

        const glm::vec3 tPos = m_Target->GetTransform()->GetWorldPosition();
        const glm::vec2 selfXZ{ selfPos.x, selfPos.z };
        glm::vec2 targetXZ{ tPos.x, tPos.z };
        const float dist = glm::length(targetXZ - selfXZ);

        if (archetype.ShouldStartSpecial(dist, m_SpecialCooldown) ||
            (dist <= AttackRange && m_AttackCooldown <= 0.0f)) {
            Transition(EnemyAIState::Windup);
            return;
        }

        targetXZ = archetype.ModifyChaseTarget(*scene, selfXZ, targetXZ, dist);

        m_NeighborScratch.clear();
        for (Engine::GameObject* other : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (other && other != go && !other->IsPendingDestroy()) {
                const glm::vec3 oPos = other->GetTransform()->GetWorldPosition();
                m_NeighborScratch.push_back({ oPos.x, oPos.z });
            }
        }

        Engine::SteeringParams sp;
        sp.MaxSpeed = m_Stats.MoveSpeed;
        sp.TargetOrbitRadius = archetype.GetOrbitRadius();
        const glm::vec2 velXZ = Engine::SteeringSystem::ComputeContextSteeringXZ(
            *scene, go, targetXZ, m_NeighborScratch, sp);

        if (rb) {
            rb->SetVelocityXZ(velXZ);
        } else if (std::isfinite(dt) && dt > 0.0f) {
            auto* transform = go->GetTransform();
            transform->position.x += velXZ.x * dt;
            transform->position.z += velXZ.y * dt;
            transform->SetDirty();
        }
        if (auto* sr = go->GetComponent<Engine::SpriteRenderer>(); sr && std::abs(velXZ.x) > 0.05f) {
            sr->SetFlipX(velXZ.x < 0.0f);
        }
    }

    Sandbox::EnemyAttackContext BuildAttackContext(Engine::GameObject& go, Engine::Scene& scene) {
        return Sandbox::EnemyAttackContext{
            go,
            scene,
            go.GetComponent<Engine::Rigidbody2D>(),
            m_Stats,
            AttackRange,
            HopRange,
            LungeRange,
            HopCooldown,
            LungeCooldown,
            WispDartCooldown,
            m_StateTimer,
            m_AttackCooldown,
            m_SpecialCooldown,
            m_IsAirborneHop,
            m_LandingSpotXZ,
            [this](Engine::GameObject* target, bool siphonFuel, bool checkMeleeRange) {
                ApplyAttackHit(target, siphonFuel, checkMeleeRange);
            },
            [this, &scene](const glm::vec2& spotXZ) {
                SpawnLandingIndicator(scene, spotXZ);
            },
            [this]() {
                Transition(EnemyAIState::Chase);
            }
        };
    }

    void EnterAttack() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            Transition(EnemyAIState::Chase);
            return;
        }

        const auto& archetype = ActiveArchetype();
        const glm::vec3 selfPos = go->GetTransform()->GetWorldPosition();
        m_Target = archetype.SelectTarget(*scene, selfPos);
        const glm::vec2 selfXZ{ selfPos.x, selfPos.z };
        const glm::vec2 targetXZ = (m_Target && !m_Target->IsPendingDestroy())
            ? glm::vec2(m_Target->GetTransform()->GetWorldPosition().x, m_Target->GetTransform()->GetWorldPosition().z)
            : (selfXZ + glm::vec2(1.0f, 0.0f));

        auto ctx = BuildAttackContext(*go, *scene);
        archetype.OnEnterAttack(ctx, m_Target, selfXZ, targetXZ);
    }

    void UpdateAttack(float dt) {
        m_StateTimer -= dt;
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            Transition(EnemyAIState::Chase);
            return;
        }
        auto ctx = BuildAttackContext(*go, *scene);
        ActiveArchetype().OnUpdateAttack(ctx, dt);
    }

    void ApplyAttackHit(Engine::GameObject* target, bool siphonFuel, bool checkMeleeRange = true) {
        auto* go = GetGameObject();
        if (!go || !target || target->IsPendingDestroy()) {
            return;
        }
        if (checkMeleeRange) {
            const glm::vec3 sPos = go->GetTransform()->GetWorldPosition();
            const glm::vec3 tPos = target->GetTransform()->GetWorldPosition();
            const float dist = glm::length(glm::vec2{ tPos.x - sPos.x, tPos.z - sPos.z });
            if (dist > (AttackRange + 0.6f)) {
                return;
            }
        }

        if (target->GetTag() == Engine::EntityTag::Player) {
            Sandbox::ApplyPlayerOrTargetDamage(target, go, m_Stats.Damage);
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
        ActiveArchetype().OnKilled(*scene, go->GetTransform()->GetWorldPosition());
    }

    Engine::EnemyDefinition m_Stats{ "enemy_ghoul", "Flesheater Ghoul", "Swarm", 35.0f, 3.8f, 8.0f, 25.0f, 1.2f, "" };
    const Sandbox::IEnemyArchetype* m_Archetype = nullptr;
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
