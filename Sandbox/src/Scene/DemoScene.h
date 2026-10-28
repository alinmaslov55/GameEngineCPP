#ifndef SANDBOX_DEMOSCENE_H
#define SANDBOX_DEMOSCENE_H

#include <Engine/Scene/Scene.h>
#include <memory>

namespace Sandbox {

class DemoScene {
public:
    // Creates the default "Village" demo scene (Player, 10x10 Tilemap, HUD) and saves Village.scene
    static std::shared_ptr<Engine::Scene> CreateVillageScene();

    // Attaches runtime gameplay scripts (PlayerController, CameraController) when entering Play Mode
    static void BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene);
};

} // namespace Sandbox

#endif // SANDBOX_DEMOSCENE_H

