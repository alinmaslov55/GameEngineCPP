#ifndef SCENE_OBJECT_H
#define SCENE_OBJECT_H

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine {
class Object {
public:
    explicit Object(std::string_view name = "Object");
    Object(uint64_t uuid, std::string_view name);
    virtual ~Object() = default;

    [[nodiscard]] uint64_t GetUUID() const noexcept { return m_UUID; }

    [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
    void SetName(std::string_view name) { m_Name = name; }

private:
    uint64_t m_UUID;
    std::string m_Name;
};
} // namespace Engine

#endif // SCENE_OBJECT_H
