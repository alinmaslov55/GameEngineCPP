#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsWorld2D.h"
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

void GameObject::NotifyPhysicsComponentRemoved(bool removedIsTilemap, bool removedIsRigidbody, Component* removedComp) {
    if (!m_Scene) {
        return;
    }
    m_Scene->AssertMainThreadAffinity("GameObject::NotifyPhysicsComponentRemoved");
    ENGINE_CORE_TRACE("GameObject '{}' (UUID {}): NotifyPhysicsComponentRemoved (tilemap={}, rb={})",
                      GetName(), GetUUID(), removedIsTilemap, removedIsRigidbody);
    if (m_Scene->IsRuntimeRunning()) {
        auto& physicsWorld = m_Scene->GetPhysicsWorld();
        if (removedComp) {
            if (removedIsRigidbody) {
                physicsWorld.UnregisterRigidbody(this, static_cast<Rigidbody2D*>(removedComp));
            } else if (removedIsTilemap) {
                auto* tilemap = static_cast<Tilemap*>(removedComp);
                physicsWorld.UnregisterTilemap(tilemap->RuntimeBody);
                tilemap->RuntimeBody.Reset();
            } else {
                physicsWorld.UnregisterCollider(this, static_cast<Collider2D*>(removedComp));
            }
        }

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

    size_t totalRemoved = 0;
    while (true) {
        std::vector<std::unique_ptr<Component>> batchToDestroy;
        for (auto it = m_Components.begin(); it != m_Components.end(); ) {
            if (*it && (*it)->IsPendingRemoval()) {
                batchToDestroy.push_back(std::move(*it));
                it = m_Components.erase(it);
            } else if (!*it) {
                it = m_Components.erase(it);
            } else {
                ++it;
            }
        }

        if (batchToDestroy.empty()) {
            break;
        }

        totalRemoved += batchToDestroy.size();
        ++m_ComponentIterationDepth;
        for (auto& comp : batchToDestroy) {
            if (comp) {
                comp->OnDestroy();
            }
        }
        --m_ComponentIterationDepth;
    }

    if (totalRemoved > 0) {
        ENGINE_CORE_DEBUG("GameObject '{}' (UUID {}): Flushed {} pending component removal(s)",
                          GetName(), GetUUID(), totalRemoved);
    }
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

void GameObject::SetScene(Scene* scene) noexcept {
    if (m_Scene == scene) return;
    if (m_Scene && HasFixedUpdate()) {
        m_Scene->UnregisterFixedUpdateEntity(this);
    }
    m_Scene = scene;
    if (m_Scene && HasFixedUpdate()) {
        m_Scene->RegisterFixedUpdateEntity(this);
    }
}

void GameObject::RegisterFixedUpdate() {
    if (m_Scene) {
        m_Scene->RegisterFixedUpdateEntity(this);
    }
}

void GameObject::UnregisterFixedUpdate() {
    if (m_Scene) {
        m_Scene->UnregisterFixedUpdateEntity(this);
    }
}

void GameObject::InvalidateSceneTerrainCache() {
    if (m_Scene) {
        m_Scene->InvalidateTerrainCache();
    }
}

} // namespace Engine
