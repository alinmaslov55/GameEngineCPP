#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/Collision2DInfo.h"

namespace Engine {

void GameObject::NotifyPhysicsComponentAdded() {
    if (m_Scene && m_Scene->IsRuntimeRunning()) {
        m_Scene->RegisterRuntimeBody(this);
        m_Scene->MarkPhysicsDirty(this);
    }
}

void GameObject::NotifyPhysicsComponentRemoved() {
    if (m_Scene && m_Scene->IsRuntimeRunning()) {
        if (HasComponent<Rigidbody2D>() || HasComponent<BoxCollider2D>() ||
            HasComponent<CircleCollider2D>() || HasComponent<CapsuleCollider2D>() ||
            HasComponent<Tilemap>()) {
            m_Scene->MarkPhysicsDirty(this);
        } else {
            m_Scene->UnregisterRuntimeBody(this);
        }
    }
}

void GameObject::DispatchCollisionEnter2D(const Collision2DInfo& info) {
    if (!IsActiveInHierarchy()) return;
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (i < m_Components.size() && m_Components[i]->IsEnabled()) {
            m_Components[i]->OnCollisionEnter2D(info);
        }
    }
}

void GameObject::DispatchCollisionExit2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (i < m_Components.size() && m_Components[i]->IsEnabled()) {
            m_Components[i]->OnCollisionExit2D(other);
        }
    }
}

void GameObject::DispatchTriggerEnter2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (i < m_Components.size() && m_Components[i]->IsEnabled()) {
            m_Components[i]->OnTriggerEnter2D(other);
        }
    }
}

void GameObject::DispatchTriggerExit2D(GameObject* other) {
    if (!IsActiveInHierarchy()) return;
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (i < m_Components.size() && m_Components[i]->IsEnabled()) {
            m_Components[i]->OnTriggerExit2D(other);
        }
    }
}

} // namespace Engine
