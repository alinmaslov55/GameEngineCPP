#ifndef SCENE_STANDEECOMMAND_H
#define SCENE_STANDEECOMMAND_H

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Engine {

class Transform;
class SpriteRenderer;

struct StandeeRenderCommand {
    const Transform* transform = nullptr;
    const SpriteRenderer* sprite = nullptr;

    glm::vec2 size{ 1.0f };
    float rotationZ = 0.0f;
    float windWeight = 0.0f;
    float spatialPhase = 0.0f;
    float camProximityBend = 0.0f;
    float shadowProximityBend = 0.0f;

    float alphaCutoff = 0.15f;
    float emissive = -1.0f;
    float receiverOffset = 0.48f;
    glm::vec4 color{ 1.0f };

    float viewDepth = 0.0f;
    glm::vec3 boundsCenter{ 0.0f };
    float boundsRadius = 0.0f;
};

} // namespace Engine

#endif // SCENE_STANDEECOMMAND_H

