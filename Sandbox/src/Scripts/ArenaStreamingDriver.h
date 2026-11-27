#ifndef SANDBOX_ARENASTREAMINGDRIVER_H
#define SANDBOX_ARENASTREAMINGDRIVER_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Scripts/CombatTargetComponent.h"
#include "Scripts/HarvestableComponent.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/StatusEffectsComponent.h"

namespace Sandbox {

/// @brief Attaches HarvestableComponent to any HarvestableTree or DestructibleProp in the scene.
inline void BindHarvestableProps(Engine::Scene& scene) {
    for (const auto& goPtr : scene.GetGameObjects()) {
        if (!goPtr || goPtr->IsPendingDestroy()) {
            continue;
        }
        const auto tag = goPtr->GetTag();
        if (tag == Engine::EntityTag::HarvestableTree || tag == Engine::EntityTag::DestructibleProp) {
            if (!goPtr->GetComponent<HarvestableComponent>()) {
                goPtr->AddComponent<HarvestableComponent>();
            }
        }
    }
}

/// @brief Drives TerrainChunkManager streaming from the Player's world XZ position each frame.
class ArenaStreamingDriver final : public Engine::Component {
public:
    void Update(float deltaTime) override {
        auto* owner = GetGameObject();
        if (!owner) {
            return;
        }
        auto* scene = owner->GetScene();
        if (!scene) {
            return;
        }
        for (const auto& goPtr : scene->GetGameObjects()) {
            if (goPtr && goPtr->GetName() == "Terrain") {
                if (auto* chunkMgr = goPtr->GetComponent<Engine::TerrainChunkManager>()) {
                    chunkMgr->UpdateStreaming(owner->GetTransform()->GetWorldPosition());
                }
                break;
            }
        }
        BindHarvestableProps(*scene);

        // Tick FireHazard damage (14 DPS) for enemies crossing active TerrainFireGrid cells
        if (std::isfinite(deltaTime) && deltaTime > 0.0f) {
            m_FireTickTimer += deltaTime;
            if (m_FireTickTimer >= 0.25f) {
                const float tickDt = m_FireTickTimer;
                m_FireTickTimer = 0.0f;
                for (Engine::GameObject* enemy : scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
                    if (!enemy || enemy->IsPendingDestroy()) continue;
                    const glm::vec3 ePos = enemy->GetTransform()->GetWorldPosition();
                    if (!scene->IsBurningAtXZ(ePos.x, ePos.z)) continue;
                    if (auto* health = enemy->GetComponent<HealthComponent>()) {
                        DamageEvent evt;
                        evt.Amount = 14.0f * tickDt;
                        evt.Source = owner;
                        evt.IsFire = true;
                        evt.BypassInvuln = true;
                        health->ApplyDamage(evt);
                    } else if (auto* target = enemy->GetComponent<CombatTargetComponent>()) {
                        target->ApplyDamage(14.0f * tickDt, 0.0f, Engine::SpriteRenderer::FlashMode::CrimsonImpact);
                        target->ApplyBurn(2.5f, 6.0f);
                    }
                    if (auto* status = enemy->GetComponent<StatusEffectsComponent>()) {
                        status->ApplyBurn(2.5f, 6.0f);
                    }
                }
            }
        }
    }

private:
    float m_FireTickTimer = 0.0f;
};

} // namespace Sandbox

#endif // SANDBOX_ARENASTREAMINGDRIVER_H
