#ifndef SANDBOX_INSPECTORPANEL_H
#define SANDBOX_INSPECTORPANEL_H

#include <Engine/Scene/GameObject.h>

namespace Sandbox {

class InspectorPanel {
public:
    void OnImGuiRender(Engine::GameObject* selectedEntity, bool* pOpen);
};

} // namespace Sandbox

#endif // SANDBOX_INSPECTORPANEL_H

