#ifndef SANDBOX_ARENASTREAMINGDRIVER_H
#define SANDBOX_ARENASTREAMINGDRIVER_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/TerrainChunkManager.h"

namespace Sandbox {

/// @brief Drives TerrainChunkManager streaming from the Player's world XZ position each frame.
class ArenaStreamingDriver final : public Engine::Component {
public:
    void Update(float /*deltaTime*/) override {
        auto* owner = GetGameObject();
        if (!owner) {
            return;
        }
        auto* scene = owner->GetScene();
        if (!scene) {
            return;
        }
        for (const auto& goPtr : scene->GetGameObjects()) {
            if (goPtr && goPtr->GetName() == "Terrain") {
                if (auto* chunkMgr = goPtr->GetComponent<Engine::TerrainChunkManager>()) {
                    chunkMgr->UpdateStreaming(owner->GetTransform()->GetWorldPosition());
                }
                break;
            }
        }
    }
};

} // namespace Sandbox

#endif // SANDBOX_ARENASTREAMINGDRIVER_H
