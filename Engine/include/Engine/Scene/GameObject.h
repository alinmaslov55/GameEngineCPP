#ifndef SCENE_GAMEOBJECT_H
#define SCENE_GAMEOBJECT_H

#include "Engine/Scene/Object.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

namespace Detail {
inline uint64_t GetNextComponentTypeID() {
    static std::atomic<uint64_t> s_Counter{0};
    return s_Counter.fetch_add(1);
}

template<typename T>
inline uint64_t GetComponentTypeMask() {
    static const uint64_t s_Mask = (1ULL << (GetNextComponentTypeID() % 64));
    return s_Mask;
}
} // namespace Detail

class GameObject : public Object {
public:
    explicit GameObject(std::string_view name = "GameObject") : Object(name) {
        ENGINE_CORE_DEBUG("GameObject '{}' created (UUID: {})", GetName(), GetUUID());
        m_Transform = AddComponent<Transform>();
    }

    GameObject(uint64_t uuid, std::string_view name) : Object(uuid, name) {
        ENGINE_CORE_DEBUG("GameObject '{}' created with UUID: {}", GetName(), GetUUID());
        m_Transform = AddComponent<Transform>();
    }

    [[nodiscard]] Transform* GetTransform() noexcept { return m_Transform; }
    [[nodiscard]] const Transform* GetTransform() const noexcept { return m_Transform; }

    [[nodiscard]] uint64_t GetComponentMask() const noexcept { return m_ComponentMask; }

    template<std::derived_from<Component> T>
    [[nodiscard]] bool HasComponent() const {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            return m_Transform != nullptr;
        }
        return (m_ComponentMask & Detail::GetComponentTypeMask<std::remove_cv_t<T>>()) != 0;
    }

    template<std::derived_from<Component> T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = component.get();

        component->m_GameObject = this;

        m_Components.push_back(std::move(component));
        m_ComponentMask |= Detail::GetComponentTypeMask<std::remove_cv_t<T>>();
        ENGINE_CORE_DEBUG("GameObject '{}': Added component", GetName());
        rawPtr->Awake();

        return rawPtr;
    }

    template<std::derived_from<Component> T>
    [[nodiscard]] T* GetComponent() const {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            return m_Transform;
        }
        if (!(m_ComponentMask & Detail::GetComponentTypeMask<std::remove_cv_t<T>>())) {
            return nullptr;
        }
        for (const auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template<std::derived_from<Component> T>
    [[nodiscard]] std::vector<T*> GetComponents() const {
        std::vector<T*> result;
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            if (m_Transform) result.push_back(m_Transform);
            return result;
        }
        if (!(m_ComponentMask & Detail::GetComponentTypeMask<std::remove_cv_t<T>>())) {
            return result;
        }
        for (const auto& component : m_Components) {
            if (T* casted = dynamic_cast<T*>(component.get())) {
                result.push_back(casted);
            }
        }
        return result;
    }

    template<std::derived_from<Component> T>
    bool RemoveComponent() {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            ENGINE_CORE_WARN("GameObject '{}': Cannot remove mandatory Transform component", GetName());
            return false;
        }

        for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
            if (dynamic_cast<T*>(it->get()) != nullptr) {
                (*it)->OnDestroy();
                m_Components.erase(it);
                ENGINE_CORE_DEBUG("GameObject '{}': Removed component", GetName());

                bool hasMore = false;
                for (const auto& comp : m_Components) {
                    if (dynamic_cast<T*>(comp.get())) {
                        hasMore = true;
                        break;
                    }
                }
                if (!hasMore) {
                    m_ComponentMask &= ~Detail::GetComponentTypeMask<std::remove_cv_t<T>>();
                }
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
    uint64_t m_ComponentMask{0};
};
} // namespace Engine

#endif // SCENE_GAMEOBJECT_H
