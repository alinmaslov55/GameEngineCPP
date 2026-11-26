#ifndef SCENE_GAMEOBJECT_H
#define SCENE_GAMEOBJECT_H

#include "Engine/Scene/Object.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/ComponentMask128.h"
#include "Engine/Scene/ComponentTypeRegistry.h"
#include "Engine/Scene/EntityTag.h"
#include "Engine/Utils/Logger.h"

#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
class PointLightComponent;
class BoxCollider2D;
class CircleCollider2D;
class CapsuleCollider2D;
class Tilemap;
class TerrainChunkManager;
class TerrainComponent;
struct Collision2DInfo;

enum class EntityLifecycle : uint8_t {
    PendingSpawn = 0,
    Alive,
    PendingDestroy,
    Destroyed
};

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
    void SetScene(Scene* scene) noexcept;

    [[nodiscard]] bool HasFixedUpdate() const noexcept { return m_FixedUpdateComponentCount > 0; }
    void RegisterFixedUpdate();
    void UnregisterFixedUpdate();
    void RegisterPointLight();
    void UnregisterPointLight();
    void InvalidateSceneTerrainCache();

    [[nodiscard]] ComponentMask128 GetComponentMask() const noexcept { return m_ComponentMask; }

    void SetTag(EntityTag tag) noexcept { m_Tag = tag; }
    [[nodiscard]] EntityTag GetTag() const noexcept { return m_Tag; }
    [[nodiscard]] bool HasTag(EntityTag tag) const noexcept { return m_Tag == tag; }

    void SetLayer(uint32_t layerMask) noexcept { m_LayerMask = layerMask; }
    [[nodiscard]] uint32_t GetLayer() const noexcept { return m_LayerMask; }
    [[nodiscard]] bool IsInLayer(uint32_t layerMask) const noexcept { return (m_LayerMask & layerMask) != 0; }

    template<std::derived_from<Component> T>
    [[nodiscard]] bool HasComponent() const noexcept {
        return GetComponent<T>() != nullptr;
    }

    template<std::derived_from<Component> T, typename... Args>
    T* AddComponent(Args&&... args) {
        using CleanT = std::remove_cv_t<T>;
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = component.get();

        const uint16_t typeID = Detail::GetComponentTypeID<CleanT>();
        component->m_GameObject = this;
        component->m_TypeID = typeID;

        m_Components.push_back(std::move(component));
        m_ComponentMask |= ComponentMask128::FromBit(typeID);

        if constexpr (Detail::HasComponentBaseType<CleanT>) {
            using BaseT = typename CleanT::ComponentBaseType;
            if constexpr (!std::is_same_v<CleanT, BaseT>) {
                const uint16_t baseID = Detail::GetComponentTypeID<BaseT>();
                rawPtr->m_BaseTypeID = baseID;
                m_ComponentMask |= ComponentMask128::FromBit(baseID);
            }
        }

        ENGINE_CORE_DEBUG("GameObject '{}': Added component", GetName());
        bool selfRemoved = false;
        {
            ScopedComponentIterationGuard guard(*this);
            rawPtr->Awake();
            if (!rawPtr->IsPendingRemoval() && IsActiveInHierarchy() && rawPtr->IsEnabled()) {
                rawPtr->OnEnable();
            }
            selfRemoved = rawPtr->IsPendingRemoval();
        }

        if (selfRemoved) {
            return nullptr;
        }

        if constexpr (std::is_same_v<CleanT, Rigidbody2D> ||
                      std::is_same_v<CleanT, Tilemap> ||
                      Detail::IsComponentWithBase<CleanT, Collider2D>()) {
            NotifyPhysicsComponentAdded();
        }

        if constexpr (Detail::HasOverriddenFixedUpdate<CleanT>) {
            rawPtr->m_HasOverriddenFixedUpdate = true;
            ++m_FixedUpdateComponentCount;
            if (m_FixedUpdateComponentCount == 1) {
                RegisterFixedUpdate();
            }
        }

        if constexpr (std::is_same_v<CleanT, TerrainChunkManager> || std::is_same_v<CleanT, TerrainComponent>) {
            InvalidateSceneTerrainCache();
        }

        if constexpr (std::is_same_v<CleanT, PointLightComponent>) {
            RegisterPointLight();
        }

        return rawPtr;
    }

    void SetActive(bool active) noexcept {
        if (m_Active == active) return;
        const bool wasActiveInHier = IsActiveInHierarchy();
        m_Active = active;
        const bool isActiveInHier = IsActiveInHierarchy();
        if (wasActiveInHier != isActiveInHier) {
            NotifyHierarchyActiveChanged();
        }
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
        NotifyStaticSpatialColliderStateChanged();
        if (m_Transform) {
            const auto& children = m_Transform->GetChildren();
            if (children.empty()) {
                return;
            }
            if (children.size() <= 16) {
                std::array<Transform*, 16> stackChildren{};
                const size_t count = children.size();
                for (size_t i = 0; i < count; ++i) {
                    stackChildren[i] = children[i];
                }
                for (size_t i = 0; i < count; ++i) {
                    if (stackChildren[i]) {
                        if (auto* childGo = stackChildren[i]->GetGameObject()) {
                            if (childGo->IsActive()) {
                                childGo->NotifyHierarchyActiveChanged();
                            }
                        }
                    }
                }
            } else {
                auto childrenCopy = children;
                for (auto* child : childrenCopy) {
                    if (child) {
                        if (auto* childGo = child->GetGameObject()) {
                            if (childGo->IsActive()) {
                                childGo->NotifyHierarchyActiveChanged();
                            }
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] bool HasDynamicRigidbody() const noexcept;
    [[nodiscard]] bool HasStaticSpatialCollider() const noexcept;
    void NotifyStaticSpatialColliderStateChanged() const noexcept;

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
    [[nodiscard]] T* GetComponent() const noexcept {
        using CleanT = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<CleanT, Transform>) {
            return m_Transform;
        } else if constexpr (std::is_same_v<CleanT, Component>) {
            for (const auto& component : m_Components) {
                if (component && !component->IsPendingRemoval()) {
                    return component.get();
                }
            }
            return nullptr;
        } else {
            const uint16_t targetID = Detail::GetComponentTypeID<CleanT>();
            if constexpr (std::is_final_v<CleanT> || Detail::IsRegisteredPolymorphicBase<CleanT>()) {
                if (!m_ComponentMask.Intersects(ComponentMask128::FromBit(targetID))) {
                    return nullptr;
                }
                for (const auto& component : m_Components) {
                    if (!component || component->IsPendingRemoval()) continue;
                    if (component->GetTypeID() == targetID || component->GetBaseTypeID() == targetID) {
                        return static_cast<T*>(component.get());
                    }
                }
                return nullptr;
            } else {
                for (const auto& component : m_Components) {
                    if (!component || component->IsPendingRemoval()) continue;
                    if (component->GetTypeID() == targetID || component->GetBaseTypeID() == targetID) {
                        return static_cast<T*>(component.get());
                    }
                    if (T* casted = dynamic_cast<T*>(component.get())) {
                        return casted;
                    }
                }
                return nullptr;
            }
        }
    }

    template<std::derived_from<Component> T>
    [[nodiscard]] std::vector<T*> GetComponents() const {
        std::vector<T*> result;
        using CleanT = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<CleanT, Transform>) {
            if (m_Transform) result.push_back(m_Transform);
        } else if constexpr (std::is_same_v<CleanT, Component>) {
            result.reserve(m_Components.size());
            for (const auto& component : m_Components) {
                if (component && !component->IsPendingRemoval()) {
                    result.push_back(component.get());
                }
            }
        } else {
            const uint16_t targetID = Detail::GetComponentTypeID<CleanT>();
            if constexpr (std::is_final_v<CleanT> || Detail::IsRegisteredPolymorphicBase<CleanT>()) {
                if (!m_ComponentMask.Intersects(ComponentMask128::FromBit(targetID))) {
                    return result;
                }
                for (const auto& component : m_Components) {
                    if (!component || component->IsPendingRemoval()) continue;
                    if (component->GetTypeID() == targetID || component->GetBaseTypeID() == targetID) {
                        result.push_back(static_cast<T*>(component.get()));
                    }
                }
            } else {
                for (const auto& component : m_Components) {
                    if (!component || component->IsPendingRemoval()) continue;
                    if (component->GetTypeID() == targetID || component->GetBaseTypeID() == targetID) {
                        result.push_back(static_cast<T*>(component.get()));
                    } else if (T* casted = dynamic_cast<T*>(component.get())) {
                        result.push_back(casted);
                    }
                }
            }
        }
        return result;
    }

    template<std::derived_from<Component> T>
    bool RemoveComponent() {
        using CleanT = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<CleanT, Transform>) {
            ENGINE_CORE_WARN("GameObject '{}': Cannot remove mandatory Transform component", GetName());
            return false;
        } else {
            const uint16_t targetID = Detail::GetComponentTypeID<CleanT>();

            auto matchesTarget = [this, targetID](Component* c) noexcept -> bool {
                if (!c || c == m_Transform || c->IsPendingRemoval()) return false;
                if constexpr (std::is_same_v<CleanT, Component>) {
                    return true;
                } else {
                    if (c->GetTypeID() == targetID || c->GetBaseTypeID() == targetID) {
                        return true;
                    }
                    if constexpr (!std::is_final_v<CleanT> && !Detail::IsRegisteredPolymorphicBase<CleanT>()) {
                        return dynamic_cast<CleanT*>(c) != nullptr;
                    } else {
                        return false;
                    }
                }
            };

            // Pass 1: Zero-RTTI exact or tracked-base match
            auto foundIt = m_Components.end();
            for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
                auto* comp = it->get();
                if (!comp || comp == m_Transform || comp->IsPendingRemoval()) continue;
                if constexpr (std::is_same_v<CleanT, Component>) {
                    foundIt = it;
                    break;
                } else if (comp->GetTypeID() == targetID || comp->GetBaseTypeID() == targetID) {
                    foundIt = it;
                    break;
                }
            }
            // Pass 2: Polymorphic RTTI fallback for unregistered bases
            if constexpr (!std::is_same_v<CleanT, Component> &&
                          !std::is_final_v<CleanT> &&
                          !Detail::IsRegisteredPolymorphicBase<CleanT>()) {
                if (foundIt == m_Components.end()) {
                    for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
                        auto* comp = it->get();
                        if (comp && comp != m_Transform && !comp->IsPendingRemoval() && dynamic_cast<CleanT*>(comp) != nullptr) {
                            foundIt = it;
                            break;
                        }
                    }
                }
            }

            if (foundIt == m_Components.end() || foundIt->get() == m_Transform) {
                return false;
            }

            Component* comp = foundIt->get();
            if (comp->IsPendingRemoval()) {
                return false;
            }

            // 1. Keep comp alive across OnDisable() and mask/physics/registry cleanup;
            //    guard exit flushes OnDestroy() + deletion once when depth returns to 0.
            ScopedComponentIterationGuard guard(*this);

            // Snapshot component metadata before OnDisable() runs
            const uint16_t removedExactID = comp->GetTypeID();
            const uint16_t removedBaseID = comp->GetBaseTypeID();
            const bool hasOverriddenFixedUpdate = comp->HasOverriddenFixedUpdate();

            // 2. Mark pending removal immediately so queries/reentrancy immediately recognize removed state
            comp->m_PendingRemoval = true;
            const bool wasEnabledAndActive = comp->IsEnabled() && IsActiveInHierarchy();
            comp->m_Enabled = false;

            if (wasEnabledAndActive) {
                comp->OnDisable();
            }

            bool hasMoreOfTarget = false;
            bool hasMoreOfExact = false;
            bool hasMoreOfBase = false;
            for (const auto& other : m_Components) {
                Component* otherPtr = other.get();
                if (!otherPtr || otherPtr == comp || otherPtr == m_Transform || otherPtr->IsPendingRemoval()) continue;
                if (otherPtr->GetTypeID() == removedExactID) {
                    hasMoreOfExact = true;
                }
                if (removedBaseID != Component::kInvalidTypeID &&
                    (otherPtr->GetTypeID() == removedBaseID || otherPtr->GetBaseTypeID() == removedBaseID)) {
                    hasMoreOfBase = true;
                }
                if (matchesTarget(otherPtr)) {
                    hasMoreOfTarget = true;
                }
            }

            if (!hasMoreOfExact && removedExactID != Component::kInvalidTypeID) {
                m_ComponentMask &= ~ComponentMask128::FromBit(removedExactID);
            }
            if (!hasMoreOfBase && removedBaseID != Component::kInvalidTypeID) {
                m_ComponentMask &= ~ComponentMask128::FromBit(removedBaseID);
            }
            if (!hasMoreOfTarget && targetID != Component::kInvalidTypeID) {
                m_ComponentMask &= ~ComponentMask128::FromBit(targetID);
            }

            const uint16_t rigidbodyID = Detail::GetComponentTypeID<Rigidbody2D>();
            const uint16_t tilemapID = Detail::GetComponentTypeID<Tilemap>();
            const uint16_t colliderBaseID = Detail::GetComponentTypeID<Collider2D>();
            const bool isTilemap = (removedExactID == tilemapID);
            const bool isRigidbody = (removedExactID == rigidbodyID);
            const bool isCollider = (removedExactID == colliderBaseID) || (removedBaseID == colliderBaseID);
            if (isRigidbody || isTilemap || isCollider) {
                NotifyPhysicsComponentRemoved(isTilemap, isRigidbody, comp);
            }

            if (hasOverriddenFixedUpdate) {
                if (m_FixedUpdateComponentCount > 0) {
                    --m_FixedUpdateComponentCount;
                    if (m_FixedUpdateComponentCount == 0) {
                        UnregisterFixedUpdate();
                    }
                }
            }

            if (removedExactID == Detail::GetComponentTypeID<TerrainChunkManager>() ||
                removedExactID == Detail::GetComponentTypeID<TerrainComponent>()) {
                InvalidateSceneTerrainCache();
            }

            const uint16_t pointLightID = Detail::GetComponentTypeID<PointLightComponent>();
            if (removedExactID == pointLightID) {
                bool hasMorePointLights = false;
                for (const auto& other : m_Components) {
                    const Component* otherPtr = other.get();
                    if (otherPtr && otherPtr != comp && otherPtr != m_Transform &&
                        !otherPtr->IsPendingRemoval() && otherPtr->GetTypeID() == pointLightID) {
                        hasMorePointLights = true;
                        break;
                    }
                }
                if (!hasMorePointLights) {
                    UnregisterPointLight();
                }
            }

            ENGINE_CORE_DEBUG("GameObject '{}': Removed component", GetName());
            return true;
        }
    }

    bool RemoveComponent(Component* instance) {
        if (!instance || instance == m_Transform || instance->IsPendingRemoval()) {
            return false;
        }

        auto foundIt = std::find_if(m_Components.begin(), m_Components.end(),
            [instance](const std::unique_ptr<Component>& ptr) { return ptr.get() == instance; });
        if (foundIt == m_Components.end()) {
            return false;
        }

        Component* comp = foundIt->get();
        ScopedComponentIterationGuard guard(*this);

        const uint16_t removedExactID = comp->GetTypeID();
        const uint16_t removedBaseID = comp->GetBaseTypeID();
        const bool hasOverriddenFixedUpdate = comp->HasOverriddenFixedUpdate();

        comp->m_PendingRemoval = true;
        const bool wasEnabledAndActive = comp->IsEnabled() && IsActiveInHierarchy();
        comp->m_Enabled = false;

        if (wasEnabledAndActive) {
            comp->OnDisable();
        }

        bool hasMoreOfExact = false;
        bool hasMoreOfBase = false;
        for (const auto& other : m_Components) {
            Component* otherPtr = other.get();
            if (!otherPtr || otherPtr == comp || otherPtr == m_Transform || otherPtr->IsPendingRemoval()) continue;
            if (otherPtr->GetTypeID() == removedExactID) {
                hasMoreOfExact = true;
            }
            if (removedBaseID != Component::kInvalidTypeID &&
                (otherPtr->GetTypeID() == removedBaseID || otherPtr->GetBaseTypeID() == removedBaseID)) {
                hasMoreOfBase = true;
            }
        }

        if (!hasMoreOfExact && removedExactID != Component::kInvalidTypeID) {
            m_ComponentMask &= ~ComponentMask128::FromBit(removedExactID);
        }
        if (!hasMoreOfBase && removedBaseID != Component::kInvalidTypeID) {
            m_ComponentMask &= ~ComponentMask128::FromBit(removedBaseID);
        }

        const uint16_t rigidbodyID = Detail::GetComponentTypeID<Rigidbody2D>();
        const uint16_t tilemapID = Detail::GetComponentTypeID<Tilemap>();
        const uint16_t colliderBaseID = Detail::GetComponentTypeID<Collider2D>();
        const bool isTilemap = (removedExactID == tilemapID);
        const bool isRigidbody = (removedExactID == rigidbodyID);
        const bool isCollider = (removedExactID == colliderBaseID) || (removedBaseID == colliderBaseID);
        if (isRigidbody || isTilemap || isCollider) {
            NotifyPhysicsComponentRemoved(isTilemap, isRigidbody, comp);
        }

        if (hasOverriddenFixedUpdate) {
            if (m_FixedUpdateComponentCount > 0) {
                --m_FixedUpdateComponentCount;
                if (m_FixedUpdateComponentCount == 0) {
                    UnregisterFixedUpdate();
                }
            }
        }

        if (removedExactID == Detail::GetComponentTypeID<TerrainChunkManager>() ||
            removedExactID == Detail::GetComponentTypeID<TerrainComponent>()) {
            InvalidateSceneTerrainCache();
        }

        const uint16_t pointLightID = Detail::GetComponentTypeID<PointLightComponent>();
        if (removedExactID == pointLightID) {
            bool hasMorePointLights = false;
            for (const auto& other : m_Components) {
                const Component* otherPtr = other.get();
                if (otherPtr && otherPtr != comp && otherPtr != m_Transform &&
                    !otherPtr->IsPendingRemoval() && otherPtr->GetTypeID() == pointLightID) {
                    hasMorePointLights = true;
                    break;
                }
            }
            if (!hasMorePointLights) {
                UnregisterPointLight();
            }
        }

        ENGINE_CORE_DEBUG("GameObject '{}': Removed component instance", GetName());
        return true;
    }

private:
    friend class Scene;

    void NotifyPhysicsComponentAdded();
    void NotifyPhysicsComponentRemoved(bool removedIsTilemap, bool removedIsRigidbody, Component* removedComp = nullptr);

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
    uint16_t m_FixedUpdateComponentCount{ 0 };
};

} // namespace Engine

#endif // SCENE_GAMEOBJECT_H
