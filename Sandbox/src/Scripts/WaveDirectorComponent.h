#ifndef SANDBOX_WAVE_DIRECTOR_COMPONENT_H
#define SANDBOX_WAVE_DIRECTOR_COMPONENT_H

#include "Scripts/EnemyAIController.h"
#include "Scripts/HealthComponent.h"

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/Components/LightComponents.h>
#include <Engine/Scene/EntityPoolManager.h>
#include <Engine/Scene/EntityTag.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Prefab.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

enum class NightPhase : uint8_t {
    Dusk = 0,          // 0:00 - 1:15 (0s .. 75s)
    DeepNight = 1,     // 1:15 - 2:45 (75s .. 165s)
    UmbralSurge = 2,   // 2:45 - 4:15 (165s .. 255s)
    EclipseClimax = 3, // 4:15 - 5:00 (255s .. 300s)
    DawnBreak = 4      // >= 5:00 (300s+)
};

class WaveDirectorComponent final : public Engine::Component {
public:
    static constexpr float kDuskEndSec = 75.0f;
    static constexpr float kDeepNightEndSec = 165.0f;
    static constexpr float kUmbralSurgeEndSec = 255.0f;
    static constexpr float kDawnBreakSec = 300.0f;

    bool EndlessMode = false;
    float SpawnMinRadius = 11.5f;
    float SpawnMaxRadius = 15.5f;
    uint32_t MaxActiveEnemies = 28;
    std::function<void()> OnDawnVictory;

    void Start() override {
        PrewarmPools();
    }

    [[nodiscard]] float GetElapsedSeconds() const noexcept { return m_ElapsedSec; }
    [[nodiscard]] NightPhase GetCurrentPhase() const noexcept { return m_Phase; }
    [[nodiscard]] bool IsVictory() const noexcept { return m_Victory; }
    [[nodiscard]] uint32_t GetTotalSpawned() const noexcept { return m_TotalSpawned; }
    [[nodiscard]] Engine::GameObject* GetActiveBoss() const noexcept {
        if (m_ActiveBoss && !m_ActiveBoss->IsPendingDestroy() && m_ActiveBoss->IsActive()) {
            return m_ActiveBoss;
        }
        return nullptr;
    }

    void AdvanceClockForTest(float elapsedSeconds) {
        m_ElapsedSec = std::max(0.0f, elapsedSeconds);
        EvaluatePhaseAndMood();
    }

    Engine::GameObject* SpawnPooledEnemy(const std::string& enemyId) {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return nullptr;
        }
        PrewarmPools();

        const auto it = m_Prefabs.find(enemyId);
        if (it == m_Prefabs.end() || !it->second) {
            return nullptr;
        }

        glm::vec3 playerPos{ 0.0f };
        if (auto* player = scene->FindFirstGameObjectWithTag(Engine::EntityTag::Player)) {
            playerPos = player->GetTransform()->GetWorldPosition();
        }

        const glm::vec3 spawnPos = FindDarkSpawnPoint(*scene, { playerPos.x, playerPos.z });
        const std::string poolKey = "pool_" + enemyId;
        Engine::GameObject* spawned = scene->GetPoolManager().AcquirePooled(poolKey, *it->second, spawnPos);
        if (!spawned) {
            return nullptr;
        }

        auto* ai = spawned->GetComponent<EnemyAIController>();
        if (!ai) {
            ai = spawned->AddComponent<EnemyAIController>();
        }
        ai->ConfigureArchetype(enemyId);

        if (auto* health = spawned->GetComponent<HealthComponent>()) {
            health->PoolKey = poolKey;
            health->DestroyOnDeath = false;
        }

        ++m_TotalSpawned;
        if (enemyId == "enemy_wendigo") {
            m_ActiveBoss = spawned;
        }
        return spawned;
    }

    void Update(float deltaTime) override {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f || m_Victory) {
            return;
        }

        m_ElapsedSec += deltaTime;
        EvaluatePhaseAndMood();
        if (m_Victory) {
            return;
        }

        m_SpawnTimer -= deltaTime;
        if (m_SpawnTimer <= 0.0f) {
            SpawnWaveTick();
        }
    }

