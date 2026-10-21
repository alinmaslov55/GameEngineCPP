#ifndef MATH_ISOMETRIC_H
#define MATH_ISOMETRIC_H
#include <glm/glm.hpp>

namespace Engine {
class Isometric {
public:
    static constexpr float TILE_WIDTH = 1.0f;
    static constexpr float TILE_HEIGHT = 0.5f;

    static glm::vec2 WorldToScreen(const glm::vec3& worldPos) {
        float screenX = (worldPos.x - worldPos.z) * (TILE_WIDTH / 2.0f);
        
        float screenY = (worldPos.x + worldPos.z) * (TILE_HEIGHT / 2.0f) + worldPos.y;
        
        return { screenX, screenY };
    }

    static glm::vec3 ScreenToWorld(const glm::vec2& screenPos) {
        float worldX = (screenPos.x / (TILE_WIDTH / 2.0f) + screenPos.y / (TILE_HEIGHT / 2.0f)) / 2.0f;
        float worldZ = (screenPos.y / (TILE_HEIGHT / 2.0f) - (screenPos.x / (TILE_WIDTH / 2.0f))) / 2.0f;
        
        return { worldX, 0.0f, worldZ };
    }
};
} // namespace Engine

#endif // MATH_ISOMETRIC_H