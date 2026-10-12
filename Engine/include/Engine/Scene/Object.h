#ifndef SCENE_OBJECT_H
#define SCENE_OBJECT_H

#include <string>

namespace Engine {
class Object {
public:
    Object(const std::string& name = "Object") : m_Name(name) {}
    virtual ~Object() = default;

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

private:
    std::string m_Name;
};
}

#endif // SCENE_OBJECT_H
