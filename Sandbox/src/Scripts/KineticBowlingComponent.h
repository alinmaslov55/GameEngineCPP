#ifndef SANDBOX_KINETIC_BOWLING_COMPONENT_H
#define SANDBOX_KINETIC_BOWLING_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/SplinterProjectileComponent.h"

#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/Time.h>
#include <Engine/Physics/Collision2DInfo.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/CameraComponent.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

class KineticBowlingComponent final : public Engine::Component {
public:
    float LaunchSpeedThreshold = 5.5f;
    float MinApproachSpeed = 2.0f;
    float CollateralDamageFactor = 0.5f;
    float WallSlamBaseDamage = 20.0f;
    float WallSlamStunDuration = 0.6f;
    float BarricadeStructureDamage = 35.0f;
    int BarricadeSplinterCount = 6;

    void Launch(float durationSec = 0.6f) noexcept {
        if (std::isfinite(durationSec) && durationSec > 0.0f) {
            m_LaunchedTimer = std::max(m_LaunchedTimer, durationSec);
        }
    }

    [[nodiscard]] bool IsLaunched() const noexcept {
        if (m_LaunchedTimer > 0.0f) {
            return true;
        }
        if (auto* go = GetGameObject()) {
            if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
                return glm::length(rb->GetVelocityXZ()) > LaunchSpeedThreshold;
            }
        }
        return false;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (m_LaunchedTimer > 0.0f) {
            m_LaunchedTimer = std::max(0.0f, m_LaunchedTimer - deltaTime);
        }
        for (auto it = m_HitCooldowns.begin(); it != m_HitCooldowns.end(); ) {
            it->second -= deltaTime;
            if (it->second <= 0.0f) {
                it = m_HitCooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        auto* self = GetGameObject();
        auto* other = info.Other;
        if (!self || !other || other == self || self->IsPendingDestroy() || other->IsPendingDestroy()) {
            return;
        }
        if (!IsLaunched() || !std::isfinite(info.ApproachSpeed) || info.ApproachSpeed < MinApproachSpeed) {
            return;
        }

        const uint64_t otherUUID = other->GetUUID();
        if (m_HitCooldowns.contains(otherUUID)) {
            return;
        }
        m_HitCooldowns[otherUUID] = 0.15f;

        auto* selfRb = self->GetComponent<Engine::Rigidbody2D>();
        const float selfMass = selfRb ? std::max(1.0f, selfRb->GetMass()) : 25.0f;
        const Engine::EntityTag otherTag = other->GetTag();

        if (otherTag == Engine::EntityTag::Enemy) {
            HandleEnemyDomino(self, other, selfMass, info);
        } else if (otherTag == Engine::EntityTag::TacticalStructure ||
                   other->GetName().find("Barricade") != std::string::npos) {
            HandleBarricadeImpact(self, other, info);
        } else if (otherTag == Engine::EntityTag::HarvestableTree ||
                   otherTag == Engine::EntityTag::Shrine ||
                   otherTag == Engine::EntityTag::DestructibleProp ||
                   (other->GetLayer() & Engine::EntityLayer::Environment) != 0u) {
            HandleWallSlam(self, other, info);
        }
    }

private:
    void TriggerPlayerMicroFeedback(Engine::Scene* scene, float trauma01, const glm::vec2& kickXZ) {
        if (!scene) return;
        if (auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player)) {
            if (auto* cam = player->GetComponent<Engine::CameraComponent>()) {
                cam->AddTrauma(trauma01, kickXZ);
            }
        }
    }