private:
    void PrewarmPools() {
        if (m_PoolsPrewarmed) {
            return;
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }
        m_PoolsPrewarmed = true;

        auto makePrefab = [](const char* name, float radius) {
            nlohmann::json entityJson = {
                { "UUID", 1 },
                { "Name", name },
                { "Tag", "Enemy" },
                { "LayerMask", Engine::EntityLayer::Enemy },
                { "ParentUUID", 0 },
                { "Components", {
                    { "Transform", {
                        { "Position", { 0.0f, 0.0f, 0.0f } },
                        { "Rotation", { 0.0f, 0.0f, 0.0f } },
                        { "Scale", { 1.15f, 1.35f, 1.0f } }
                    } },
                    { "SpriteRenderer", {
                        { "Color", { 1.0f, 1.0f, 1.0f, 1.0f } },
                        { "AlphaCutoff", 0.18f },
                        { "CastShadows", true },
                        { "ReceiveLighting", true }
                    } },
                    { "Rigidbody2D", {
                        { "BodyType", 2 },
                        { "UseGravityY", true },
                        { "LinearDamping", 4.0f }
                    } },
                    { "CircleCollider2D", {
                        { "Radius", radius },
                        { "CategoryBits", Engine::PhysicsLayer::Enemy },
                        { "MaskBits", Engine::PhysicsLayer::All }
                    } }
                } }
            };
            return std::make_shared<Engine::Prefab>(std::move(entityJson));
        };

        m_Prefabs["enemy_ghoul"] = makePrefab("Flesheater Ghoul", 0.40f);
        m_Prefabs["enemy_thrall"] = makePrefab("Rotting Thrall", 0.45f);
        m_Prefabs["enemy_jiangshi"] = makePrefab("Hopping Jiangshi", 0.42f);
        m_Prefabs["enemy_wisp"] = makePrefab("Blight Wisp", 0.35f);
        m_Prefabs["enemy_wendigo"] = makePrefab("Apex Wendigo", 0.60f);

        auto& poolMgr = scene->GetPoolManager();
        poolMgr.PrewarmPool("pool_enemy_ghoul", *m_Prefabs["enemy_ghoul"], 6u);
        poolMgr.PrewarmPool("pool_enemy_thrall", *m_Prefabs["enemy_thrall"], 4u);
        poolMgr.PrewarmPool("pool_enemy_jiangshi", *m_Prefabs["enemy_jiangshi"], 4u);
        poolMgr.PrewarmPool("pool_enemy_wisp", *m_Prefabs["enemy_wisp"], 4u);
        poolMgr.PrewarmPool("pool_enemy_wendigo", *m_Prefabs["enemy_wendigo"], 2u);
    }

    void EvaluatePhaseAndMood() {
        NightPhase nextPhase = NightPhase::Dusk;
        if (!EndlessMode && m_ElapsedSec >= kDawnBreakSec) {
            nextPhase = NightPhase::DawnBreak;
        } else if (m_ElapsedSec >= kUmbralSurgeEndSec) {
            nextPhase = NightPhase::EclipseClimax;
        } else if (m_ElapsedSec >= kDeepNightEndSec) {
            nextPhase = NightPhase::UmbralSurge;
        } else if (m_ElapsedSec >= kDuskEndSec) {
            nextPhase = NightPhase::DeepNight;
        }

        if (nextPhase != m_Phase) {
            m_Phase = nextPhase;
            OnPhaseEntered(nextPhase);
        }
    }

    void OnPhaseEntered(NightPhase phase) {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        Engine::DirectionalLightComponent* moon = FindMoonLight(*scene);
        switch (phase) {
            case NightPhase::Dusk:
                if (moon) {
                    moon->SunIntensity = 0.25f;
                }
                break;
            case NightPhase::DeepNight:
                if (moon) {
                    moon->SunIntensity = 0.14f;
                    moon->AmbientIntensity = 0.15f;
                }
                break;
            case NightPhase::UmbralSurge:
                if (moon) {
                    moon->SunIntensity = 0.10f;
                    moon->AmbientIntensity = 0.12f;
                }
                if (!m_FirstWendigoSpawned) {
                    m_FirstWendigoSpawned = true;
                    SpawnPooledEnemy("enemy_wendigo");
                }
                break;
            case NightPhase::EclipseClimax:
                if (moon) {
                    moon->SunIntensity = 0.06f;
                    moon->SunColor = { 0.58f, 0.24f, 0.42f };
                }
                if (!m_TwinWendigosSpawned) {
                    m_TwinWendigosSpawned = true;
                    SpawnPooledEnemy("enemy_wendigo");
                    SpawnPooledEnemy("enemy_wendigo");
                    SpawnPooledEnemy("enemy_jiangshi");
                }
                break;
            case NightPhase::DawnBreak:
                TriggerDawnBreak(*scene, moon);
                break;
        }
    }

    void TriggerDawnBreak(Engine::Scene& scene, Engine::DirectionalLightComponent* moon) {
        m_Victory = true;
        if (moon) {
            moon->SunColor = { 1.0f, 0.88f, 0.56f };
            moon->SunIntensity = 1.15f;
            moon->AmbientColor = { 0.62f, 0.58f, 0.52f };
            moon->AmbientIntensity = 0.65f;
        }

        // Dissolve remaining shadows to ash
        for (Engine::GameObject* enemy : scene.FindGameObjectsWithTag(Engine::EntityTag::Enemy)) {
            if (!enemy || enemy->IsPendingDestroy()) {
                continue;
            }
            if (auto* health = enemy->GetComponent<HealthComponent>()) {
                DamageEvent dawn;
                dawn.Amount = 9999.0f;
                dawn.IsRadiant = true;
                dawn.BypassInvuln = true;
                health->ApplyDamage(dawn);
            } else {
                scene.DestroyGameObject(enemy);
            }
        }

        if (OnDawnVictory) {
            OnDawnVictory();
        }
    }

    void SpawnWaveTick() {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        const size_t activeCount = scene->FindGameObjectsWithTag(Engine::EntityTag::Enemy).size();
        if (activeCount >= MaxActiveEnemies) {
            m_SpawnTimer = 1.5f;
            return;
        }

        const uint32_t roll = NextRand() % 100u;
        switch (m_Phase) {
            case NightPhase::Dusk:
                m_SpawnTimer = 4.2f;
                SpawnPooledEnemy((roll < 70u) ? "enemy_ghoul" : "enemy_thrall");
                if (roll < 35u) SpawnPooledEnemy("enemy_ghoul");
                break;
            case NightPhase::DeepNight:
                m_SpawnTimer = 3.2f;
                if (roll < 40u) SpawnPooledEnemy("enemy_ghoul");
                else if (roll < 65u) SpawnPooledEnemy("enemy_thrall");
                else if (roll < 85u) SpawnPooledEnemy("enemy_jiangshi");
                else SpawnPooledEnemy("enemy_wisp");
                break;
            case NightPhase::UmbralSurge:
                m_SpawnTimer = 2.4f;
                SpawnPooledEnemy((roll < 50u) ? "enemy_ghoul" : "enemy_jiangshi");
                SpawnPooledEnemy((roll < 45u) ? "enemy_thrall" : "enemy_wisp");
                break;
            case NightPhase::EclipseClimax:
                m_SpawnTimer = 1.8f;
                SpawnPooledEnemy((roll < 40u) ? "enemy_jiangshi" : "enemy_ghoul");
                SpawnPooledEnemy((roll < 50u) ? "enemy_wisp" : "enemy_thrall");
                break;
            case NightPhase::DawnBreak:
                break;
        }
    }

    [[nodiscard]] glm::vec3 FindDarkSpawnPoint(const Engine::Scene& scene, const glm::vec2& playerXZ) {
        constexpr int kCandidates = 12;
        const float baseAngle = static_cast<float>(NextRand() % 360u) * 0.0174532925f;
        const float minR = std::max(6.0f, SpawnMinRadius);
        const float maxR = std::max(minR + 1.0f, SpawnMaxRadius);

        glm::vec2 bestPos = playerXZ + glm::vec2(std::cos(baseAngle) * minR, std::sin(baseAngle) * minR);
        float lowestLum = 999.0f;

        for (int i = 0; i < kCandidates; ++i) {
            const float angle = baseAngle + (6.2831853f * static_cast<float>(i)) / static_cast<float>(kCandidates);
            const float frac = static_cast<float>(NextRand() % 100u) / 100.0f;
            const float radius = minR + (maxR - minR) * frac;
            const glm::vec2 candidate{ playerXZ.x + std::cos(angle) * radius,
                                       playerXZ.y + std::sin(angle) * radius };

            const auto illum = scene.EvaluateIlluminationAtXZ(candidate);
            if (!illum.IsInSafeLight(0.05f)) {
                return { candidate.x, 0.0f, candidate.y };
            }
            if (illum.SafeIntensity < lowestLum) {
                lowestLum = illum.SafeIntensity;
                bestPos = candidate;
            }
        }
        return { bestPos.x, 0.0f, bestPos.y };
    }

    [[nodiscard]] static Engine::DirectionalLightComponent* FindMoonLight(Engine::Scene& scene) noexcept {
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (goPtr && !goPtr->IsPendingDestroy()) {
                if (auto* dl = goPtr->GetComponent<Engine::DirectionalLightComponent>()) {
                    return dl;
                }
            }
        }
        return nullptr;
    }

    uint32_t NextRand() noexcept {
        m_RngState ^= m_RngState << 13;
        m_RngState ^= m_RngState >> 17;
        m_RngState ^= m_RngState << 5;
        return m_RngState;
    }

    NightPhase m_Phase = NightPhase::Dusk;
    float m_ElapsedSec = 0.0f;
    float m_SpawnTimer = 1.5f;
    bool m_PoolsPrewarmed = false;
    bool m_FirstWendigoSpawned = false;
    bool m_TwinWendigosSpawned = false;
    bool m_Victory = false;
    uint32_t m_TotalSpawned = 0;
    uint32_t m_RngState = 0xA511E9B3u;
    Engine::GameObject* m_ActiveBoss = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Engine::Prefab>> m_Prefabs;
};

#endif // SANDBOX_WAVE_DIRECTOR_COMPONENT_H
