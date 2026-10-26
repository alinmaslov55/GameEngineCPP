#ifndef SCENE_OBJECT_H
#define SCENE_OBJECT_H

#include <cstdint>
#include <string>

namespace Engine {
class Object {
public:
    Object(const std::string& name = "Object");
    Object(uint64_t uuid, const std::string& name);
    virtual ~Object() = default;

    uint64_t GetUUID() const { return m_UUID; }

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

private:
    uint64_t m_UUID;
    std::string m_Name;
};
} // namespace Engine

#endif // SCENE_OBJECT_H
