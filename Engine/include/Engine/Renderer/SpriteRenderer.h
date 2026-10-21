#ifndef RENDERER_SPRITERENDERER_H
#define RENDERER_SPRITERENDERER_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"

#include <glm/glm.hpp>
#include <memory>

namespace Engine {
class SpriteRenderer : public Component {
public:
    SpriteRenderer() = default;
    ~SpriteRenderer() override = default;

    void SetTexture(const std::shared_ptr<Texture>& texture) {
        m_Texture = texture;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetTexture ID {}", texture ? texture->GetRendererID() : 0);
    }
    std::shared_ptr<Texture> GetTexture() const { return m_Texture; }

    void SetColor(const glm::vec4& color) {
        m_Color = color;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetColor ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
    }
    const glm::vec4& GetColor() const { return m_Color; }
    void SetSubTexture(const std::shared_ptr<SubTexture2D>& subTexture) { m_SubTexture = subTexture; }
    std::shared_ptr<SubTexture2D> GetSubTexture() const { return m_SubTexture; }
private:
    std::shared_ptr<Texture> m_Texture = nullptr;
    std::shared_ptr<SubTexture2D> m_SubTexture = nullptr;
    glm::vec4 m_Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // white
};
} // namespace Engine

#endif // RENDERER_SPRITERENDERER_H