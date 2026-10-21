#ifndef SCENE_BOXCOLLIDER2D_H
#define SCENE_BOXCOLLIDER2D_H

#include "Engine/Scene/Component.h"

#include <glm/glm.hpp>

namespace Engine {
class BoxCollider2D : public Component {
public:
    glm::vec2 size = { 1.0f, 1.0f };   // Base size of the bounding box
    glm::vec2 offset = { 0.0f, 0.0f }; // Shift from the Transform's center
    bool isDynamic = false;            // True for player (moves), False for trees (static)
};
} // namespace Engine

#endif // SCENE_BOXCOLLIDER2D_H