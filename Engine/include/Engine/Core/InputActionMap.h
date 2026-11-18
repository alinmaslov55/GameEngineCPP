#ifndef CORE_INPUTACTIONMAP_H
#define CORE_INPUTACTIONMAP_H

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Engine {

struct ActionBinding {
    SDL_Scancode PrimaryKey = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode SecondaryKey = SDL_SCANCODE_UNKNOWN;
    int MouseButton = 0; // 0 = none, SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, etc.
    SDL_GamepadButton PadButton = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadAxis PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID; // > 0.5f threshold for LT/RT

    bool operator==(const ActionBinding&) const = default;
};

/// @brief Self-contained action-binding registry and JSON serializer owned by Input.
class InputActionMap {
public:
    static constexpr size_t kMaxMouseButtons = 8; // SDL mouse buttons 1..5 (+ headroom)

    [[nodiscard]] static bool IsValidScancode(SDL_Scancode key) noexcept {
        return key > SDL_SCANCODE_UNKNOWN && static_cast<int>(key) < SDL_SCANCODE_COUNT;
    }

    [[nodiscard]] static bool IsValidMouseButton(int button) noexcept {
        return button >= 1 && static_cast<size_t>(button) < kMaxMouseButtons;
    }

    [[nodiscard]] static bool IsValidGamepadButton(SDL_GamepadButton btn) noexcept {
        return btn > SDL_GAMEPAD_BUTTON_INVALID && static_cast<int>(btn) < SDL_GAMEPAD_BUTTON_COUNT;
    }

    [[nodiscard]] static bool IsValidGamepadAxis(SDL_GamepadAxis axis) noexcept {
        return axis > SDL_GAMEPAD_AXIS_INVALID && static_cast<int>(axis) < SDL_GAMEPAD_AXIS_COUNT;
    }

    void BindAction(std::string_view actionName, const ActionBinding& binding);
    void UnbindAction(std::string_view actionName);
    void Clear() noexcept;
    void RegisterDefaults();

    [[nodiscard]] const ActionBinding* GetBinding(std::string_view actionName) const noexcept;

    [[nodiscard]] std::string SerializeToString() const;
    bool DeserializeFromString(std::string_view jsonStr);
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

private:
    struct TransparentStringHash {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
        size_t operator()(const char* s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    using BindingMap = std::unordered_map<std::string, ActionBinding, TransparentStringHash, std::equal_to<>>;
    BindingMap m_Bindings;
};

} // namespace Engine

#endif // CORE_INPUTACTIONMAP_H
