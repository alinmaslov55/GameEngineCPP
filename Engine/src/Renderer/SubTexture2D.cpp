#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"

namespace Engine {
SubTexture2D::SubTexture2D(const std::shared_ptr<Texture>& texture, const glm::vec2& min, const glm::vec2& max)
    : m_Texture(texture), m_Min(min), m_Max(max) {
    ENGINE_CORE_TRACE("SubTexture2D created with UV min({:.4f}, {:.4f}) max({:.4f}, {:.4f})", min.x, min.y, max.x, max.y);
}

std::shared_ptr<SubTexture2D> SubTexture2D::CreateFromCoords(
    const std::shared_ptr<Texture>& texture, 
    const glm::vec2& coords, 
    const glm::vec2& cellSize, 
    const glm::vec2& spriteSize) 
{
    if (!texture) {
        ENGINE_CORE_ERROR("SubTexture2D::CreateFromCoords failed: texture is null");
        return nullptr;
    }

    // OpenGL UVs go from 0.0 to 1.0. We divide the pixel coords by the total width/height.
    float minX = (coords.x * cellSize.x) / texture->GetWidth();
    float minY = (coords.y * cellSize.y) / texture->GetHeight();
    float maxX = ((coords.x + spriteSize.x) * cellSize.x) / texture->GetWidth();
    float maxY = ((coords.y + spriteSize.y) * cellSize.y) / texture->GetHeight();

    return std::make_shared<SubTexture2D>(texture, glm::vec2{minX, minY}, glm::vec2{maxX, maxY});
}
} // namespace Engine