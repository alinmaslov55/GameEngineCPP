#ifndef SCENE_GAMEOBJECT_H
#define SCENE_GAMEOBJECT_H

#include "Engine/Scene/Object.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/ComponentMask128.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Utils/Logger.h"

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

class Scene;
class Rigidbody2D;
class Collider2D;
class BoxCollider2D;
class CircleCollider2D;
class CapsuleCollider2D;
class Tilemap;
struct Collision2DInfo;

enum class EntityLifecycle : uint8_t {
    PendingSpawn = 0,
    Alive,
    PendingDestroy,
    Destroyed
};

namespace Detail {
inline uint32_t GetNextComponentTypeID() {
    static std::atomic<uint32_t> s_Counter{0};
    uint32_t id = s_Counter.fetch_add(1);
    assert(id < 128 && "Exceeded maximum allowed 128 unique Component types in ComponentMask128!");
    return id;
}

template<typename T>
inline ComponentMask128 GetComponentTypeMask() {
    static const ComponentMask128 s_Mask = []() {
        uint32_t id = GetNextComponentTypeID();
        return ComponentMask128::FromBit(id);
    }();
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

    [[nodiscard]] Scene* GetScene() const noexcept { return m_Scene; }

    [[nodiscard]] ComponentMask128 GetComponentMask() const noexcept { return m_ComponentMask; }

    void SetTag(EntityTag tag) noexcept { m_Tag = tag; }
    [[nodiscard]] EntityTag GetTag() const noexcept { return m_Tag; }
    [[nodiscard]] bool HasTag(EntityTag tag) const noexcept { return m_Tag == tag; }

    void SetLayer(uint32_t layerMask) noexcept { m_LayerMask = layerMask; }
    [[nodiscard]] uint32_t GetLayer() const noexcept { return m_LayerMask; }
    [[nodiscard]] bool IsInLayer(uint32_t layerMask) const noexcept { return (m_LayerMask & layerMask) != 0; }

    template<std::derived_from<Component> T>
    [[nodiscard]] bool HasComponent() const {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            return m_Transform != nullptr;
        }
        return m_ComponentMask.Intersects(Detail::GetComponentTypeMask<std::remove_cv_t<T>>());
    }

    template<std::derived_from<Component> T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = component.get();

        component->m_GameObject = this;

        m_Components.push_back(std::move(component));
        m_ComponentMask |= Detail::GetComponentTypeMask<std::remove_cv_t<T>>();
        if constexpr (std::is_same_v<std::remove_cv_t<T>, BoxCollider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, CircleCollider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, CapsuleCollider2D>) {
            m_ComponentMask |= Detail::GetComponentTypeMask<Collider2D>();
        }
        ENGINE_CORE_DEBUG("GameObject '{}': Added component", GetName());
        rawPtr->Awake();
        if (IsActiveInHierarchy() && rawPtr->IsEnabled()) {
            rawPtr->OnEnable();
        }

        if constexpr (std::is_same_v<std::remove_cv_t<T>, Rigidbody2D> ||
                      std::is_same_v<std::remove_cv_t<T>, Collider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, BoxCollider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, CircleCollider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, CapsuleCollider2D> ||
                      std::is_same_v<std::remove_cv_t<T>, Tilemap>) {
            NotifyPhysicsComponentAdded();
        }

