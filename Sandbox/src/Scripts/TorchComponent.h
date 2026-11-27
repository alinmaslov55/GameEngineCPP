#ifndef SANDBOX_TORCH_COMPONENT_H
#define SANDBOX_TORCH_COMPONENT_H

#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Audio/MusicDirector.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/Time.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/Components/PostProcessComponent.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

class TorchComponent final : public Engine::Component {
public:
    // Fuel & burn parameters
    float MaxFuel = 100.0f;
    float Fuel = 100.0f;
    float BaseBurnRate = 2.2f;       // Fuel units per second
    float BurnRateMultiplier = 1.0f; // Modified by Slow Burn Root

    // Dynamic light curve
    float MinRadius = 2.5f;
    float MaxRadius = 9.5f;
    float DreadRadius = 1.8f;
    float MinIntensity = 0.6f;
    float MaxIntensity = 2.2f;
    float DreadIntensity = 0.25f;

    // Searing Rim (outer 25% of torch radius)
    float AuraDamage = 12.0f; // Radiant DPS before LightVulnerability
    float SearingTickInterval = 0.25f;

    // Stoke Torch
    int StokeWoodCost = 2;
    float StokeFuelRestore = 25.0f;
    float StokeIgniteRadius = 2.2f;
    float DashIgniteRadius = 1.2f;
    std::shared_ptr<Engine::AudioClip> StokeClip;

    // Ash Frenzy (Fuel <= 20%)
    float AshFrenzyThreshold = 0.20f;
    float AshFrenzySpeedBonus = 0.20f; // +20% move & attack speed

    // Darkness Dread (Fuel <= 0%)
    float DreadDamagePerSec = 6.0f;

    void Awake() override {
        Engine::Input::BindAction("StokeTorch", {
            .PrimaryKey = SDL_SCANCODE_E,
            .SecondaryKey = SDL_SCANCODE_F,
            .MouseButton = 0,
            .PadButton = SDL_GAMEPAD_BUTTON_WEST,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID
        });
    }

    [[nodiscard]] float GetFuel01() const noexcept {
        return (MaxFuel > 1e-4f) ? std::clamp(Fuel / MaxFuel, 0.0f, 1.0f) : 0.0f;
    }

    [[nodiscard]] bool IsAshFrenzy() const noexcept {
        return Fuel > 0.0f && GetFuel01() <= AshFrenzyThreshold;
    }

    [[nodiscard]] bool IsDarknessDread() const noexcept {
        return Fuel <= 0.0f;
    }

    [[nodiscard]] float GetCurrentRadius() const noexcept {
        return m_CurrentRadius;
    }

    bool StokeTorch() {
        auto* player = ResolvePlayer();
        auto* stats = player ? player->GetComponent<PlayerStatsComponent>() : nullptr;
        if (!stats || stats->Wood < StokeWoodCost) {
            return false;
        }

        stats->Wood -= StokeWoodCost;
        Fuel = std::min(MaxFuel, Fuel + StokeFuelRestore);
        m_FlarePulse = 1.0f;

        if (auto* go = GetGameObject()) {
            if (auto* scene = go->GetScene()) {
                const glm::vec3 pos = go->GetTransform()->GetWorldPosition();
                scene->IgniteAtXZ({ pos.x, pos.z }, StokeIgniteRadius);
            }
            if (auto* ps = (player ? player->GetComponent<Engine::ParticleSystem>() : nullptr)) {
                Engine::ParticleProps flare;
                flare.position = go->GetTransform()->GetWorldPosition() + glm::vec3(0.0f, 0.6f, 0.0f);
                flare.colorBegin = { 1.0f, 0.78f, 0.30f, 1.0f };
                flare.colorEnd = { 1.0f, 0.35f, 0.08f, 0.0f };
                flare.sizeBegin = 0.22f;
                flare.sizeEnd = 0.05f;
                flare.lifeTime = 0.45f;
                flare.velocity = { 0.0f, 2.8f, 0.0f };
                flare.velocityVariation = { 2.4f, 1.4f, 2.4f };
                ps->EmitBurst(flare, 12u);
            }
            if (StokeClip && Engine::AudioManager::IsInitialized()) {
                Engine::SoundPlayParams params;
                params.Bus = Engine::AudioBus::SFX;
                params.PitchMin = 0.96f;
                params.PitchMax = 1.06f;
                params.SpatializedXZ = true;
                params.WorldPos = go->GetTransform()->GetWorldPosition();
                Engine::AudioManager::PlayOneShot(StokeClip, params);
            }
        }
        return true;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }

        if (Engine::Input::IsActionJustPressed("StokeTorch")) {
            StokeTorch();
        }

