#ifndef SANDBOX_DEPLOYABLE_STRUCTURES_H
#define SANDBOX_DEPLOYABLE_STRUCTURES_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/CombatDamageHelper.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/KineticBowlingComponent.h"
#include "Scripts/PickupComponent.h"
#include "Scripts/SplinterProjectileComponent.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Particles/DecalManager.h>
#include <Engine/Physics/Collision2DInfo.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/UI/UIRadialWheelComponent.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <SDL3/SDL_scancode.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace Sandbox {

struct DeployableDescriptor {
    Engine::RadialWheelItem Item = Engine::RadialWheelItem::None;
    const char* DropId = "";
    const char* DisplayName = "DeployableStructure";
    int FallbackWoodCost = 0;
    float FallbackHealth = 100.0f;
    const char* FallbackPrefabPath = "";
    SDL_Scancode Hotkey = SDL_SCANCODE_UNKNOWN;
    const char* TexturePath = nullptr;
};

class DeployableRegistry {
public:
    static constexpr std::array<DeployableDescriptor, 4> kEntries = {{
        { Engine::RadialWheelItem::SplinterBarricade, "drop_barricade", "Splinter Barricade", 4, 120.0f, "assets/prefabs/barricade.prefab", SDL_SCANCODE_1, "assets/diorama/splinter_barricade.png" },
        { Engine::RadialWheelItem::FlarePeg, "drop_beacon", "Flare Peg", 10, 80.0f, "assets/prefabs/beacon.prefab", SDL_SCANCODE_2, nullptr },
        { Engine::RadialWheelItem::Thumper, "drop_springboard", "Thumper", 15, 75.0f, "assets/prefabs/springboard.prefab", SDL_SCANCODE_3, nullptr },
        { Engine::RadialWheelItem::TarSiphon, "drop_siphon", "Tar Siphon", 20, 100.0f, "assets/prefabs/siphon.prefab", SDL_SCANCODE_4, nullptr }
    }};

    [[nodiscard]] static const std::array<DeployableDescriptor, 4>& All() noexcept {
        return kEntries;
    }

    [[nodiscard]] static const DeployableDescriptor* Find(Engine::RadialWheelItem item) noexcept {
        for (const auto& entry : kEntries) {
            if (entry.Item == item) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static int ResolveWoodCost(Engine::RadialWheelItem item) {
        const auto* desc = Find(item);
        if (!desc) {
            return 0;
        }
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Drops().Find(desc->DropId)) {
                return static_cast<int>(def->WoodCost);
            }
        }
        return desc->FallbackWoodCost;
    }

    [[nodiscard]] static float ResolveMaxHealth(Engine::RadialWheelItem item) {
        const auto* desc = Find(item);
        if (!desc) {
            return 100.0f;
        }
        if (Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Drops().Find(desc->DropId)) {
                if (def->Health > 0.0f) {
                    return def->Health;
                }
            }
        }
        return desc->FallbackHealth;
    }
};

/// @brief Non-Component strategy interface for tactical deployable structures (0 extra ComponentMask128 bits).
class IDeployableBehavior {
public:
    virtual ~IDeployableBehavior() = default;
    virtual void OnConfigure(Engine::GameObject& go, HealthComponent& health) = 0;
    virtual void OnStart(Engine::GameObject& /*go*/) {}
    virtual void OnUpdate(Engine::GameObject& /*go*/, float /*dt*/) {}
    virtual void OnTriggerEnter2D(Engine::GameObject& /*go*/, Engine::GameObject* /*other*/) {}
    virtual void OnCollisionEnter2D(Engine::GameObject& /*go*/, const Engine::Collision2DInfo& /*info*/) {}
};

class BarricadeBehavior final : public IDeployableBehavior {
public:
    float ContactDamage = 10.0f;
    int ShatterSplinterCount = 6;

    void OnConfigure(Engine::GameObject& go, HealthComponent& health) override {
        go.SetTag(Engine::EntityTag::TacticalStructure);
        go.SetLayer(Engine::EntityLayer::LowObstacle);
        if (!go.GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go.AddComponent<Engine::BoxCollider2D>();
            col->size = { 1.1f, 0.55f };
            col->CategoryBits = Engine::PhysicsLayer::LowObstacle;
        }
        Engine::GameObject* goPtr = &go;
        health.OnKilled = [this, goPtr](const DamageEvent& evt) {
            if (m_Shattered) {
                return;
            }
            m_Shattered = true;
            if (auto* scene = goPtr->GetScene()) {
                SplinterProjectileComponent::SpawnBurst(
                    *scene, goPtr->GetTransform()->GetWorldPosition(), evt.ImpulseXZ, ShatterSplinterCount, 0.65f);
            }
        };
    }

    void OnCollisionEnter2D(Engine::GameObject& go, const Engine::Collision2DInfo& info) override {
        auto* other = info.Other;
        if (!other || other->IsPendingDestroy() || other->GetTag() != Engine::EntityTag::Enemy) {
            return;
        }
        DamageEvent evt;
        evt.Amount = ContactDamage;
        evt.Source = &go;
        evt.ImpulseXZ = -info.NormalXZ * 4.0f;
        CombatDamage::Apply(other, evt);
    }

private:
    bool m_Shattered = false;
};

