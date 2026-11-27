#ifndef SANDBOX_WEAPON_ARCHETYPES_H
#define SANDBOX_WEAPON_ARCHETYPES_H

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Scene/GameObject.h>

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace Sandbox {

struct WeaponArchetypeConfig {
    float WindupFraction = 0.25f;
    float StunSec = 0.20f;
    float HitStopSec = 0.030f;
    float Trauma = 0.24f;
    float InnateBurnDPS = 0.0f;
    bool IsFire = false;
    int MaxFrenzyStacks = 0;
    float FrenzyDecaySec = 3.0f;
    const char* SwingSfxPath = "assets/audio/sfx_maul_swing.wav";
};

/// @brief Non-Component strategy interface for weapon-type combat hooks (0 ComponentMask128 bits).
class IWeaponBehavior {
public:
    virtual ~IWeaponBehavior() = default;

    [[nodiscard]] virtual const WeaponArchetypeConfig& GetConfig() const noexcept = 0;
    virtual void OnTargetHit(Engine::GameObject& /*target*/) const {}
    virtual void OnSwingConnected(int& /*frenzyStacks*/, float& /*frenzyDecayTimer*/) const {}
};

class MaulWeaponBehavior final : public IWeaponBehavior {
public:
    [[nodiscard]] const WeaponArchetypeConfig& GetConfig() const noexcept override {
        static constexpr WeaponArchetypeConfig kConfig{
            0.28f, 0.25f, 0.045f, 0.38f, 0.0f, false, 0, 0.0f, "assets/audio/sfx_maul_swing.wav"
        };
        return kConfig;
    }
};

class HalberdWeaponBehavior final : public IWeaponBehavior {
public:
    [[nodiscard]] const WeaponArchetypeConfig& GetConfig() const noexcept override {
        static constexpr WeaponArchetypeConfig kConfig{
            0.20f, 0.35f, 0.030f, 0.24f, 0.0f, false, 0, 0.0f, "assets/audio/sfx_maul_swing.wav"
        };
        return kConfig;
    }

    void OnTargetHit(Engine::GameObject& target) const override {
        if (auto* targetRb = target.GetComponent<Engine::Rigidbody2D>();
            targetRb && targetRb->VerticalVelocity > 0.0f) {
            // Interrupt hopping enemies (e.g. Jiangshi) mid-air
            targetRb->VerticalVelocity = 0.0f;
        }
    }
};

class SickleWeaponBehavior final : public IWeaponBehavior {
public:
    [[nodiscard]] const WeaponArchetypeConfig& GetConfig() const noexcept override {
        static constexpr WeaponArchetypeConfig kConfig{
            0.15f, 0.10f, 0.015f, 0.12f, 4.0f, true, 5, 3.0f, "assets/audio/sfx_maul_swing.wav"
        };
        return kConfig;
    }

    void OnSwingConnected(int& frenzyStacks, float& frenzyDecayTimer) const override {
        const auto& cfg = GetConfig();
        frenzyStacks = std::min(cfg.MaxFrenzyStacks, frenzyStacks + 1);
        frenzyDecayTimer = cfg.FrenzyDecaySec;
    }
};

class WeaponArchetypeRegistry {
public:
    [[nodiscard]] static const IWeaponBehavior& Resolve(std::string_view weaponType) {
        static const MaulWeaponBehavior s_Maul;
        static const HalberdWeaponBehavior s_Halberd;
        static const SickleWeaponBehavior s_Sickle;

        static const std::unordered_map<std::string_view, const IWeaponBehavior*> kByType = {
            { "Maul", &s_Maul },
            { "Halberd", &s_Halberd },
            { "Sickle", &s_Sickle },
        };
        const auto it = kByType.find(weaponType);
        return (it != kByType.end()) ? *it->second : static_cast<const IWeaponBehavior&>(s_Maul);
    }
};

} // namespace Sandbox

#endif // SANDBOX_WEAPON_ARCHETYPES_H
