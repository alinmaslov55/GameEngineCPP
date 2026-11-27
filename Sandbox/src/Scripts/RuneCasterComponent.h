#ifndef SANDBOX_RUNE_CASTER_COMPONENT_H
#define SANDBOX_RUNE_CASTER_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/RuneEffectsComponents.h"
#include "Scripts/StatusEffectsComponent.h"
#include "Scripts/TorchComponent.h"

#include <Engine/Core/Input.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
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
#include <string>

class RuneCasterComponent final : public Engine::Component {
public:
    void Awake() override {
        Engine::Input::BindAction("CastRune", {
            .PrimaryKey = SDL_SCANCODE_Q,
            .SecondaryKey = SDL_SCANCODE_UNKNOWN,
            .MouseButton = SDL_BUTTON_RIGHT,
            .PadButton = SDL_GAMEPAD_BUTTON_INVALID,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER
        });

        EquipRune(m_EquippedRune.Id.empty() ? "rune_ignite" : m_EquippedRune.Id);
    }

    bool EquipRune(const std::string& runeId) {
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Runes().Find(runeId)) {
                m_EquippedRune = *def;
                return true;
            }
        }

        // Built-in fallbacks matching Sandbox/assets/data/runes.json
        if (runeId == "rune_ignite") {
            m_EquippedRune = { "rune_ignite", "Rune of Ignition", "", 15.0f, 4.0f, 5.0f, 3.5f, "" };
            return true;
        }
        if (runeId == "rune_chronos") {
            m_EquippedRune = { "rune_chronos", "Rune of Chronos", "", 20.0f, 8.0f, 7.0f, 4.0f, "" };
            return true;
        }
        if (runeId == "rune_shockwave") {
            m_EquippedRune = { "rune_shockwave", "Rune of Shockwave", "", 12.0f, 3.5f, 6.0f, 0.5f, "" };
            return true;
        }
        if (runeId == "rune_consecrate") {
            m_EquippedRune = { "rune_consecrate", "Rune of Consecration", "", 25.0f, 10.0f, 8.0f, 6.0f, "" };
            return true;
        }
        return false;
    }

    [[nodiscard]] const Engine::RuneDefinition& GetEquippedRune() const noexcept { return m_EquippedRune; }
    [[nodiscard]] float GetCooldownRemaining() const noexcept { return m_CooldownTimer; }

    bool TryCastRune() {
        if (m_CooldownTimer > 0.0f) {
            return false;
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* torch = ResolveTorch();
        if (!go || !scene || !torch) {
            return false;
        }
        if (torch->Fuel < m_EquippedRune.FuelCost) {
            return false;
        }

        torch->Fuel = std::max(0.0f, torch->Fuel - m_EquippedRune.FuelCost);
        m_CooldownTimer = m_EquippedRune.CooldownSec;

        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        if (m_EquippedRune.Id == "rune_ignite") {
            CastIgnition(*scene, *go, origin);
        } else if (m_EquippedRune.Id == "rune_chronos") {
            CastChronos(*scene, origin);
        } else if (m_EquippedRune.Id == "rune_shockwave") {
            CastShockwave(*scene, *go, origin);
        } else if (m_EquippedRune.Id == "rune_consecrate") {
            CastConsecration(*scene, origin);
        }
        Engine::SoundPlayParams runeParams;
        runeParams.Bus = Engine::AudioBus::SFX;
        runeParams.Volume = 0.95f;
        runeParams.SpatializedXZ = true;
        runeParams.WorldPos = origin;
        Sandbox::PlaySfxCue("assets/audio/sfx_rune_cast.wav", runeParams);
        return true;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (m_CooldownTimer > 0.0f) {
            m_CooldownTimer = std::max(0.0f, m_CooldownTimer - deltaTime);
        }
        if (Engine::Input::IsActionJustPressed("CastRune")) {
            TryCastRune();
        }
    }

private:
    [[nodiscard]] TorchComponent* ResolveTorch() const noexcept {
        auto* go = GetGameObject();
        if (!go) {
            return nullptr;
        }
        if (auto* selfTorch = go->GetComponent<TorchComponent>()) {
            return selfTorch;
        }
        for (Engine::Transform* child : go->GetTransform()->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* childTorch = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    return childTorch;
                }
            }
        }
        return nullptr;
    }

    void CastIgnition(Engine::Scene& scene, Engine::GameObject& caster, const glm::vec3& origin) {
        const float r = m_EquippedRune.Radius;
        const float rSq = r * r;
        scene.IgniteAtXZ({ origin.x, origin.z }, r);

        for (Engine::GameObject* enemy : scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - origin.x;
            const float dz = ePos.z - origin.z;
            if ((dx * dx + dz * dz) > rSq) {
                continue;
            }

            if (auto* health = enemy->GetComponent<HealthComponent>()) {
                DamageEvent evt;
                evt.Amount = 10.0f;
                evt.Source = &caster;
                evt.IsFire = true;
                health->ApplyDamage(evt);
            } else if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                target->ApplyDamage(10.0f, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
                target->ApplyBurn(m_EquippedRune.DurationSec, 12.0f);
            }
            if (auto* status = enemy->GetComponent<StatusEffectsComponent>()) {
                status->ApplyBurn(m_EquippedRune.DurationSec, 12.0f);
            }
        }
    }

    void CastChronos(Engine::Scene& scene, const glm::vec3& origin) {
        const float r = m_EquippedRune.Radius;
        const float rSq = r * r;
        for (Engine::GameObject* enemy : scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const float dx = ePos.x - origin.x;
            const float dz = ePos.z - origin.z;
            if ((dx * dx + dz * dz) > rSq) {
                continue;
            }

            if (auto* tracker = enemy->GetComponent<ChronosStasisTrackerComponent>()) {
                tracker->Refresh(m_EquippedRune.DurationSec);
            } else {
                tracker = enemy->AddComponent<ChronosStasisTrackerComponent>();
                tracker->RemainingSec = m_EquippedRune.DurationSec;
            }
        }
    }

    void CastShockwave(Engine::Scene& scene, Engine::GameObject& caster, const glm::vec3& origin) {
        const float r = m_EquippedRune.Radius;
        const float rSq = r * r;
        constexpr float kShockwaveImpulse = 28.0f;

        for (Engine::GameObject* enemy : scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const glm::vec2 delta{ ePos.x - origin.x, ePos.z - origin.z };
            const float distSq = glm::dot(delta, delta);
            if (distSq > rSq) {
                continue;
            }

            const float dist = std::sqrt(distSq);
            const glm::vec2 dir = (dist > 1e-4f) ? (delta / dist) : glm::vec2(1.0f, 0.0f);

            if (auto* health = enemy->GetComponent<HealthComponent>()) {
                DamageEvent evt;
                evt.Amount = 15.0f;
                evt.Source = &caster;
                evt.ImpulseXZ = dir * kShockwaveImpulse;
                evt.StunDuration = 0.35f;
                health->ApplyDamage(evt);
            } else {
                if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                    target->ApplyDamage(15.0f, 0.35f, Engine::SpriteRenderer::FlashMode::PureWhite);
                }
                if (auto* rb = enemy->GetComponent<Engine::Rigidbody2D>()) {
                    rb->ApplyImpulseXZ(dir * kShockwaveImpulse);
                }
            }
            if (auto* bowling = enemy->GetComponent<KineticBowlingComponent>()) {
                bowling->Launch(0.6f);
            }
        }

        // Flat ground shockwave ring (VFX-01)
        auto* ringGo = scene.CreateGameObject("ShockwaveRing");
        ringGo->GetTransform()->position = { origin.x, 0.02f, origin.z };
        auto* sr = ringGo->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
        sr->SetColor({ 1.0f, 0.72f, 0.32f, 0.85f });
        sr->SetEmissive(1.2f);
        auto* ring = ringGo->AddComponent<ShockwaveRingComponent>();
        ring->MaxRadius = r;
        ring->DurationSec = m_EquippedRune.DurationSec;

        if (auto* pc = caster.GetComponent<PlayerController>()) {
            pc->TriggerImpactFeedback(0.42f, { 0.0f, -0.25f });
        }
    }

    void CastConsecration(Engine::Scene& scene, const glm::vec3& origin) {
        auto* zoneGo = scene.CreateGameObject("ConsecrationZone");
        zoneGo->SetTag(Engine::EntityTag::SafeZoneBrazier);
        zoneGo->GetTransform()->position = { origin.x, 0.02f, origin.z };
        zoneGo->GetTransform()->scale = { m_EquippedRune.Radius * 2.0f, m_EquippedRune.Radius * 2.0f, 1.0f };

        auto* sr = zoneGo->AddComponent<Engine::SpriteRenderer>();
        sr->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
        sr->SetColor({ 1.0f, 0.86f, 0.38f, 0.32f });
        sr->SetEmissive(0.75f);

        auto* light = zoneGo->AddComponent<Engine::PointLightComponent>();
        light->Color = { 1.0f, 0.84f, 0.36f };
        light->Intensity = 2.1f;
        light->Radius = m_EquippedRune.Radius;
        light->Falloff = 1.6f;
        light->Priority = Engine::LightPriority::High_Tactical;
        light->Semantic = Engine::LightSemantic::SafeLifeline;

        auto* zone = zoneGo->AddComponent<ConsecrationZoneComponent>();
        zone->Radius = m_EquippedRune.Radius;
        zone->DurationSec = m_EquippedRune.DurationSec;
    }

    Engine::RuneDefinition m_EquippedRune{ "rune_ignite", "Rune of Ignition", "", 15.0f, 4.0f, 5.0f, 3.5f, "" };
    float m_CooldownTimer = 0.0f;
};

#endif // SANDBOX_RUNE_CASTER_COMPONENT_H
