#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"

namespace Engine {

void Component::SetEnabled(bool enabled) noexcept {
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
