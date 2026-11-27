#ifndef SANDBOX_RUN_PROGRESSION_COMPONENT_H
#define SANDBOX_RUN_PROGRESSION_COMPONENT_H

#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/WeaponController.h"

#include <Engine/Core/Input.h>
#include <Engine/Core/Time.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/UI/UIButtonComponent.h>
#include <Engine/UI/UICanvas.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

class RunProgressionComponent final : public Engine::Component {
public:
    static constexpr float kWeightCommon = 60.0f;
    static constexpr float kWeightRare = 28.0f;
    static constexpr float kWeightEpic = 10.0f;
    static constexpr float kWeightLegendary = 2.0f;

    [[nodiscard]] static int ComputeThresholdForLevel(int level) noexcept {
        // Level 1 -> 15, Level 2 -> 35, Level 3 -> 65, Level 4 -> 105, ...
        if (level <= 1) {
            return 15;
        }
        int threshold = 15;
        for (int l = 2; l <= level; ++l) {
            threshold += 10 * l;
        }
        return threshold;
    }

    [[nodiscard]] static float GetRarityWeight(const std::string& rarity) noexcept {
        if (rarity == "Legendary") return kWeightLegendary;
        if (rarity == "Epic")      return kWeightEpic;
        if (rarity == "Rare")      return kWeightRare;
        return kWeightCommon;
    }

    [[nodiscard]] int GetLevel() const noexcept { return m_Level; }
    [[nodiscard]] int GetNextLevelThreshold() const noexcept { return ComputeThresholdForLevel(m_Level + 1); }
    [[nodiscard]] bool IsDraftOpen() const noexcept { return m_DraftOpen; }
    [[nodiscard]] const std::vector<Engine::BranchDefinition>& GetCurrentCards() const noexcept { return m_CurrentCards; }
    [[nodiscard]] const std::vector<Engine::GameObject*>& GetCardButtons() const noexcept { return m_CardButtonObjects; }

    void Update(float /*deltaTime*/) override {
        auto* go = GetGameObject();
        auto* stats = go ? go->GetComponent<PlayerStatsComponent>() : nullptr;
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !stats || !scene) {
            return;
        }

        if (!m_DraftOpen && stats->XP >= GetNextLevelThreshold()) {
            OpenDraft(*scene);
            return;
        }

        if (m_DraftOpen) {
            if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_LEFT) ||
                Engine::Input::IsKeyJustPressed(SDL_SCANCODE_A)) {
                Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Left, *scene);
            } else if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_RIGHT) ||
                       Engine::Input::IsKeyJustPressed(SDL_SCANCODE_D)) {
                Engine::UIButtonComponent::NavigateFocus(Engine::UIFocusDirection::Right, *scene);
            } else if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_RETURN) ||
                       Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE)) {
                Engine::UIButtonComponent::SubmitActiveFocus();
            } else if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_1)) {
                SelectCard(0);
            } else if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_2)) {
                SelectCard(1);
            } else if (Engine::Input::IsKeyJustPressed(SDL_SCANCODE_3)) {
                SelectCard(2);
            }
        }
    }

    bool SelectCard(size_t cardIndex) {
        if (!m_DraftOpen || cardIndex >= m_CurrentCards.size()) {
            return false;
        }
        const Engine::BranchDefinition chosen = m_CurrentCards[cardIndex];
        ApplyBranchCard(chosen);
        CloseDraft();
        return true;
    }

    void ApplyBranchCard(const Engine::BranchDefinition& card) {
        auto* go = GetGameObject();
        if (!go) {
            return;
        }
        auto* stats = go->GetComponent<PlayerStatsComponent>();
        if (!stats) {
            stats = go->AddComponent<PlayerStatsComponent>();
        }

        const std::string& mod = card.StatModifier;
        const float val = card.Value;

        if (mod == "AttackRate") {
            stats->AttackSpeedMultiplier += val;
        } else if (mod == "MaxHealth") {
            stats->MaxHP += val;
            stats->Heal(val);
        } else if (mod == "AuraDamage") {
            if (auto* torch = ResolveTorch(*go)) {
                torch->AuraDamage += val;
            }
        } else if (mod == "MoveSpeed") {
            stats->MoveSpeedMultiplier += val * 0.1f;
        } else if (mod == "AshAttractionRange") {
            stats->AshAttractionRange += val;
        } else if (mod == "KnockbackResistance") {
            if (auto* health = go->GetComponent<HealthComponent>()) {
                health->KnockbackResistance = std::min(1.0f, health->KnockbackResistance + val);
            }
        } else if (mod == "BurnDamageOnHit") {
            stats->BurnDamageOnHit += val;
        } else if (mod == "FuelOnKill") {
            stats->FuelOnKill += val;
        } else if (mod == "SwingArcDeg") {
            if (auto* wc = go->GetComponent<WeaponController>()) {
                wc->AddSwingArcBonus(val);
            }
        } else if (mod == "HealOnBurnKill") {
            stats->HealOnBurnKill += val;
        } else if (mod == "BlindDuration") {
            stats->SolarFlareBlindSec += val;
        } else if (mod == "QuakeRadius") {
            stats->QuakeRadius = std::max(stats->QuakeRadius, val);
        } else if (mod == "BeaconRegenRate") {
            stats->BeaconRegenRate += val;
        }
    }

    void OnDestroy() override {
        if (m_DraftOpen) {
            CloseDraft();
        }
    }