    void HandleEnemyDomino(Engine::GameObject* self,
                           Engine::GameObject* other,
                           float selfMass,
                           const Engine::Collision2DInfo& info) {
        const float collateralDamage = CollateralDamageFactor * selfMass * info.ApproachSpeed;

        Sandbox::ApplyImpactDamage(self, other, collateralDamage, 0.25f);
        const glm::vec2 pushDir = Sandbox::SafeNormalizeXZ(info.NormalXZ);
        Sandbox::ApplyImpactDamage(other, self, collateralDamage, 0.35f, pushDir * (info.ApproachSpeed * 1.35f));
        if (auto* otherBowling = other->GetComponent<KineticBowlingComponent>()) {
            otherBowling->Launch(0.45f);
        }

        if (auto* audio = self->GetComponent<Engine::AudioSource>()) {
            audio->Play();
        } else {
            Engine::SoundPlayParams crunchParams;
            crunchParams.Bus = Engine::AudioBus::SFX;
            crunchParams.Volume = 0.95f;
            crunchParams.SpatializedXZ = true;
            crunchParams.WorldPos = self->GetTransform()->GetWorldPosition();
            Sandbox::PlaySfxCue("assets/audio/sfx_domino_crunch.wav", crunchParams);
        }
        Engine::Time::TriggerHitStop(0.025f, 0.04f);
        TriggerPlayerMicroFeedback(self->GetScene(), 0.18f, info.NormalXZ * 0.18f);
    }

    void HandleWallSlam(Engine::GameObject* self,
                        Engine::GameObject* other,
                        const Engine::Collision2DInfo& info) {
        const float wallDamage = WallSlamBaseDamage + 1.5f * info.ApproachSpeed;
        Sandbox::ApplyImpactDamage(self, other, wallDamage, WallSlamStunDuration, { 0.0f, 0.0f }, false);
        Sandbox::ApplyImpactDamage(other, self, wallDamage * 0.5f, 0.0f, { 0.0f, 0.0f }, false);
        if (auto* otherSS = other->GetComponent<Engine::SquashStretchComponent>()) {
            otherSS->TriggerImpulse(-2.8f, 12.0f);
        }
        Engine::Time::TriggerHitStop(0.020f, 0.05f);
        TriggerPlayerMicroFeedback(self->GetScene(), 0.15f, -info.NormalXZ * 0.12f);
    }

    void HandleBarricadeImpact(Engine::GameObject* self,
                               Engine::GameObject* barricade,
                               const Engine::Collision2DInfo& info) {
        const float structDamage = BarricadeStructureDamage + 2.5f * info.ApproachSpeed;
        auto* barricadeHealth = barricade->GetComponent<HealthComponent>();
        if (!barricadeHealth) {
            return;
        }

        const bool wasAlive = barricadeHealth->IsAlive();
        const glm::vec2 impactDir = Sandbox::SafeNormalizeXZ(-info.NormalXZ);
        DamageEvent evt;
        evt.Amount = structDamage;
        evt.Source = self;
        evt.ImpulseXZ = impactDir;
        barricadeHealth->ApplyDamage(evt);
        if (wasAlive && !barricadeHealth->IsAlive()) {
            if (!barricadeHealth->OnKilled) {
                SpawnBarricadeSplinters(barricade, info);
            }
            if (auto* scene = barricade->GetScene()) {
                scene->DestroyGameObject(barricade);
            }
        }

        Sandbox::ApplyImpactDamage(self, barricade, 15.0f, 0.35f, { 0.0f, 0.0f }, false);
        Engine::Time::TriggerHitStop(0.030f, 0.04f);
        TriggerPlayerMicroFeedback(self->GetScene(), 0.25f, info.NormalXZ * 0.22f);
    }

    void SpawnBarricadeSplinters(Engine::GameObject* barricade, const Engine::Collision2DInfo& info) {
        auto* scene = barricade ? barricade->GetScene() : nullptr;
        auto* bTransform = barricade ? barricade->GetTransform() : nullptr;
        if (!scene || !bTransform) {
            return;
        }
        SplinterProjectileComponent::SpawnBurst(
            *scene,
            bTransform->GetWorldPosition(),
            -info.NormalXZ,
            BarricadeSplinterCount,
            0.610865f);
    }

    float m_LaunchedTimer = 0.0f;
    std::unordered_map<uint64_t, float> m_HitCooldowns;
};

#endif // SANDBOX_KINETIC_BOWLING_COMPONENT_H
