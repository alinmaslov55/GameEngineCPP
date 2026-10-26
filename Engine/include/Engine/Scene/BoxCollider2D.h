#ifndef SCENE_BOXCOLLIDER2D_H
#define SCENE_BOXCOLLIDER2D_H

#include "Engine/Scene/Component.h"

#include <glm/glm.hpp>

namespace Engine {
class BoxCollider2D : public Component {
public:
    glm::vec2 size = { 1.0f, 1.0f };   // Base size of the bounding box on XZ ground plane
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from the Transform's center on XZ ground plane
    bool isDynamic = false;            // True for moving entities (player), False for static obstacles

    // Vertical Y-axis isometric physics (jumping & gravity)
    bool useGravity = true;
    float gravity = -9.8f;
    float verticalVelocity = 0.0f;
    float groundLevel = 0.0f;

    bool IsGrounded(float currentY) const {
        return currentY <= (groundLevel + 1e-4f) && verticalVelocity <= 0.0f;
    }
};
} // namespace Engine

#endif // SCENE_BOXCOLLIDER2D_H
