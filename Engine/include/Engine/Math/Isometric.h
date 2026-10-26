#ifndef MATH_ISOMETRIC_H
#define MATH_ISOMETRIC_H
#include <glm/glm.hpp>

namespace Engine {
class Isometric {
public:
    static constexpr float TILE_WIDTH = 1.0f;
    static constexpr float TILE_HEIGHT = 0.5f;
    static constexpr float TILE_W_HALF = TILE_WIDTH / 2.0f;
    static constexpr float TILE_H_HALF = TILE_HEIGHT / 2.0f;

    static inline glm::vec2 WorldToScreen(const glm::vec3& worldPos) {
        return {
            (worldPos.x - worldPos.z) * TILE_W_HALF,
            (worldPos.x + worldPos.z) * TILE_H_HALF + worldPos.y
        };
    }

    static inline glm::vec3 ScreenToWorld(const glm::vec2& screenPos) {
        return {
            (screenPos.x / TILE_W_HALF + screenPos.y / TILE_H_HALF) / 2.0f,
            0.0f, // Y is up, grid is flat on XZ plane
            (screenPos.y / TILE_H_HALF - screenPos.x / TILE_W_HALF) / 2.0f
        };
    }
};
} // namespace Engine

#endif // MATH_ISOMETRIC_H