private:
    void OpenDraft(Engine::Scene& scene) {
        ++m_Level;
        m_DraftOpen = true;
        m_SavedTimeScale = Engine::Time::GetBaseTimeScale();
        Engine::Time::SetTimeScale(0.0f);

        RollThreeDistinctCards();
        BuildDraftOverlay(scene);
    }

    void CloseDraft() {
        m_DraftOpen = false;
        DestroyDraftOverlay();
        Engine::Time::SetTimeScale(m_SavedTimeScale > 0.0f ? m_SavedTimeScale : 1.0f);
    }

    void RollThreeDistinctCards() {
        m_CurrentCards.clear();
        std::vector<Engine::BranchDefinition> pool = BuildCandidatePool();

        for (int pick = 0; pick < 3 && !pool.empty(); ++pick) {
            float totalWeight = 0.0f;
            for (const auto& def : pool) {
                totalWeight += GetRarityWeight(def.Rarity);
            }
            float roll = NextFloat01() * totalWeight;
            size_t chosenIdx = 0;
            for (size_t i = 0; i < pool.size(); ++i) {
                roll -= GetRarityWeight(pool[i].Rarity);
                if (roll <= 0.0f) {
                    chosenIdx = i;
                    break;
                }
            }
            m_CurrentCards.push_back(pool[chosenIdx]);
            pool.erase(pool.begin() + static_cast<ptrdiff_t>(chosenIdx));
        }
    }

    [[nodiscard]] static std::vector<Engine::BranchDefinition> BuildCandidatePool() {
        if (Engine::GameplayData::IsInitialized()) {
            const auto& all = Engine::GameplayData::Branches().GetAll();
            if (all.size() >= 3u) {
                return all;
            }
        }
        return {
            { "branch_blade_dance", "Blade Dance", "Faster weapon swings.", "Common", {}, "AttackRate", 0.15f, "" },
            { "branch_thick_bark", "Thick Bark", "+25 Max Health.", "Common", {}, "MaxHealth", 25.0f, "" },
            { "branch_ignited_edges", "Ignited Edges", "Melee hits ignite enemies.", "Rare", {}, "BurnDamageOnHit", 6.0f, "" },
            { "branch_cleaving_momentum", "Cleaving Momentum", "+25 deg Swing Arc.", "Rare", {}, "SwingArcDeg", 25.0f, "" },
            { "branch_solar_flare", "Solar Flare", "Stoking blinds nearby enemies.", "Epic", {}, "BlindDuration", 2.0f, "" },
            { "branch_eternal_beacon", "Eternal Beacon", "Torch regenerates fuel.", "Legendary", {}, "BeaconRegenRate", 1.5f, "" }
        };
    }

    void BuildDraftOverlay(Engine::Scene& scene) {
        DestroyDraftOverlay();

        m_OverlayRoot = scene.CreateGameObject("BranchDraftOverlay");
        auto* backdrop = m_OverlayRoot->AddComponent<Engine::UIPanel>();
        backdrop->color = { 0.04f, 0.04f, 0.08f, 0.78f };
        backdrop->useRectTransform = true;
        backdrop->rectTransform.Anchor = Engine::UIAnchor::Center;
        backdrop->rectTransform.Pivot = { 0.5f, 0.5f };
        backdrop->rectTransform.Size = { 1920.0f, 1080.0f };
        backdrop->rectTransform.ZOrder = 200;
        backdrop->rectTransform.IgnoreSafeArea = true;

        for (size_t i = 0; i < m_CurrentCards.size(); ++i) {
            const auto& card = m_CurrentCards[i];
            auto* cardGo = scene.CreateGameObject("BranchCard_" + std::to_string(i));
            cardGo->GetTransform()->SetParent(m_OverlayRoot->GetTransform(), false);

            auto* panel = cardGo->AddComponent<Engine::UIPanel>();
            panel->color = RarityTint(card.Rarity);
            panel->useRectTransform = true;
            panel->rectTransform.Anchor = Engine::UIAnchor::Center;
            panel->rectTransform.Pivot = { 0.5f, 0.5f };
            panel->rectTransform.Size = { 220.0f, 300.0f };
            panel->rectTransform.AnchoredOffset = { (static_cast<float>(i) - 1.0f) * 250.0f, 0.0f };
            panel->rectTransform.ZOrder = 201;

            auto* label = cardGo->AddComponent<Engine::UILabel>();
            label->text = card.Name + " [" + card.Rarity + "]";
            label->scale = 1.0f;

            auto* btn = cardGo->AddComponent<Engine::UIButtonComponent>();
            btn->OnClick = [this, i]() {
                SelectCard(i);
            };
            m_CardButtonObjects.push_back(cardGo);
        }

        // Wire directional horizontal focus navigation across the 3 cards
        for (size_t i = 0; i < m_CardButtonObjects.size(); ++i) {
            auto* btn = m_CardButtonObjects[i]->GetComponent<Engine::UIButtonComponent>();
            const uint64_t leftUUID = (i > 0) ? m_CardButtonObjects[i - 1]->GetUUID() : 0;
            const uint64_t rightUUID = (i + 1 < m_CardButtonObjects.size()) ? m_CardButtonObjects[i + 1]->GetUUID() : 0;
            btn->SetNeighbors(0, 0, leftUUID, rightUUID);
        }
        // Clear any stale focus so first NavigateFocus focuses card 0
        Engine::UIButtonComponent::SetFocusedButton(nullptr);
    }

    void DestroyDraftOverlay() {
        m_CardButtonObjects.clear();
        if (m_OverlayRoot) {
            if (auto* scene = m_OverlayRoot->GetScene()) {
                scene->DestroyGameObject(m_OverlayRoot);
            }
            m_OverlayRoot = nullptr;
        }
    }

    [[nodiscard]] static glm::vec4 RarityTint(const std::string& rarity) noexcept {
        if (rarity == "Legendary") return { 0.92f, 0.68f, 0.16f, 0.95f };
        if (rarity == "Epic")      return { 0.62f, 0.24f, 0.85f, 0.95f };
        if (rarity == "Rare")      return { 0.20f, 0.52f, 0.92f, 0.95f };
        return { 0.22f, 0.24f, 0.28f, 0.95f };
    }

    [[nodiscard]] static TorchComponent* ResolveTorch(Engine::GameObject& player) noexcept {
        for (Engine::Transform* child : player.GetTransform()->GetChildren()) {
            if (child && child->GetGameObject()) {
                if (auto* t = child->GetGameObject()->GetComponent<TorchComponent>()) {
                    return t;
                }
            }
        }
        return nullptr;
    }

    float NextFloat01() noexcept {
        m_RngState ^= m_RngState << 13;
        m_RngState ^= m_RngState >> 17;
        m_RngState ^= m_RngState << 5;
        return static_cast<float>(m_RngState & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    int m_Level = 0;
    bool m_DraftOpen = false;
    float m_SavedTimeScale = 1.0f;
    uint32_t m_RngState = 0x51C8A231u;
    Engine::GameObject* m_OverlayRoot = nullptr;
    std::vector<Engine::BranchDefinition> m_CurrentCards;
    std::vector<Engine::GameObject*> m_CardButtonObjects;
};

#endif // SANDBOX_RUN_PROGRESSION_COMPONENT_H
