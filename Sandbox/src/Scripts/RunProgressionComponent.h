#ifndef SANDBOX_RUN_PROGRESSION_COMPONENT_H
#define SANDBOX_RUN_PROGRESSION_COMPONENT_H

#include "Scripts/AudioCueHelper.h"
#include "Scripts/HealthComponent.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"
#include "Scripts/StatModifierRegistry.h"
#include "Scripts/TorchComponent.h"
#include "Scripts/UIOverlaySystem.h"
#include "Scripts/WeaponController.h"

#include <Engine/Core/Input.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>

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
    [[nodiscard]] bool IsDraftOpen() const noexcept { return m_DraftOverlay.IsOpen(); }
    [[nodiscard]] const std::vector<Engine::BranchDefinition>& GetCurrentCards() const noexcept { return m_CurrentCards; }
    [[nodiscard]] const std::vector<Engine::GameObject*>& GetCardButtons() const noexcept { return m_DraftOverlay.GetCardButtons(); }

    void Update(float /*deltaTime*/) override {
        auto* go = GetGameObject();
        auto* stats = go ? go->GetComponent<PlayerStatsComponent>() : nullptr;
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !stats || !scene) {
            return;
        }

        if (!m_DraftOverlay.IsOpen() && stats->XP >= GetNextLevelThreshold()) {
            OpenDraft(*scene);
            return;
        }

        if (m_DraftOverlay.IsOpen()) {
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
        if (!m_DraftOverlay.IsOpen() || cardIndex >= m_CurrentCards.size()) {
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
        if (!go->GetComponent<PlayerStatsComponent>()) {
            go->AddComponent<PlayerStatsComponent>();
        }
        Sandbox::StatModifierRegistry::Apply(*go, card.StatModifier, card.Value, StatLayer::Run);
    }

    void OnDestroy() override {
        if (m_DraftOverlay.IsOpen()) {
            CloseDraft();
        }
    }

private:
    void OpenDraft(Engine::Scene& scene) {
        ++m_Level;
        RollThreeDistinctCards();
        m_DraftOverlay.Cards = m_CurrentCards;
        m_DraftOverlay.OnCardSelected = [this](size_t cardIdx) {
            SelectCard(cardIdx);
        };
        m_DraftOverlay.Open(scene);

        Engine::SoundPlayParams levelParams;
        levelParams.Bus = Engine::AudioBus::UI;
        levelParams.PitchMin = 1.0f;
        levelParams.PitchMax = 1.0f;
        Sandbox::PlaySfxCue("assets/audio/sfx_level_up.wav", levelParams);
    }

    void CloseDraft() {
        auto* go = GetGameObject();
        m_DraftOverlay.Close(go ? go->GetScene() : nullptr);
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
    uint32_t m_RngState = 0x51C8A231u;
    Sandbox::UI::BranchDraftOverlay m_DraftOverlay;
    std::vector<Engine::BranchDefinition> m_CurrentCards;
};

#endif // SANDBOX_RUN_PROGRESSION_COMPONENT_H