class FlarePegBehavior final : public IDeployableBehavior {
public:
    float LightRadius = 7.5f;
    float RemainingLifetime = 35.0f;

    void OnConfigure(Engine::GameObject& go, HealthComponent& /*health*/) override {
        go.SetTag(Engine::EntityTag::SafeZoneBrazier);
        if (!go.GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go.AddComponent<Engine::BoxCollider2D>();
            col->size = { 0.45f, 0.45f };
            col->CategoryBits = Engine::PhysicsLayer::Environment;
        }
        auto* light = go.GetComponent<Engine::PointLightComponent>();
        if (!light) {
            light = go.AddComponent<Engine::PointLightComponent>();
        }
        light->Color = { 1.0f, 0.78f, 0.36f };
        light->Intensity = 2.2f;
        light->Radius = LightRadius;
        light->Falloff = 1.6f;
        light->Priority = Engine::LightPriority::High_Tactical;
        light->Semantic = Engine::LightSemantic::SafeLifeline;
        light->CastShadows = true;

        Engine::SoundPlayParams igniteParams;
        igniteParams.Bus = Engine::AudioBus::SFX;
        igniteParams.Volume = 0.80f;
        igniteParams.SpatializedXZ = true;
        igniteParams.WorldPos = go.GetTransform()->GetWorldPosition();
        PlaySfxCue("assets/audio/sfx_fire_ignite.wav", igniteParams);
    }

    void OnUpdate(Engine::GameObject& go, float dt) override {
        RemainingLifetime -= dt;
        if (RemainingLifetime <= 0.0f) {
            if (auto* scene = go.GetScene()) {
                scene->DestroyGameObject(&go);
            }
        }
    }
};

class ThumperBehavior final : public IDeployableBehavior {
public:
    float BlastRadius = 2.2f;
    float LaunchImpulseXZ = 24.0f;
    float LaunchVerticalVel = 6.0f;
    float ResetCooldown = 2.5f;
    float CooldownTimer = 0.0f;

    void OnConfigure(Engine::GameObject& go, HealthComponent& /*health*/) override {
        go.SetTag(Engine::EntityTag::TacticalStructure);
        if (!go.GetComponent<Engine::CircleCollider2D>()) {
            auto* col = go.AddComponent<Engine::CircleCollider2D>();
            col->Radius = 1.2f;
            col->IsSensor = true;
            col->CategoryBits = Engine::PhysicsLayer::LowObstacle;
            col->MaskBits = Engine::PhysicsLayer::Enemy;
        }
        if (!go.GetComponent<Engine::SquashStretchComponent>()) {
            go.AddComponent<Engine::SquashStretchComponent>();
        }
    }

    void OnTriggerEnter2D(Engine::GameObject& go, Engine::GameObject* other) override {
        if (CooldownTimer > 0.0f || !other || other->IsPendingDestroy()) {
            return;
        }
        if (other->GetTag() == Engine::EntityTag::Enemy) {
            TriggerBlast(go);
        }
    }

    void OnUpdate(Engine::GameObject& /*go*/, float dt) override {
        if (CooldownTimer > 0.0f) {
            CooldownTimer = std::max(0.0f, CooldownTimer - dt);
        }
    }

    void TriggerBlast(Engine::GameObject& go) {
        auto* scene = go.GetScene();
        if (!scene || CooldownTimer > 0.0f) {
            return;
        }

        CooldownTimer = ResetCooldown;
        const glm::vec3 center = go.GetTransform()->GetWorldPosition();
        const float rSq = BlastRadius * BlastRadius;

        for (Engine::GameObject* enemy : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
            const glm::vec2 delta{ ePos.x - center.x, ePos.z - center.z };
            if (glm::dot(delta, delta) > rSq) {
                continue;
            }

            const glm::vec2 dir = SafeNormalizeXZ(delta);
            if (auto* rb = enemy->GetComponent<Engine::Rigidbody2D>()) {
                rb->LaunchVertical(LaunchVerticalVel);
                rb->ApplyImpulseXZ(dir * LaunchImpulseXZ);
            }
            if (auto* bowling = enemy->GetComponent<KineticBowlingComponent>()) {
                bowling->Launch(0.6f);
            }
        }

        if (auto* ss = go.GetComponent<Engine::SquashStretchComponent>()) {
            ss->TriggerImpulse(3.2f, 12.0f);
        }

        Engine::GroundDecal decal;
        decal.Position = { center.x, 0.0f, center.z };
        decal.SizeXZ = { BlastRadius * 1.4f, BlastRadius * 1.4f };
        decal.Color = { 0.35f, 0.30f, 0.25f, 0.70f };
        scene->GetDecalManager().SpawnDecal(decal);
    }
};

class TarSiphonBehavior final : public IDeployableBehavior {
public:
    float TarRadius = 3.2f;
    float CycleInterval = 3.0f;
    float CycleTimer = 0.0f;

