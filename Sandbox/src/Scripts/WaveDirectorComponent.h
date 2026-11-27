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

struct WaveEnemyEntry {
    const char* EnemyId = "enemy_ghoul";
    uint32_t Weight = 1;
};

struct NightPhaseConfig {
    NightPhase Phase = NightPhase::Dusk;
    float StartTimeSec = 0.0f;
    float SunIntensity = 0.25f;
    float AmbientIntensity = 0.22f;
    bool OverrideSunColor = false;
    glm::vec3 SunColor = { 0.42f, 0.50f, 0.78f };
    float SpawnIntervalSec = 4.2f;
    int SpawnsPerTick = 1;
    uint32_t BonusSpawnPct = 0;
    const char* BonusEnemyId = nullptr;
    std::vector<WaveEnemyEntry> WeightedPool;
    std::vector<const char*> OnEnterGuaranteedSpawns;
    bool TriggersDawnVictory = false;
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
        EnsureDefaultPhaseConfigs();
        PrewarmPools();
    }

    [[nodiscard]] const std::vector<NightPhaseConfig>& GetPhaseConfigs() {
        EnsureDefaultPhaseConfigs();
        return m_PhaseConfigs;
    }

    void SetPhaseConfigs(std::vector<NightPhaseConfig> configs) {
        m_PhaseConfigs = std::move(configs);
        m_PhaseConfigsInitialized = true;
    }

    [[nodiscard]] float GetElapsedSeconds() const noexcept { return m_ElapsedSec; }
    [[nodiscard]] NightPhase GetCurrentPhase() const noexcept { return m_Phase; }
    [[nodiscard]] bool IsVictory() const noexcept { return m_Victory; }
    [[nodiscard]] uint32_t GetTotalSpawned() const noexcept { return m_TotalSpawned; }
    [[nodiscard]] Engine::GameObject* GetActiveBoss() const noexcept {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return nullptr;
        }
        for (const uint64_t uuid : m_ActiveBossUUIDs) {
            if (auto* boss = scene->FindGameObjectByUUID(uuid)) {
                if (!boss->IsPendingDestroy() && boss->IsActive()) {
                    if (auto* health = boss->GetComponent<HealthComponent>(); !health || health->IsAlive()) {
                        return boss;
                    }
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] size_t GetActiveBossCount() const noexcept {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return 0u;
        }
        size_t count = 0u;
        for (const uint64_t uuid : m_ActiveBossUUIDs) {
            if (auto* boss = scene->FindGameObjectByUUID(uuid)) {
                if (!boss->IsPendingDestroy() && boss->IsActive()) {
                    if (auto* health = boss->GetComponent<HealthComponent>(); !health || health->IsAlive()) {
                        ++count;
                    }
                }
            }
        }
        return count;
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
            const uint64_t bossUuid = spawned->GetUUID();
            if (std::find(m_ActiveBossUUIDs.begin(), m_ActiveBossUUIDs.end(), bossUuid) == m_ActiveBossUUIDs.end()) {
                m_ActiveBossUUIDs.push_back(bossUuid);
            }
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
        EnsureDefaultPhaseConfigs();
        const NightPhaseConfig* targetCfg = &m_PhaseConfigs.front();
        for (const auto& cfg : m_PhaseConfigs) {
            if (EndlessMode && cfg.TriggersDawnVictory) {
                continue;
            }
            if (m_ElapsedSec >= cfg.StartTimeSec && cfg.StartTimeSec >= targetCfg->StartTimeSec) {
                targetCfg = &cfg;
            }
        }

        if (targetCfg->Phase != m_Phase) {
            m_Phase = targetCfg->Phase;
            OnPhaseEntered(*targetCfg);
        }
    }

    void OnPhaseEntered(const NightPhaseConfig& cfg) {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        Engine::DirectionalLightComponent* moon = FindMoonLight(*scene);
        if (cfg.TriggersDawnVictory) {
            TriggerDawnBreak(*scene, moon);
            return;
        }

        if (moon) {
            moon->SunIntensity = cfg.SunIntensity;
            moon->AmbientIntensity = cfg.AmbientIntensity;
            if (cfg.OverrideSunColor) {
                moon->SunColor = cfg.SunColor;
            }
        }

        const uint32_t phaseBit = 1u << static_cast<uint8_t>(cfg.Phase);
        if ((m_EnteredPhasesMask & phaseBit) == 0u) {
            m_EnteredPhasesMask |= phaseBit;
            for (const char* enemyId : cfg.OnEnterGuaranteedSpawns) {
                if (enemyId) {
                    SpawnPooledEnemy(enemyId);
                }
            }
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

        EnsureDefaultPhaseConfigs();
        const NightPhaseConfig* cfg = FindPhaseConfig(m_Phase);
        if (!cfg || cfg->WeightedPool.empty()) {
            return;
        }

        m_SpawnTimer = cfg->SpawnIntervalSec;
        uint32_t totalWeight = 0u;
        for (const auto& entry : cfg->WeightedPool) {
            totalWeight += entry.Weight;
        }
        if (totalWeight == 0u) {
            return;
        }

        for (int i = 0; i < cfg->SpawnsPerTick; ++i) {
            uint32_t roll = NextRand() % totalWeight;
            const char* chosen = cfg->WeightedPool.front().EnemyId;
            for (const auto& entry : cfg->WeightedPool) {
                if (roll < entry.Weight) {
                    chosen = entry.EnemyId;
                    break;
                }
                roll -= entry.Weight;
            }
            SpawnPooledEnemy(chosen);
        }

        if (cfg->BonusEnemyId && cfg->BonusSpawnPct > 0u && (NextRand() % 100u) < cfg->BonusSpawnPct) {
            SpawnPooledEnemy(cfg->BonusEnemyId);
        }
    }

    void EnsureDefaultPhaseConfigs() {
        if (m_PhaseConfigsInitialized && !m_PhaseConfigs.empty()) {
            return;
        }
        m_PhaseConfigsInitialized = true;
        m_PhaseConfigs = {
            NightPhaseConfig{
                .Phase = NightPhase::Dusk,
                .StartTimeSec = 0.0f,
                .SunIntensity = 0.25f,
                .AmbientIntensity = 0.22f,
                .SpawnIntervalSec = 4.2f,
                .SpawnsPerTick = 1,
                .BonusSpawnPct = 35u,
                .BonusEnemyId = "enemy_ghoul",
                .WeightedPool = { { "enemy_ghoul", 70u }, { "enemy_thrall", 30u } }
            },
            NightPhaseConfig{
                .Phase = NightPhase::DeepNight,
                .StartTimeSec = kDuskEndSec,
                .SunIntensity = 0.14f,
                .AmbientIntensity = 0.15f,
                .SpawnIntervalSec = 3.2f,
                .SpawnsPerTick = 1,
                .WeightedPool = {
                    { "enemy_ghoul", 40u },
                    { "enemy_thrall", 25u },
                    { "enemy_jiangshi", 20u },
                    { "enemy_wisp", 15u }
                }
            },
            NightPhaseConfig{
                .Phase = NightPhase::UmbralSurge,
                .StartTimeSec = kDeepNightEndSec,
                .SunIntensity = 0.10f,
                .AmbientIntensity = 0.12f,
                .SpawnIntervalSec = 2.4f,
                .SpawnsPerTick = 2,
                .WeightedPool = {
                    { "enemy_ghoul", 25u },
                    { "enemy_jiangshi", 25u },
                    { "enemy_thrall", 22u },
                    { "enemy_wisp", 28u }
                },
                .OnEnterGuaranteedSpawns = { "enemy_wendigo" }
            },
            NightPhaseConfig{
                .Phase = NightPhase::EclipseClimax,
                .StartTimeSec = kUmbralSurgeEndSec,
                .SunIntensity = 0.06f,
                .AmbientIntensity = 0.12f,
                .OverrideSunColor = true,
                .SunColor = { 0.58f, 0.24f, 0.42f },
                .SpawnIntervalSec = 1.8f,
                .SpawnsPerTick = 2,
                .WeightedPool = {
                    { "enemy_jiangshi", 20u },
                    { "enemy_ghoul", 30u },
                    { "enemy_wisp", 25u },
                    { "enemy_thrall", 25u }
                },
                .OnEnterGuaranteedSpawns = { "enemy_wendigo", "enemy_wendigo", "enemy_jiangshi" }
            },
            NightPhaseConfig{
                .Phase = NightPhase::DawnBreak,
                .StartTimeSec = kDawnBreakSec,
                .TriggersDawnVictory = true
            }
        };
    }

    [[nodiscard]] const NightPhaseConfig* FindPhaseConfig(NightPhase phase) const noexcept {
        for (const auto& cfg : m_PhaseConfigs) {
            if (cfg.Phase == phase) {
                return &cfg;
            }
        }
        return nullptr;
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
    bool m_PhaseConfigsInitialized = false;
    uint32_t m_EnteredPhasesMask = 0u;
    bool m_Victory = false;
    uint32_t m_TotalSpawned = 0;
    uint32_t m_RngState = 0xA511E9B3u;
    std::vector<NightPhaseConfig> m_PhaseConfigs;
    std::vector<uint64_t> m_ActiveBossUUIDs;
    std::unordered_map<std::string, std::shared_ptr<Engine::Prefab>> m_Prefabs;
};

#endif // SANDBOX_WAVE_DIRECTOR_COMPONENT_H
