#ifndef SCENE_GAMEOBJECT_H
#define SCENE_GAMEOBJECT_H

#include "Engine/Scene/Object.h"
#include "Engine/Scene/Component.h"
#include <vector>
#include <memory>
#include <type_traits>

namespace Engine {
    class GameObject : public Object {
    public:
        GameObject(const std::string& name = "GameObject") : Object(name) {}

        template<typename T, typename... Args>
        T* AddComponent(Args&&... args) {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");

            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* rawPtr = component.get();

            component->m_GameObject = this;

            m_Components.push_back(std::move(component));
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

        void Update() {
            for (auto& component : m_Components) {
                component->Update();
            }
        }

    private:
        std::vector<std::unique_ptr<Component>> m_Components;
    };
}

#endif // SCENE_GAMEOBJECT_H
