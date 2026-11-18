#include "Engine/Core/InputActionMap.h"
#include "Engine/Utils/Logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <utility>

namespace Engine {

namespace {

nlohmann::json SerializeBinding(const ActionBinding& binding) {
    nlohmann::json j;
    j["primaryKey"] = static_cast<int>(binding.PrimaryKey);
    if (InputActionMap::IsValidScancode(binding.PrimaryKey)) {
        if (const char* name = SDL_GetScancodeName(binding.PrimaryKey); name && name[0] != '\0') {
            j["primaryKeyName"] = name;
        }
    }
    j["secondaryKey"] = static_cast<int>(binding.SecondaryKey);
    if (InputActionMap::IsValidScancode(binding.SecondaryKey)) {
        if (const char* name = SDL_GetScancodeName(binding.SecondaryKey); name && name[0] != '\0') {
            j["secondaryKeyName"] = name;
        }
    }
    j["mouseButton"] = binding.MouseButton;

    const char* padBtnName = InputActionMap::IsValidGamepadButton(binding.PadButton)
        ? SDL_GetGamepadStringForButton(binding.PadButton)
        : nullptr;
    j["padButton"] = (padBtnName && padBtnName[0] != '\0') ? std::string(padBtnName) : std::string();

    const char* padAxisName = InputActionMap::IsValidGamepadAxis(binding.PadTriggerAxis)
        ? SDL_GetGamepadStringForAxis(binding.PadTriggerAxis)
        : nullptr;
    j["padTriggerAxis"] = (padAxisName && padAxisName[0] != '\0') ? std::string(padAxisName) : std::string();
    return j;
}

SDL_Scancode ParseScancodeField(const nlohmann::json& j, const char* idKey, const char* nameKey) {
    if (j.contains(idKey)) {
        const auto& val = j.at(idKey);
        if (val.is_number_integer()) {
            const int code = val.get<int>();
            if (code >= 0 && code < SDL_SCANCODE_COUNT) {
                return static_cast<SDL_Scancode>(code);
            }
        } else if (val.is_string()) {
            const std::string& name = val.get_ref<const std::string&>();
            if (!name.empty()) {
                return SDL_GetScancodeFromName(name.c_str());
            }
        }
    }
    if (nameKey && j.contains(nameKey) && j.at(nameKey).is_string()) {
        const std::string& name = j.at(nameKey).get_ref<const std::string&>();
        if (!name.empty()) {
            return SDL_GetScancodeFromName(name.c_str());
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

SDL_GamepadButton ParseGamepadButtonField(const nlohmann::json& j) {
    if (!j.contains("padButton")) {
        return SDL_GAMEPAD_BUTTON_INVALID;
    }
    const auto& val = j.at("padButton");
    if (val.is_string()) {
        const std::string& str = val.get_ref<const std::string&>();
        return str.empty() ? SDL_GAMEPAD_BUTTON_INVALID : SDL_GetGamepadButtonFromString(str.c_str());
    }
    if (val.is_number_integer()) {
        const int id = val.get<int>();
        if (id >= 0 && id < SDL_GAMEPAD_BUTTON_COUNT) {
            return static_cast<SDL_GamepadButton>(id);
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

SDL_GamepadAxis ParseGamepadAxisField(const nlohmann::json& j) {
    if (!j.contains("padTriggerAxis")) {
        return SDL_GAMEPAD_AXIS_INVALID;
    }
    const auto& val = j.at("padTriggerAxis");
    if (val.is_string()) {
        const std::string& str = val.get_ref<const std::string&>();
        return str.empty() ? SDL_GAMEPAD_AXIS_INVALID : SDL_GetGamepadAxisFromString(str.c_str());
    }
    if (val.is_number_integer()) {
        const int id = val.get<int>();
        if (id >= 0 && id < SDL_GAMEPAD_AXIS_COUNT) {
            return static_cast<SDL_GamepadAxis>(id);
        }
    }
    return SDL_GAMEPAD_AXIS_INVALID;
}

} // namespace

void InputActionMap::BindAction(std::string_view actionName, const ActionBinding& binding) {
    if (actionName.empty()) {
        return;
    }
    auto it = m_Bindings.find(actionName);
    if (it != m_Bindings.end()) {
        it->second = binding;
    } else {
        m_Bindings.emplace(std::string(actionName), binding);
    }
}

void InputActionMap::UnbindAction(std::string_view actionName) {
    if (auto it = m_Bindings.find(actionName); it != m_Bindings.end()) {
        m_Bindings.erase(it);
    }
}

void InputActionMap::Clear() noexcept {
    m_Bindings.clear();
}

void InputActionMap::RegisterDefaults() {
    BindAction("Jump", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_SPACE,
        .PadButton = SDL_GAMEPAD_BUTTON_SOUTH
    });
    BindAction("Attack", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_J,
        .MouseButton = SDL_BUTTON_LEFT,
        .PadButton = SDL_GAMEPAD_BUTTON_WEST
    });
    BindAction("Dodge", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_LSHIFT,
        .PadButton = SDL_GAMEPAD_BUTTON_EAST
    });
    BindAction("Interact", ActionBinding{
        .PrimaryKey = SDL_SCANCODE_E,
        .PadButton = SDL_GAMEPAD_BUTTON_NORTH
    });
}

const ActionBinding* InputActionMap::GetBinding(std::string_view actionName) const noexcept {
    const auto it = m_Bindings.find(actionName);
    return (it != m_Bindings.end()) ? &it->second : nullptr;
}

std::string InputActionMap::SerializeToString() const {
    nlohmann::json root;
    nlohmann::json bindingsJson = nlohmann::json::object();
    for (const auto& [name, binding] : m_Bindings) {
        bindingsJson[name] = SerializeBinding(binding);
    }
    root["bindings"] = std::move(bindingsJson);
    return root.dump(2);
}

bool InputActionMap::DeserializeFromString(std::string_view jsonStr) {
    if (jsonStr.empty()) {
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr.begin(), jsonStr.end());
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("Failed to parse input bindings JSON: {}", e.what());
        return false;
    }

    if (!root.is_object() || !root.contains("bindings") || !root.at("bindings").is_object()) {
        ENGINE_CORE_ERROR("Input bindings JSON missing 'bindings' object");
        return false;
    }

    BindingMap parsed;
    for (const auto& [actionName, entry] : root.at("bindings").items()) {
        if (!entry.is_object() || actionName.empty()) {
            continue;
        }
        ActionBinding binding;
        binding.PrimaryKey = ParseScancodeField(entry, "primaryKey", "primaryKeyName");
        binding.SecondaryKey = ParseScancodeField(entry, "secondaryKey", "secondaryKeyName");
        if (entry.contains("mouseButton") && entry.at("mouseButton").is_number_integer()) {
            const int btn = entry.at("mouseButton").get<int>();
            binding.MouseButton = IsValidMouseButton(btn) ? btn : 0;
        }
        binding.PadButton = ParseGamepadButtonField(entry);
        binding.PadTriggerAxis = ParseGamepadAxisField(entry);
        parsed.emplace(actionName, binding);
    }

    m_Bindings = std::move(parsed);
    return true;
}

bool InputActionMap::SaveToFile(const std::string& filepath) const {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        ENGINE_CORE_ERROR("Failed to open input bindings file for writing: {}", filepath);
        return false;
    }
    const std::string data = SerializeToString();
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool InputActionMap::LoadFromFile(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        ENGINE_CORE_ERROR("Failed to open input bindings file for reading: {}", filepath);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return DeserializeFromString(ss.str());
}

} // namespace Engine
