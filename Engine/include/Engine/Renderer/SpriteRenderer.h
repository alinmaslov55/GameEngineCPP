#ifndef RENDERER_SPRITERENDERER_H
#define RENDERER_SPRITERENDERER_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <glm/vec4.hpp>
#include <memory>

namespace Engine {
class SpriteRenderer : public Component {
public:
    enum class BillboardMode {
        ViewPlane = 0,    // Parallel to camera view plane (Cult of the Lamb cutout standee)
        CylindricalY = 1, // Upright vertical (+Y), rotates around Y to face camera yaw
        FlatGround = 2,   // Laid flat on XZ ground plane (rugs, soil plots, ground decals)
        None = 3          // Uses 3D Transform rotation directly
    };

    SpriteRenderer() = default;
    ~SpriteRenderer() override = default;

    void SetTexture(const std::shared_ptr<Texture>& texture) {
        m_Texture = texture;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetTexture ID {}", texture ? texture->GetRendererID() : 0);
    }
    const std::shared_ptr<Texture>& GetTexture() const { return m_Texture; }

    void SetColor(const glm::vec4& color) {
        m_Color = color;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetColor ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
    }
    const glm::vec4& GetColor() const { return m_Color; }
    glm::vec4& GetColor() { return m_Color; }

    void SetSubTexture(const std::shared_ptr<SubTexture2D>& subTexture) {
        m_SubTexture = subTexture;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetSubTexture {}", subTexture ? "assigned" : "nullptr");
    }
    const std::shared_ptr<SubTexture2D>& GetSubTexture() const { return m_SubTexture; }

    BillboardMode GetBillboardMode() const { return m_BillboardMode; }
    void SetBillboardMode(BillboardMode mode) {
        m_BillboardMode = mode;
        ENGINE_CORE_DEBUG("SpriteRenderer: SetBillboardMode {}", static_cast<int>(mode));
    }

    float GetAlphaCutoff() const { return m_AlphaCutoff; }
    void SetAlphaCutoff(float cutoff) {
        m_AlphaCutoff = std::clamp(cutoff, 0.0f, 1.0f);
        ENGINE_CORE_TRACE("SpriteRenderer: SetAlphaCutoff {:.2f}", m_AlphaCutoff);
    }

    bool GetCastShadows() const { return m_CastShadows; }
    void SetCastShadows(bool castShadows) {
        m_CastShadows = castShadows;
        ENGINE_CORE_TRACE("SpriteRenderer: SetCastShadows {}", castShadows);
    }

    bool GetReceiveLighting() const { return m_ReceiveLighting; }
    void SetReceiveLighting(bool receiveLighting) {
        m_ReceiveLighting = receiveLighting;
        ENGINE_CORE_TRACE("SpriteRenderer: SetReceiveLighting {}", receiveLighting);
    }

    float GetEmissive() const { return m_Emissive; }
    void SetEmissive(float emissive) {
        m_Emissive = std::clamp(emissive, 0.0f, 5.0f);
        ENGINE_CORE_TRACE("SpriteRenderer: SetEmissive {:.2f}", m_Emissive);
    }

    float GetWindSwayWeight() const { return m_WindSwayWeight; }
    void SetWindSwayWeight(float weight) { m_WindSwayWeight = std::clamp(weight, 0.0f, 2.0f); }

    float GetWindPhaseOffset() const { return m_WindPhaseOffset; }
    void SetWindPhaseOffset(float offset) { m_WindPhaseOffset = offset; }

private:
    std::shared_ptr<Texture> m_Texture = nullptr;
    std::shared_ptr<SubTexture2D> m_SubTexture = nullptr;
    glm::vec4 m_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    BillboardMode m_BillboardMode = BillboardMode::ViewPlane;
    float m_AlphaCutoff = 0.15f;
    bool m_CastShadows = true;
    bool m_ReceiveLighting = true;
    float m_Emissive = 0.0f;
    float m_WindSwayWeight = 0.0f;
    float m_WindPhaseOffset = 0.0f;
};
} // namespace Engine

#endif // RENDERER_SPRITERENDERER_H
