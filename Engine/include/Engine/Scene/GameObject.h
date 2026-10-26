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
        ENGINE_CORE_DEBUG("GameObject '{}' created (UUID: {})", GetName(), GetUUID());
        m_Transform = AddComponent<Transform>();
    }

    GameObject(uint64_t uuid, const std::string& name) : Object(uuid, name) {
        ENGINE_CORE_DEBUG("GameObject '{}' created with UUID: {}", GetName(), GetUUID());
        m_Transform = AddComponent<Transform>();
    }

    Transform* GetTransform() { return m_Transform; }
    const Transform* GetTransform() const { return m_Transform; }

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
    T* GetComponent() const {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            return m_Transform;
        }
        for (const auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template<typename T>
    std::vector<T*> GetComponents() const {
        std::vector<T*> result;
        for (const auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                result.push_back(casted);
            }
        }
        return result;
    }

    template<typename T>
    bool RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            ENGINE_CORE_WARN("GameObject '{}': Cannot remove mandatory Transform component", GetName());
            return false;
        }

        for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
            if (dynamic_cast<T*>(it->get()) != nullptr) {
                (*it)->OnDestroy();
                m_Components.erase(it);
                ENGINE_CORE_DEBUG("GameObject '{}': Removed component", GetName());
                return true;
            }
        }
        return false;
    }

    void Update(float deltaTime) {
        ENGINE_CORE_TRACE("GameObject '{}': Updating components", GetName());
        for (auto& component : m_Components) {
            component->Update(deltaTime);
        }
    }

private:
    Transform* m_Transform{nullptr};
    std::vector<std::unique_ptr<Component>> m_Components;
};
} // namespace Engine

#endif // SCENE_GAMEOBJECT_H