    void OnConfigure(Engine::GameObject& go, HealthComponent& /*health*/) override {
        go.SetTag(Engine::EntityTag::TacticalStructure);
        if (!go.GetComponent<Engine::BoxCollider2D>()) {
            auto* col = go.AddComponent<Engine::BoxCollider2D>();
            col->size = { 0.75f, 0.75f };
            col->CategoryBits = Engine::PhysicsLayer::Environment;
        }
    }

    void OnStart(Engine::GameObject& go) override {
        PaintTarPool(go);
    }

    void OnUpdate(Engine::GameObject& go, float dt) override {
        CycleTimer += dt;
        if (CycleTimer >= CycleInterval) {
            CycleTimer -= CycleInterval;
            PaintTarPool(go);
            if (auto* scene = go.GetScene()) {
                const glm::vec3 pos = go.GetTransform()->GetWorldPosition();
                PickupComponent::SpawnPickup(*scene, PickupKind::AshEmber, pos, { 1.2f, 0.6f }, 5.0f);
            }
        }
    }

private:
    void PaintTarPool(Engine::GameObject& go) const {
        if (auto* scene = go.GetScene()) {
            const glm::vec3 pos = go.GetTransform()->GetWorldPosition();
            scene->PaintMaterialWorld({ pos.x, pos.z }, 2u, TarRadius, 1.0f);
        }
    }
};

} // namespace Sandbox

/// @brief Consolidated component for all tactical deployable structures, delegating to IDeployableBehavior.
class DeployableStructureComponent final : public Engine::Component {
public:
    void SetKind(Engine::RadialWheelItem kind) {
        if (m_Kind == kind && m_Behavior) {
            return;
        }
        m_Kind = kind;
        switch (kind) {
            case Engine::RadialWheelItem::SplinterBarricade:
                m_Behavior = std::make_unique<Sandbox::BarricadeBehavior>();
                break;
            case Engine::RadialWheelItem::FlarePeg:
                m_Behavior = std::make_unique<Sandbox::FlarePegBehavior>();
                break;
            case Engine::RadialWheelItem::Thumper:
                m_Behavior = std::make_unique<Sandbox::ThumperBehavior>();
                break;
            case Engine::RadialWheelItem::TarSiphon:
                m_Behavior = std::make_unique<Sandbox::TarSiphonBehavior>();
                break;
            default:
                m_Behavior.reset();
                break;
        }
        ConfigureIfAttached();
    }

    [[nodiscard]] Engine::RadialWheelItem GetKind() const noexcept { return m_Kind; }

    template<typename T>
    [[nodiscard]] T* GetBehavior() noexcept {
        return dynamic_cast<T*>(m_Behavior.get());
    }

    template<typename T>
    [[nodiscard]] const T* GetBehavior() const noexcept {
        return dynamic_cast<const T*>(m_Behavior.get());
    }

    void Awake() override {
        if (!m_Behavior && m_Kind != Engine::RadialWheelItem::None) {
            SetKind(m_Kind);
            return;
        }
        ConfigureIfAttached();
    }

    void Start() override {
        if (auto* go = GetGameObject(); go && m_Behavior) {
            m_Behavior->OnStart(*go);
        }
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            return;
        }
        if (auto* go = GetGameObject(); go && m_Behavior) {
            m_Behavior->OnUpdate(*go, deltaTime);
        }
    }

    void OnTriggerEnter2D(Engine::GameObject* other) override {
        if (auto* go = GetGameObject(); go && m_Behavior) {
            m_Behavior->OnTriggerEnter2D(*go, other);
        }
    }

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        if (auto* go = GetGameObject(); go && m_Behavior) {
            m_Behavior->OnCollisionEnter2D(*go, info);
        }
    }

private:
    void ConfigureIfAttached() {
        auto* go = GetGameObject();
        if (!go || !m_Behavior || m_ConfiguredKind == m_Kind) {
            return;
        }
        m_ConfiguredKind = m_Kind;

        if (const auto* desc = Sandbox::DeployableRegistry::Find(m_Kind); desc && desc->TexturePath) {
            if (auto* sr = go->GetComponent<Engine::SpriteRenderer>()) {
                if (auto tex = Engine::ResourceManager::Load<Engine::Texture>(desc->TexturePath)) {
                    sr->SetTexture(tex);
                    sr->SetAlphaCutoff(0.18f);
                }
            }
        }

        auto* health = go->GetComponent<HealthComponent>();
        if (!health) {
            health = go->AddComponent<HealthComponent>();
        }
        health->ResetHealth(Sandbox::DeployableRegistry::ResolveMaxHealth(m_Kind));
        health->DestroyOnDeath = true;

        m_Behavior->OnConfigure(*go, *health);
    }

    Engine::RadialWheelItem m_Kind = Engine::RadialWheelItem::SplinterBarricade;
    Engine::RadialWheelItem m_ConfiguredKind = Engine::RadialWheelItem::None;
    std::unique_ptr<Sandbox::IDeployableBehavior> m_Behavior;
};

#endif // SANDBOX_DEPLOYABLE_STRUCTURES_H
