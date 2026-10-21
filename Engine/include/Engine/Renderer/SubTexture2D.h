#ifndef RENDERER_SUBTEXTURE2D_H
#define RENDERER_SUBTEXTURE2D_H

#include "Engine/Renderer/Texture.h"

#include <memory>
#include <glm/glm.hpp>

namespace Engine {

class SubTexture2D {
public:
    SubTexture2D(const std::shared_ptr<Texture>& texture, const glm::vec2& min, const glm::vec2& max);

    const std::shared_ptr<Texture>& GetTexture() const { return m_Texture; }
    const glm::vec2& GetMin() const { return m_Min; }
    const glm::vec2& GetMax() const { return m_Max; }

    /**
     * @brief Factory method to calculate UV bounds from grid coordinates
     * @param texture
     * @param coords: The (x, y) index of the sprite on the grid (0,0 is bottom-left)
     * @param cellSize: The width/height of a single grid cell in pixels (e.g., 16x16, 32x32)
     * @param spriteSize: For sprites that take up more than 1 block (e.g., a tree might be 1x2)
     */
    static std::shared_ptr<SubTexture2D> CreateFromCoords(
        const std::shared_ptr<Texture>& texture, 
        const glm::vec2& coords, 
        const glm::vec2& cellSize, 
        const glm::vec2& spriteSize = { 1.0f, 1.0f }
    );

private:
    std::shared_ptr<Texture> m_Texture;
    glm::vec2 m_Min;
    glm::vec2 m_Max;
};

} // namespace Engine

#endif // RENDERER_SUBTEXTURE2D_H