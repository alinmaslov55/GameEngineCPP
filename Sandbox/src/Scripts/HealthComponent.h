#ifndef SANDBOX_HEALTH_COMPONENT_H
#define SANDBOX_HEALTH_COMPONENT_H

#include "Scripts/StatusEffectsComponent.h"

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/Time.h>
#include <Engine/Particles/DecalManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/Collider2D.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/EntityPoolManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

class HealthComponent final : public Engine::Component {
public:
    static constexpr float kFlashDecayDuration = 0.12f;

    float MaxHP = 100.0f;
    float CurrentHP = 100.0f;
    float DamageReduction = 0.0f;     // [0.0, 0.90]
    float KnockbackResistance = 0.0f; // [0.0, 1.0]
    float LightVulnerability = 1.0f;  // Searing Rim radiant multiplier
    float InvulnOnHitDuration = 0.0f;
    float DissolveDuration = 0.35f;
    bool DestroyOnDeath = true;
    std::string PoolKey;

    std::shared_ptr<Engine::AudioClip> HitClip;
    std::shared_ptr<Engine::AudioClip> DeathClip;

    std::function<void(const DamageEvent&, float finalDamage)> OnDamaged;
    std::function<void(const DamageEvent&)> OnKilled;

    void ResetHealth(float maxHp) noexcept {
        MaxHP = std::max(1.0f, std::isfinite(maxHp) ? maxHp : 100.0f);
        CurrentHP = MaxHP;
        m_FlashTimer = 0.0f;
        m_Dying = false;
        m_DissolveTimer = 0.0f;
    }

    void OnPoolAcquire() override {
        ResetHealth(MaxHP);
        if (auto* go = GetGameObject()) {
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                sr->SetDissolveAmount(0.0f);
                sr->SetFlashAmount(0.0f);
            }
            if (auto* col = go->GetComponent<Engine::Collider2D>()) {
                col->SetEnabled(true);
            }
        }
    }

    void OnPoolRelease() override {
        m_FlashTimer = 0.0f;
        m_Dying = false;
        m_DissolveTimer = 0.0f;
    }

    [[nodiscard]] bool IsAlive() const noexcept { return !m_Dying && CurrentHP > 0.0f; }
    [[nodiscard]] bool IsDying() const noexcept { return m_Dying; }

    bool ApplyDamage(const DamageEvent& evt) {
        if (m_Dying || CurrentHP <= 0.0f || !std::isfinite(evt.Amount) || evt.Amount <= 0.0f) {
            return false;
        }

        auto* go = GetGameObject();
        auto* status = go ? go->GetComponent<StatusEffectsComponent>() : nullptr;
        if (status && !evt.BypassInvuln && status->IsInvulnerable()) {
            return false;
        }

        const float clampedDR = std::clamp(std::isfinite(DamageReduction) ? DamageReduction : 0.0f, 0.0f, 0.90f);
        const float finalDamage = evt.Amount * (1.0f - clampedDR);
        if (finalDamage <= 0.0f) {
            return false;
        }

        CurrentHP = std::max(0.0f, CurrentHP - finalDamage);

        if (status) {
            if (evt.StunDuration > 0.0f) {
                status->ApplyStun(evt.StunDuration);
            }
            if (InvulnOnHitDuration > 0.0f && !evt.BypassInvuln) {
                status->SetInvulnerable(InvulnOnHitDuration);
            }
        }

        if (go) {
            // 1. Knockback impulse mitigated by KnockbackResistance
            const float kbRes = std::clamp(std::isfinite(KnockbackResistance) ? KnockbackResistance : 0.0f, 0.0f, 1.0f);
            const glm::vec2 impulse = evt.ImpulseXZ * (1.0f - kbRes);
            if (std::isfinite(impulse.x) && std::isfinite(impulse.y) && glm::dot(impulse, impulse) > 1e-6f) {
                if (auto* rb = go->GetComponent<Engine::Rigidbody2D>()) {
                    rb->ApplyImpulseXZ(impulse);
                }
            }

            // 2. Hit-stop
            if (std::isfinite(evt.HitStopSec) && evt.HitStopSec > 0.0f) {
                Engine::Time::TriggerHitStop(evt.HitStopSec, 0.02f);
            }

            // 3. Hit-flash (PureWhite or CrimsonImpact decaying over 0.12s)
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                sr->SetFlashMode(evt.IsFire ? Engine::SpriteRenderer::FlashMode::CrimsonImpact
                                            : Engine::SpriteRenderer::FlashMode::PureWhite);
                sr->SetFlashAmount(1.0f);
                m_FlashTimer = kFlashDecayDuration;
            }

            // 4. Squash & stretch impulse
            if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
                ss->TriggerImpulse(-2.4f, 9.5f);
            }

            // 5. Particle burst (sparks / ash)
            if (auto* ps = go->GetComponent<Engine::ParticleSystem>()) {
                Engine::ParticleProps burst;
                burst.position = go->GetTransform()->GetWorldPosition() + glm::vec3(0.0f, 0.6f, 0.0f);
                if (evt.IsFire || evt.IsRadiant) {
                    burst.colorBegin = { 1.0f, 0.62f, 0.18f, 0.95f };
                    burst.colorEnd = { 0.95f, 0.22f, 0.08f, 0.0f };
                } else {
                    burst.colorBegin = { 0.85f, 0.82f, 0.78f, 0.90f };
                    burst.colorEnd = { 0.28f, 0.26f, 0.28f, 0.0f };
                }
                burst.sizeBegin = 0.14f;
                burst.sizeEnd = 0.04f;
                burst.lifeTime = 0.35f;
                burst.velocity = { 0.0f, 2.0f, 0.0f };
                burst.velocityVariation = { 2.6f, 1.4f, 2.6f };
                ps->EmitBurst(burst, 8u);
            }

            // 6. Spatialized one-shot audio with +-10% pitch jitter
            std::shared_ptr<Engine::AudioClip> clipToPlay = HitClip;
            if (!clipToPlay) {
                if (auto* audioSrc = go->GetComponent<Engine::AudioSource>()) {
                    clipToPlay = audioSrc->GetClip();
                }
            }
            if (!clipToPlay) {
                const char* fallbackPath = (go->GetTag() == Engine::EntityTag::Player)
                    ? "assets/audio/sfx_player_hurt.wav"
                    : "assets/audio/sfx_hit_heavy.wav";
                clipToPlay = Engine::ResourceManager::Load<Engine::AudioClip>(fallbackPath);
            }
            if (clipToPlay && Engine::AudioManager::IsInitialized()) {
                Engine::SoundPlayParams params;
                params.Bus = Engine::AudioBus::SFX;
                params.PitchMin = 0.90f;
                params.PitchMax = 1.10f;
                params.SpatializedXZ = true;
                params.WorldPos = go->GetTransform()->GetWorldPosition();
                Engine::AudioManager::PlayOneShot(clipToPlay, params);
            }
        }

        if (OnDamaged) {
            OnDamaged(evt, finalDamage);
        }

        if (CurrentHP <= 0.0f && !m_Dying) {
            BeginDeathSequence(evt);
        }
        return true;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }

        auto* go = GetGameObject();
        if (!go) {
            return;
        }

        // Decay hit-flash over 0.12s
        if (m_FlashTimer > 0.0f) {
            m_FlashTimer = std::max(0.0f, m_FlashTimer - deltaTime);
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                const float alpha = std::clamp(m_FlashTimer / kFlashDecayDuration, 0.0f, 1.0f);
                sr->SetFlashAmount(alpha);
            }
        }

        // Tick pending burn damage from StatusEffectsComponent
        if (!m_Dying) {
            if (auto* status = go->GetComponent<StatusEffectsComponent>()) {
                const float burnDmg = status->ConsumePendingBurnDamage();
                if (burnDmg > 0.0f) {
                    DamageEvent burnEvt;
                    burnEvt.Amount = burnDmg;
                    burnEvt.IsFire = true;
                    burnEvt.BypassInvuln = true;
                    ApplyDamage(burnEvt);
                }
            }
        }

        // RND-01 Ash Dissolve (0 -> 1) before returning to pool or destroying
        if (m_Dying) {
            m_DissolveTimer += deltaTime;
            const float dur = std::max(0.05f, DissolveDuration);
            const float t = std::clamp(m_DissolveTimer / dur, 0.0f, 1.0f);
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                sr->SetDissolveAmount(t);
            }
            if (t >= 1.0f) {
                FinishDeath();
            }
        }
    }

