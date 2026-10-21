#include "Engine/Renderer/SubTexture2D.h"

namespace Engine {
SubTexture2D::SubTexture2D(const std::shared_ptr<Texture>& texture, const glm::vec2& min, const glm::vec2& max)
    : m_Texture(texture), m_Min(min), m_Max(max) {}

std::shared_ptr<SubTexture2D> SubTexture2D::CreateFromCoords(
    const std::shared_ptr<Texture>& texture, 
    const glm::vec2& coords, 
    const glm::vec2& cellSize, 
    const glm::vec2& spriteSize) 
{
    // OpenGL UVs go from 0.0 to 1.0. We divide the pixel coords by the total width/height.
    float minX = (coords.x * cellSize.x) / texture->GetWidth();
    float minY = (coords.y * cellSize.y) / texture->GetHeight();
    float maxX = ((coords.x + spriteSize.x) * cellSize.x) / texture->GetWidth();
    float maxY = ((coords.y + spriteSize.y) * cellSize.y) / texture->GetHeight();

    return std::make_shared<SubTexture2D>(texture, glm::vec2{minX, minY}, glm::vec2{maxX, maxY});
}
} // namespace Engine