        const float burnMult = std::max(0.0f, std::isfinite(BurnRateMultiplier) ? BurnRateMultiplier : 1.0f);
        Fuel = std::max(0.0f, Fuel - BaseBurnRate * burnMult * deltaTime);

        if (m_FlarePulse > 0.0f) {
            m_FlarePulse = std::max(0.0f, m_FlarePulse - deltaTime * 2.2f);
        }

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* player = ResolvePlayer();
        if (!go || !scene) {
            return;
        }

        // 1. Update dynamic PointLightComponent radius & intensity
        const float fuel01 = GetFuel01();
        if (Fuel <= 0.0f) {
            m_CurrentRadius = DreadRadius;
        } else {
            m_CurrentRadius = MinRadius + (MaxRadius - MinRadius) * fuel01 + m_FlarePulse * 2.0f;
        }

        if (auto* light = ResolveTorchLight(player)) {
            if (Fuel <= 0.0f) {
                light->Radius = DreadRadius;
                light->Intensity = DreadIntensity;
            } else {
                light->Radius = m_CurrentRadius;
                light->Intensity = MinIntensity + (MaxIntensity - MinIntensity) * fuel01 + m_FlarePulse * 0.85f;
            }
        }

        // 2. Searing Rim ([0.75 * R, 1.05 * R]) & nearby enemy count
        const glm::vec3 torchPos = go->GetTransform()->GetWorldPosition();
        if (Fuel > 0.0f && player) {
            if (auto* pc = player->GetComponent<PlayerController>(); pc && pc->IsDodging()) {
                scene->IgniteAtXZ({ torchPos.x, torchPos.z }, DashIgniteRadius);
            }
        }
        const uint32_t nearbyEnemies = UpdateSearingRim(*scene, torchPos, deltaTime);

        // 3. Ash Frenzy (0 < Fuel <= 20%)
        const bool frenzy = IsAshFrenzy();
        if (player) {
            if (auto* stats = player->GetComponent<PlayerStatsComponent>()) {
                const float mult = frenzy ? (1.0f + AshFrenzySpeedBonus) : 1.0f;
                stats->MoveSpeedMultiplier = mult;
                stats->AttackSpeedMultiplier = mult;
            }
        }
        UpdateAshFrenzyVignette(*scene, frenzy, deltaTime);
        const uint32_t musicEnemies = frenzy ? std::max(nearbyEnemies, 6u) : nearbyEnemies;
        Engine::MusicDirector::UpdateCombatState(fuel01, musicEnemies, Engine::Time::GetUnscaledDeltaTime() > 0.0f ? Engine::Time::GetUnscaledDeltaTime() : deltaTime);

        // 4. Darkness Dread (Fuel <= 0%)
        if (Fuel <= 0.0f && player) {
            if (!IsShelteredBySafeLifeline(*scene, player)) {
                const float dreadDmg = DreadDamagePerSec * deltaTime;
                if (auto* stats = player->GetComponent<PlayerStatsComponent>()) {
                    stats->TakeDamage(dreadDmg);
                }
                if (auto* health = player->GetComponent<HealthComponent>()) {
                    DamageEvent evt;
                    evt.Amount = dreadDmg;
                    evt.BypassInvuln = true;
                    health->ApplyDamage(evt);
                }
            }
        }
    }

