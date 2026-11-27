#ifndef SANDBOX_HARVESTABLE_COMPONENT_H
#define SANDBOX_HARVESTABLE_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatTargetComponent.h"
#include "Scripts/PickupComponent.h"

#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/Collider2D.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

class HarvestableComponent final : public Engine::Component {
public:
    float DefaultTreeHealth = 60.0f;
    float DefaultPropHealth = 30.0f;
    float DissolveDuration = 0.25f;
    int MinDrops = 2;
    int MaxDrops = 4;

    void Awake() override {
        BindCombatTarget();
    }

    void Start() override {
        BindCombatTarget();
    }

    void Update(float deltaTime) override {
        if (!m_Felled || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        m_DissolveTimer += deltaTime;
        const float t = std::clamp(m_DissolveTimer / std::max(0.05f, DissolveDuration), 0.0f, 1.0f);

        if (auto* go = GetGameObject()) {
            if (auto* tf = go->GetTransform()) {
                tf->scale = m_InitialScale * (1.0f - 0.85f * t);
            }
            if (t >= 1.0f) {
                if (auto* scene = go->GetScene()) {
                    scene->DestroyGameObject(go);
                }
            }
        }
    }

    [[nodiscard]] bool IsFelled() const noexcept { return m_Felled; }

private:
    void BindCombatTarget() {
        if (m_Bound) {
            return;
        }
        auto* go = GetGameObject();
        if (!go) {
            return;
        }

        auto* target = go->GetComponent<CombatTargetComponent>();
        if (!target) {
            target = go->AddComponent<CombatTargetComponent>();
            const float hp = (go->GetTag() == Engine::EntityTag::HarvestableTree)
                ? DefaultTreeHealth
                : DefaultPropHealth;
            target->ResetHealth(hp);
        }
        target->DestroyOnDeath = false;

        target->OnDamaged = [this](float /*amount*/) {
            HandleDamagedFeedback();
        };
        target->OnKilled = [this]() {
            HandleFelled();
        };
        m_Bound = true;
    }

    void HandleDamagedFeedback() {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        if (auto* ss = go->GetComponent<Engine::SquashStretchComponent>()) {
            ss->TriggerImpulse(-2.6f, 10.0f);
        } else {
            auto* added = go->AddComponent<Engine::SquashStretchComponent>();
            added->TriggerImpulse(-2.6f, 10.0f);
        }

        auto* ps = go->GetComponent<Engine::ParticleSystem>();
        if (!ps) {
            ps = go->AddComponent<Engine::ParticleSystem>(64);
        }
        Engine::ParticleProps chipProps;
        chipProps.position = go->GetTransform()->GetWorldPosition() + glm::vec3(0.0f, 0.7f, 0.0f);
        chipProps.colorBegin = { 0.62f, 0.42f, 0.24f, 0.95f };
        chipProps.colorEnd = { 0.42f, 0.28f, 0.16f, 0.0f };
        chipProps.sizeBegin = 0.14f;
        chipProps.sizeEnd = 0.06f;
        chipProps.lifeTime = 0.40f;
        chipProps.velocity = { 0.0f, 2.2f, 0.0f };
        chipProps.velocityVariation = { 2.8f, 1.5f, 2.8f };
        for (int i = 0; i < 8; ++i) {
            ps->Emit(chipProps);
        }
        Engine::SoundPlayParams chopParams;
        chopParams.Bus = Engine::AudioBus::SFX;
        chopParams.Volume = 0.85f;
        chopParams.SpatializedXZ = true;
        chopParams.WorldPos = go->GetTransform()->GetWorldPosition();
        Sandbox::PlaySfxCue("assets/audio/sfx_wood_chop.wav", chopParams);
    }

    void HandleFelled() {
        if (m_Felled) {
            return;
        }
        m_Felled = true;
        m_DissolveTimer = 0.0f;

        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }

        m_InitialScale = go->GetTransform()->scale;
        if (auto* col = go->GetComponent<Engine::Collider2D>()) {
            col->SetEnabled(false);
        }

        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        const int clampedMin = std::max(1, MinDrops);
        const int clampedMax = std::max(clampedMin, MaxDrops);
        uint64_t rng = go->GetUUID() ^ 0x9E3779B97F4A7C15ULL;
        auto nextFloat01 = [&rng]() -> float {
            rng ^= rng >> 12;
            rng ^= rng << 25;
            rng ^= rng >> 27;
            const uint32_t bits = static_cast<uint32_t>((rng * 0x2545F4914F6CDD1DULL) >> 40);
            return static_cast<float>(bits) / static_cast<float>(1u << 24);
        };

        const int dropCount = clampedMin + static_cast<int>(nextFloat01() * static_cast<float>(clampedMax - clampedMin + 1));
        const int safeDropCount = std::clamp(dropCount, clampedMin, clampedMax);

        const Engine::EntityTag tag = go->GetTag();
        for (int i = 0; i < safeDropCount; ++i) {
            const float angle = (6.2831853f * static_cast<float>(i)) / static_cast<float>(safeDropCount)
                              + (nextFloat01() - 0.5f) * 0.65f;
            const float speedXZ = 1.6f + nextFloat01() * 2.2f;
            const glm::vec2 scatterVel{ std::cos(angle) * speedXZ, std::sin(angle) * speedXZ };
            const float launchY = 4.5f + nextFloat01() * 2.0f; // [4.5f, 6.5f]

            PickupKind kind = PickupKind::WoodLog;
            if (tag == Engine::EntityTag::Foliage) {
                kind = (i == 0) ? PickupKind::CrimsonBerry : PickupKind::WoodLog;
            } else if (tag == Engine::EntityTag::DestructibleProp) {
                kind = (i % 2 == 0) ? PickupKind::WoodLog : PickupKind::AshEmber;
            } else if (i == safeDropCount - 1 && nextFloat01() > 0.55f) {
                kind = PickupKind::AshEmber;
            }

            PickupComponent::SpawnPickup(*scene, kind, origin, scatterVel, launchY);
        }
    }

    bool m_Bound = false;
    bool m_Felled = false;
    float m_DissolveTimer = 0.0f;
    glm::vec3 m_InitialScale{ 1.0f };
};

#endif // SANDBOX_HARVESTABLE_COMPONENT_H