        return rawPtr;
    }

    void SetActive(bool active) noexcept {
        if (m_Active == active) return;
        m_Active = active;
        NotifyHierarchyActiveChanged();
    }

    [[nodiscard]] bool IsIteratingComponents() const noexcept { return m_ComponentIterationDepth > 0; }
    void FlushPendingComponentRemovals();

    class ScopedComponentIterationGuard {
    public:
        explicit ScopedComponentIterationGuard(GameObject& go) noexcept : m_Go(go) {
            ++m_Go.m_ComponentIterationDepth;
        }
        ~ScopedComponentIterationGuard() {
            if (m_Go.m_ComponentIterationDepth > 0) {
                --m_Go.m_ComponentIterationDepth;
                if (m_Go.m_ComponentIterationDepth == 0) {
                    m_Go.FlushPendingComponentRemovals();
                }
            }
        }
        ScopedComponentIterationGuard(const ScopedComponentIterationGuard&) = delete;
        ScopedComponentIterationGuard& operator=(const ScopedComponentIterationGuard&) = delete;
        ScopedComponentIterationGuard(ScopedComponentIterationGuard&&) = delete;
        ScopedComponentIterationGuard& operator=(ScopedComponentIterationGuard&&) = delete;

    private:
        GameObject& m_Go;
    };

    void NotifyHierarchyActiveChanged() noexcept {
        const bool activeInHier = IsActiveInHierarchy();
        {
            ScopedComponentIterationGuard guard(*this);
            for (size_t i = 0; i < m_Components.size(); ++i) {
                auto* comp = m_Components[i].get();
                if (!comp || comp->IsPendingRemoval()) continue;
                if (comp->IsEnabled()) {
                    if (activeInHier) {
                        comp->OnEnable();
                    } else {
                        comp->OnDisable();
                    }
                }
            }
        }
        if (m_Transform) {
            for (auto* child : m_Transform->GetChildren()) {
                if (child && child->GetGameObject()) {
                    child->GetGameObject()->NotifyHierarchyActiveChanged();
                }
            }
        }
    }

    [[nodiscard]] bool IsActive() const noexcept { return m_Active; }

    void DispatchCollisionEnter2D(const Collision2DInfo& info);
    void DispatchCollisionExit2D(GameObject* other);
    void DispatchTriggerEnter2D(GameObject* other);
    void DispatchTriggerExit2D(GameObject* other);

    void SetPoolKey(std::string_view key) { m_PoolKey = key; }
    [[nodiscard]] const std::string& GetPoolKey() const noexcept { return m_PoolKey; }
    [[nodiscard]] bool IsPooled() const noexcept { return !m_PoolKey.empty(); }

    [[nodiscard]] bool IsInFreeList() const noexcept { return m_InFreeList; }
    void SetInFreeList(bool inFreeList) noexcept { m_InFreeList = inFreeList; }

    void OnPoolAcquire();
    void OnPoolRelease();

    [[nodiscard]] bool IsActiveInHierarchy() const noexcept {
        if (!m_Active || IsPendingDestroy()) {
            return false;
        }
        if (m_Transform && m_Transform->GetParent()) {
            if (const auto* parentGo = m_Transform->GetParent()->GetGameObject()) {
                return parentGo->IsActiveInHierarchy();
            }
        }
        return true;
    }

    [[nodiscard]] EntityLifecycle GetLifecycleState() const noexcept { return m_LifecycleState; }
    [[nodiscard]] bool IsPendingSpawn() const noexcept { return m_LifecycleState == EntityLifecycle::PendingSpawn; }
    [[nodiscard]] bool IsAlive() const noexcept { return m_LifecycleState == EntityLifecycle::Alive; }
    [[nodiscard]] bool IsPendingDestroy() const noexcept {
        return m_LifecycleState == EntityLifecycle::PendingDestroy || m_LifecycleState == EntityLifecycle::Destroyed;
    }
    [[nodiscard]] bool IsDestroyed() const noexcept { return m_LifecycleState == EntityLifecycle::Destroyed; }

    void ProcessPendingStarts() {
        if (!IsActiveInHierarchy()) return;
        ScopedComponentIterationGuard guard(*this);
        for (size_t i = 0; i < m_Components.size(); ++i) {
            if (!IsActiveInHierarchy()) break;
            auto* component = m_Components[i].get();
            if (!component || !component->IsEnabled() || component->IsPendingRemoval()) continue;
            if (!component->m_HasStarted) {
                component->m_HasStarted = true;
                component->Start();
            }
        }
    }

    void Update(float deltaTime) {
        if (!IsActiveInHierarchy()) return;
        ENGINE_CORE_TRACE("GameObject '{}': Updating components", GetName());
        ScopedComponentIterationGuard guard(*this);
        for (size_t i = 0; i < m_Components.size(); ++i) {
            if (!IsActiveInHierarchy()) break;
            auto* component = m_Components[i].get();
            if (!component || !component->IsEnabled() || component->IsPendingRemoval()) continue;
            if (!component->m_HasStarted) {
                component->m_HasStarted = true;
                component->Start();
            }
            if (!IsActiveInHierarchy()) break;
            if (component->IsEnabled() && !component->IsPendingRemoval()) {
                component->Update(deltaTime);
            }
        }
    }

    void FixedUpdate(float fixedDeltaTime) {
        if (!IsActiveInHierarchy()) return;
        ScopedComponentIterationGuard guard(*this);
        for (size_t i = 0; i < m_Components.size(); ++i) {
            if (!IsActiveInHierarchy()) break;
            auto* component = m_Components[i].get();
            if (!component || !component->IsEnabled() || component->IsPendingRemoval()) continue;
            if (!component->m_HasStarted) {
                component->m_HasStarted = true;
                component->Start();
            }
            if (!IsActiveInHierarchy()) break;
            if (component->IsEnabled() && !component->IsPendingRemoval()) {
                component->FixedUpdate(fixedDeltaTime);
            }
        }
    }

    void LateUpdate(float deltaTime) {
        if (!IsActiveInHierarchy()) return;
        ScopedComponentIterationGuard guard(*this);
        for (size_t i = 0; i < m_Components.size(); ++i) {
            if (!IsActiveInHierarchy()) break;
            auto* component = m_Components[i].get();
            if (!component || !component->IsEnabled() || component->IsPendingRemoval()) continue;
            component->LateUpdate(deltaTime);
        }
    }

    template<std::derived_from<Component> T>
    [[nodiscard]] T* GetComponent() const {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, Transform>) {
            return m_Transform;
        }
        if (!m_ComponentMask.Intersects(Detail::GetComponentTypeMask<std::remove_cv_t<T>>())) {
            return nullptr;
        }
        for (const auto& component : m_Components) {
            if (component->IsPendingRemoval()) continue;
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
        if (!m_ComponentMask.Intersects(Detail::GetComponentTypeMask<std::remove_cv_t<T>>())) {
            return result;
        }
        for (const auto& component : m_Components) {
            if (component->IsPendingRemoval()) continue;
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
            auto* comp = it->get();
            if (comp && !comp->IsPendingRemoval() && dynamic_cast<T*>(comp) != nullptr) {
                if (comp->IsEnabled() && IsActiveInHierarchy()) {
                    comp->OnDisable();
                }
                comp->m_Enabled = false;

                bool hasMore = false;
                for (const auto& other : m_Components) {
                    if (other.get() != comp && !other->IsPendingRemoval() && dynamic_cast<T*>(other.get())) {
                        hasMore = true;
                        break;
                    }
                }
                if (!hasMore) {
                    m_ComponentMask &= ~Detail::GetComponentTypeMask<std::remove_cv_t<T>>();
                    if constexpr (std::is_same_v<std::remove_cv_t<T>, BoxCollider2D> ||
                                  std::is_same_v<std::remove_cv_t<T>, CircleCollider2D> ||
                                  std::is_same_v<std::remove_cv_t<T>, CapsuleCollider2D>) {
                        bool hasAnyCollider = false;
                        for (const auto& other : m_Components) {
                            if (other.get() != comp && !other->IsPendingRemoval()) {
                                if (dynamic_cast<Collider2D*>(other.get())) {
                                    hasAnyCollider = true;
                                    break;
                                }
                            }
                        }
                        if (!hasAnyCollider) {
                            m_ComponentMask &= ~Detail::GetComponentTypeMask<Collider2D>();
                        }
                    }
                }

                if constexpr (std::is_same_v<std::remove_cv_t<T>, Rigidbody2D> ||
                              std::is_same_v<std::remove_cv_t<T>, Collider2D> ||
                              std::is_same_v<std::remove_cv_t<T>, BoxCollider2D> ||
                              std::is_same_v<std::remove_cv_t<T>, CircleCollider2D> ||
                              std::is_same_v<std::remove_cv_t<T>, CapsuleCollider2D> ||
                              std::is_same_v<std::remove_cv_t<T>, Tilemap>) {
                    NotifyPhysicsComponentRemoved();
                }

                if (m_ComponentIterationDepth > 0) {
                    comp->m_PendingRemoval = true;
                } else {
                    comp->OnDestroy();
                    m_Components.erase(it);
                }
                ENGINE_CORE_DEBUG("GameObject '{}': Removed component", GetName());
                return true;
            }
        }
        return false;
    }

private:
    friend class Scene;

    void NotifyPhysicsComponentAdded();
    void NotifyPhysicsComponentRemoved();

    Scene* m_Scene{ nullptr };
    Transform* m_Transform{nullptr};
    std::vector<std::unique_ptr<Component>> m_Components;
    ComponentMask128 m_ComponentMask;
    EntityTag m_Tag{ EntityTag::Untagged };
    uint32_t m_LayerMask{ EntityLayer::Default };
    std::string m_PoolKey;
    bool m_InFreeList{ false };
    bool m_Active{ true };
    EntityLifecycle m_LifecycleState{ EntityLifecycle::Alive };
    uint32_t m_ComponentIterationDepth{ 0 };
};

inline void Component::SetEnabled(bool enabled) noexcept {
    if (m_Enabled == enabled) return;
    m_Enabled = enabled;
    if (m_GameObject && m_GameObject->IsActiveInHierarchy()) {
        if (m_Enabled) {
            OnEnable();
        } else {
            OnDisable();
        }
    }
}

} // namespace Engine

#endif // SCENE_GAMEOBJECT_H