private:
    [[nodiscard]] Engine::GameObject* ResolvePlayer() const noexcept {
        auto* go = GetGameObject();
        if (!go) {
            return nullptr;
        }
        if (go->GetTag() == Engine::EntityTag::Player) {
            return go;
        }
        if (auto* parent = go->GetTransform()->GetParent()) {
            return parent->GetGameObject();
        }
        return nullptr;
    }

    [[nodiscard]] Engine::PointLightComponent* ResolveTorchLight(Engine::GameObject* player) const noexcept {
        if (auto* go = GetGameObject()) {
            if (auto* selfLight = go->GetComponent<Engine::PointLightComponent>()) {
                return selfLight;
            }
        }
        if (player) {
            return player->GetComponent<Engine::PointLightComponent>();
        }
        return nullptr;
    }

    uint32_t UpdateSearingRim(Engine::Scene& scene, const glm::vec3& torchPos, float deltaTime) {
        const float rMin = m_CurrentRadius * 0.75f;
        const float rMax = m_CurrentRadius * 1.05f;
        const float rMinSq = rMin * rMin;
        const float rMaxSq = rMax * rMax;

        uint32_t nearbyCount = 0;
        const auto enemies = scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy);
        for (Engine::GameObject* enemy : enemies) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - torchPos.x;
            const float dz = ePos.z - torchPos.z;
            const float distSq = dx * dx + dz * dz;
            if (distSq <= (m_CurrentRadius + 3.0f) * (m_CurrentRadius + 3.0f)) {
                ++nearbyCount;
            }
        }

        if (Fuel <= 0.0f) {
            m_SearingTimer = 0.0f;
            return nearbyCount;
        }

        m_SearingTimer += deltaTime;
        if (m_SearingTimer < SearingTickInterval) {
            return nearbyCount;
        }
        const float tickDt = m_SearingTimer;
        m_SearingTimer = 0.0f;

        for (Engine::GameObject* enemy : enemies) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - torchPos.x;
            const float dz = ePos.z - torchPos.z;
            const float distSq = dx * dx + dz * dz;
            if (distSq < rMinSq || distSq > rMaxSq) {
                continue;
            }

            if (auto* health = enemy->GetComponent<HealthComponent>()) {
                const float vuln = std::max(0.0f, health->LightVulnerability);
                DamageEvent evt;
                evt.Amount = AuraDamage * tickDt * vuln;
                evt.Source = ResolvePlayer();
                evt.IsFire = true;
                evt.IsRadiant = true;
                evt.BypassInvuln = true;
                health->ApplyDamage(evt);
            } else if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                target->ApplyDamage(AuraDamage * tickDt, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
            }

            if (auto* ps = enemy->GetComponent<Engine::ParticleSystem>()) {
                Engine::ParticleProps ember;
                ember.position = ePos + glm::vec3(0.0f, 0.45f, 0.0f);
                ember.colorBegin = { 1.0f, 0.65f, 0.22f, 0.95f };
                ember.colorEnd = { 0.95f, 0.20f, 0.05f, 0.0f };
                ember.sizeBegin = 0.10f;
                ember.sizeEnd = 0.03f;
                ember.lifeTime = 0.35f;
                ember.velocity = { 0.0f, 1.8f, 0.0f };
                ember.velocityVariation = { 0.8f, 0.6f, 0.8f };
                ps->EmitBurst(ember, 3u);
            }
        }
        return nearbyCount;
    }

    void UpdateAshFrenzyVignette(Engine::Scene& scene, bool frenzyActive, float deltaTime) {
        Engine::PostProcessComponent* pp = nullptr;
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (goPtr && !goPtr->IsPendingDestroy()) {
                if (auto* comp = goPtr->GetComponent<Engine::PostProcessComponent>()) {
                    pp = comp;
                    break;
                }
            }
        }
        if (!pp) {
            return;
        }

        if (frenzyActive) {
            if (!m_SavedBaselineVignette) {
                m_BaselineVignetteColor = pp->VignetteColor;
                m_BaselineVignetteIntensity = pp->VignetteIntensity;
                m_SavedBaselineVignette = true;
            }
            m_FrenzyPulseTime += deltaTime * 6.0f;
            const float pulse = 0.5f + 0.5f * std::sin(m_FrenzyPulseTime);
            pp->VignetteEnabled = true;
            pp->VignetteColor = { 0.55f, 0.06f, 0.08f };
            pp->VignetteIntensity = std::clamp(m_BaselineVignetteIntensity + 0.14f + 0.12f * pulse, 0.0f, 1.0f);
        } else if (m_SavedBaselineVignette) {
            pp->VignetteColor = m_BaselineVignetteColor;
            pp->VignetteIntensity = m_BaselineVignetteIntensity;
            m_SavedBaselineVignette = false;
            m_FrenzyPulseTime = 0.0f;
        }
    }

    [[nodiscard]] bool IsShelteredBySafeLifeline(Engine::Scene& scene, Engine::GameObject* player) const noexcept {
        const glm::vec3 pPos = player->GetTransform()->GetWorldPosition();
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (!goPtr || goPtr->IsPendingDestroy() || goPtr.get() == player || goPtr.get() == GetGameObject()) {
                continue;
            }
            auto* light = goPtr->GetComponent<Engine::PointLightComponent>();
            if (!light || light->Semantic != Engine::LightSemantic::SafeLifeline || light->Intensity <= 0.3f) {
                continue;
            }
            const glm::vec3 lPos = goPtr->GetTransform()->GetWorldPosition();
            const float dx = pPos.x - lPos.x;
            const float dz = pPos.z - lPos.z;
            if ((dx * dx + dz * dz) <= (light->Radius * light->Radius)) {
                return true;
            }
        }
        return false;
    }

    float m_CurrentRadius = 9.5f;
    float m_FlarePulse = 0.0f;
    float m_SearingTimer = 0.0f;
    float m_FrenzyPulseTime = 0.0f;
    bool m_SavedBaselineVignette = false;
    glm::vec3 m_BaselineVignetteColor{ 0.06f, 0.04f, 0.10f };
    float m_BaselineVignetteIntensity = 0.36f;
};

#endif // SANDBOX_TORCH_COMPONENT_H
