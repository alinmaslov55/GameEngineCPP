#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"

namespace Engine {

void GameObject::NotifyPhysicsComponentAdded() {
    if (m_Scene && m_Scene->IsRuntimeRunning()) {
        m_Scene->RegisterRuntimeBody(this);
    }
}

void GameObject::NotifyPhysicsComponentRemoved() {
    if (m_Scene && m_Scene->IsRuntimeRunning()) {
        m_Scene->UnregisterRuntimeBody(this);
    }
}

} // namespace Engine
