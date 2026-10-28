#ifndef SANDBOX_SCENEHIERARCHYPANEL_H
#define SANDBOX_SCENEHIERARCHYPANEL_H

#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <memory>

namespace Sandbox {

class SceneHierarchyPanel {
public:
    void OnImGuiRender(const std::shared_ptr<Engine::Scene>& activeScene,
                       Engine::GameObject*& selectedEntity,
                       bool* pOpen);
};

} // namespace Sandbox

#endif // SANDBOX_SCENEHIERARCHYPANEL_H

