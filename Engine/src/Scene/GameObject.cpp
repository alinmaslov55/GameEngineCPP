#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Utils/Logger.h"

namespace Engine {

bool GameObject::HasDynamicRigidbody() const noexcept {
    if (!HasComponent<Rigidbody2D>()) {
        return false;
    }
    const auto* rb = GetComponent<Rigidbody2D>();
    return rb && rb->Type == Rigidbody2D::BodyType::Dynamic;
}

bool GameObject::HasStaticSpatialCollider() const noexcept {
    if (HasComponent<Tilemap>()) {
        return true;
    }
    if (!HasComponent<Collider2D>()) {
        return false;
    }
    return !HasDynamicRigidbody();
}

void GameObject::NotifyStaticSpatialColliderStateChanged() const noexcept {
    if (m_Scene && IsAlive() && HasStaticSpatialCollider()) {
        m_Scene->InvalidateSpatialGrid();
    }
}

void GameObject::NotifyPhysicsComponentAdded() {
    if (!m_Scene) {
        return;
    }
    m_Scene->AssertMainThreadAffinity("GameObject::NotifyPhysicsComponentAdded");
    ENGINE_CORE_TRACE("GameObject '{}' (UUID {}): NotifyPhysicsComponentAdded (runtime={})",
                      GetName(), GetUUID(), m_Scene->IsRuntimeRunning());
    if (m_Scene->IsRuntimeRunning()) {
        m_Scene->RegisterRuntimeBody(this);
        m_Scene->MarkPhysicsDirty(this);
    }
    NotifyStaticSpatialColliderStateChanged();
}

void GameObject::NotifyPhysicsComponentRemoved(bool removedIsTilemap, bool removedIsRigidbody) {
    if (!m_Scene) {
        return;
    }
    m_Scene->AssertMainThreadAffinity("GameObject::NotifyPhysicsComponentRemoved");
    ENGINE_CORE_TRACE("GameObject '{}' (UUID {}): NotifyPhysicsComponentRemoved (tilemap={}, rb={})",
                      GetName(), GetUUID(), removedIsTilemap, removedIsRigidbody);
    if (m_Scene->IsRuntimeRunning()) {
        if (HasComponent<Rigidbody2D>() || HasComponent<Collider2D>() || HasComponent<Tilemap>()) {
            m_Scene->MarkPhysicsDirty(this);
        } else {
            m_Scene->UnregisterRuntimeBody(this);
        }
    }
    if (IsAlive()) {
        const bool affectsStaticGrid = removedIsTilemap ||
            (removedIsRigidbody ? (HasComponent<Collider2D>() && !HasDynamicRigidbody())
                                : !HasDynamicRigidbody());
        if (affectsStaticGrid) {
            m_Scene->InvalidateSpatialGrid();
        }
    }
}

void GameObject::FlushPendingComponentRemovals() {
    if (m_ComponentIterationDepth > 0) return;

    bool hasPending = false;
    for (const auto& comp : m_Components) {
        if (comp && comp->IsPendingRemoval()) {
            hasPending = true;
            break;
        }
    }
    if (!hasPending) return;

    size_t removedCount = 0;
    ++m_ComponentIterationDepth;
    for (size_t i = 0; i < m_Components.size(); ++i) {
        auto* comp = m_Components[i].get();
        if (comp && comp->IsPendingRemoval()) {
            comp->OnDestroy();
            ++removedCount;
        }
    }
    --m_ComponentIterationDepth;

    std::erase_if(m_Components, [](const std::unique_ptr<Component>& comp) {
        return !comp || comp->IsPendingRemoval();
    });
    ENGINE_CORE_DEBUG("GameObject '{}' (UUID {}): Flushed {} pending component removal(s)",
                      GetName(), GetUUID(), removedCount);
}

void GameObject::DispatchCollisionEnter2D(const Collision2DInfo& info) {
    if (!IsActiveInHierarchy()) return;
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (!IsActiveInHierarchy()) break;
        auto* comp = m_Components[i].get();
        if (comp && comp->IsEnabled() && !comp->IsPendingRemoval()) {
            comp->OnCollisionEnter2D(info);
        }
    }
}

void GameObject::DispatchCollisionExit2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (!IsActiveInHierarchy()) break;
        auto* comp = m_Components[i].get();
        if (comp && comp->IsEnabled() && !comp->IsPendingRemoval()) {
            comp->OnCollisionExit2D(other);
        }
    }
}

void GameObject::DispatchTriggerEnter2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (!IsActiveInHierarchy()) break;
        auto* comp = m_Components[i].get();
        if (comp && comp->IsEnabled() && !comp->IsPendingRemoval()) {
            comp->OnTriggerEnter2D(other);
        }
    }
}

void GameObject::DispatchTriggerExit2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (!IsActiveInHierarchy()) break;
        auto* comp = m_Components[i].get();
        if (comp && comp->IsEnabled() && !comp->IsPendingRemoval()) {
            comp->OnTriggerExit2D(other);
        }
    }
}

void GameObject::OnPoolAcquire() {
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        auto* comp = m_Components[i].get();
        if (comp && !comp->IsPendingRemoval()) {
            comp->OnPoolAcquire();
        }
    }
}

void GameObject::OnPoolRelease() {
    ScopedComponentIterationGuard guard(*this);
    for (size_t i = 0; i < m_Components.size(); ++i) {
        auto* comp = m_Components[i].get();
        if (comp && !comp->IsPendingRemoval()) {
            comp->OnPoolRelease();
        }
    }
}

} // namespace Engine
