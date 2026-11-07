#ifndef SCENE_PREFAB_H
#define SCENE_PREFAB_H

#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace Engine {

class GameObject;

class Prefab {
public:
    Prefab() = default;
    explicit Prefab(nlohmann::json rootHierarchyJson);

    static std::shared_ptr<Prefab> LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    // Snapshot any live scene entity hierarchy (root + all descendants)
    static std::shared_ptr<Prefab> CreateFromGameObject(const GameObject* root, bool zeroRootPosition = true);

    [[nodiscard]] const nlohmann::json& GetJson() const noexcept { return m_Json; }
    [[nodiscard]] bool IsValid() const noexcept;

private:
    nlohmann::json m_Json;
};

} // namespace Engine

#endif // SCENE_PREFAB_H
