#ifndef SCENE_GAMEOBJECT_H
#define SCENE_GAMEOBJECT_H

#include "Engine/Scene/Object.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <vector>
#include <memory>
#include <type_traits>

namespace Engine {
class GameObject : public Object {
public:
    GameObject(const std::string& name = "GameObject") : Object(name) {
        ENGINE_CORE_DEBUG("GameObject '{}' created", GetName());
        AddComponent<Transform>();
    }

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = component.get();

        component->m_GameObject = this;

        m_Components.push_back(std::move(component));
        ENGINE_CORE_DEBUG("GameObject '{}': Added component", GetName());
        rawPtr->Awake();

        return rawPtr;
    }

    template<typename T>
    T* GetComponent() {
        for (auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template<typename T>
    std::vector<T*> GetComponents() {
        std::vector<T*> result;
        for (auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                result.push_back(casted);
            }
        }
        return result;
    }

    void Update(float deltaTime) {
        ENGINE_CORE_TRACE("GameObject '{}': Updating components", GetName());
        for (auto& component : m_Components) {
            component->Update(deltaTime);
        }
    }

private:
    std::vector<std::unique_ptr<Component>> m_Components;
};
} // namespace Engine

#endif // SCENE_GAMEOBJECT_H