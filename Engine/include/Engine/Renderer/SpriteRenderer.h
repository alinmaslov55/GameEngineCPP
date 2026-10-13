#ifndef RENDERER_SPRITERENDERER_H
#define RENDERER_SPRITERENDERER_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <memory>


namespace Engine {
class SpriteRenderer : public Component {
public:
    SpriteRenderer() = default;
    ~SpriteRenderer() override = default;

    void SetTexture(const std::shared_ptr<Texture>& texture) { m_Texture = texture; }
    std::shared_ptr<Texture> GetTexture() const { return m_Texture; }

    void SetColor(const glm::vec4& color) { m_Color = color; }
    const glm::vec4& GetColor() const { return m_Color; }

private:
    std::shared_ptr<Texture> m_Texture = nullptr;
    glm::vec4 m_Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // white
};
} // namespace Engine

#endif // RENDERER_SPRITERENDERER_H