private:
    void BeginDeathSequence(const DamageEvent& evt) {
        m_Dying = true;
        m_DissolveTimer = 0.0f;

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (go && scene) {
            // Spawn ground decal (ichor splatter or scorch mark)
            Engine::GroundDecal decal;
            decal.Position = go->GetTransform()->GetWorldPosition();
            decal.Position.y = 0.0f;
            decal.SizeXZ = evt.IsFire ? glm::vec2(1.4f, 1.4f) : glm::vec2(1.1f, 1.1f);
            decal.Color = evt.IsFire ? glm::vec4(0.12f, 0.10f, 0.10f, 0.85f)
                                     : glm::vec4(0.32f, 0.06f, 0.10f, 0.85f);
            scene->GetDecalManager().SpawnDecal(decal);

            if (auto* col = go->GetComponent<Engine::Collider2D>()) {
                col->SetEnabled(false);
            }
            if (DeathClip && Engine::AudioManager::IsInitialized()) {
                Engine::SoundPlayParams params;
                params.Bus = Engine::AudioBus::SFX;
                params.PitchMin = 0.90f;
                params.PitchMax = 1.10f;
                params.SpatializedXZ = true;
                params.WorldPos = go->GetTransform()->GetWorldPosition();
                Engine::AudioManager::PlayOneShot(DeathClip, params);
            }
        }

        if (OnKilled) {
            OnKilled(evt);
        }
    }

    void FinishDeath() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }
        if (!PoolKey.empty()) {
            scene->GetPoolManager().ReleaseToPool(PoolKey, go);
        } else if (DestroyOnDeath) {
            scene->DestroyGameObject(go);
        }
    }

    float m_FlashTimer = 0.0f;
    bool m_Dying = false;
    float m_DissolveTimer = 0.0f;
};

#endif // SANDBOX_HEALTH_COMPONENT_H
