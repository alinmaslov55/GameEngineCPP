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
class SpriteRenderer final : public Component {
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

    [[nodiscard]] bool GetFlipX() const noexcept { return m_FlipX; }
    void SetFlipX(bool flip) noexcept { m_FlipX = flip; }

    [[nodiscard]] bool GetFlipY() const noexcept { return m_FlipY; }
    void SetFlipY(bool flip) noexcept { m_FlipY = flip; }

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

    //Ash Dissolve, Hit-Flash & Canopy Occlusion Silhouette
    enum class FlashMode : uint32_t {
        PureWhite = 0,
        Crimson = 1,
        CrimsonImpact = 1,
        SpectralBlue = 2,
        DivineGold = 3,
        Custom = 4
    };

    float GetDissolveAmount() const { return m_DissolveAmount; }
    void SetDissolveAmount(float amount) { m_DissolveAmount = std::clamp(amount, 0.0f, 1.0f); }

    float GetDissolveEdgeWidth() const { return m_DissolveEdgeWidth; }
    void SetDissolveEdgeWidth(float width) { m_DissolveEdgeWidth = std::clamp(width, 0.001f, 0.5f); }

    float GetFlashAmount() const { return m_FlashAmount; }
    void SetFlashAmount(float amount) { m_FlashAmount = std::clamp(amount, 0.0f, 1.0f); }

    float GetFlashColorMode() const { return m_FlashColorMode; }
    void SetFlashColorMode(float mode) { m_FlashColorMode = mode; }
    void SetFlashMode(FlashMode mode) { m_FlashColorMode = static_cast<float>(mode); }

    void SetFlashColor(const glm::vec3& rgb) {
        if (rgb == glm::vec3(1.0f, 1.0f, 1.0f)) {
            m_FlashColorMode = 0.0f;
        } else if (rgb == glm::vec3(1.0f, 0.15f, 0.15f)) {
            m_FlashColorMode = 1.0f;
        } else if (rgb == glm::vec3(0.35f, 0.75f, 1.5f)) {
            m_FlashColorMode = 2.0f;
        } else if (rgb == glm::vec3(1.5f, 1.2f, 0.25f)) {
            m_FlashColorMode = 3.0f;
        } else {
            const auto r = static_cast<uint32_t>(std::clamp(rgb.r, 0.0f, 1.0f) * 255.0f);
            const auto g = static_cast<uint32_t>(std::clamp(rgb.g, 0.0f, 1.0f) * 255.0f);
            const auto b = static_cast<uint32_t>(std::clamp(rgb.b, 0.0f, 1.0f) * 255.0f);
            m_FlashColorMode = 4.0f + static_cast<float>((r << 16) | (g << 8) | b);
        }
    }

    [[nodiscard]] glm::vec3 GetFlashColor() const {
        if (m_FlashColorMode <= 0.5f) return glm::vec3(1.0f, 1.0f, 1.0f);
        if (m_FlashColorMode < 1.5f)  return glm::vec3(1.0f, 0.15f, 0.15f);
        if (m_FlashColorMode < 2.5f)  return glm::vec3(0.35f, 0.75f, 1.5f);
        if (m_FlashColorMode < 3.5f)  return glm::vec3(1.5f, 1.2f, 0.25f);
        const auto packed = static_cast<uint32_t>(m_FlashColorMode - 4.0f);
        const float r = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
        const float g = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
        const float b = static_cast<float>(packed & 0xFF) / 255.0f;
        return glm::vec3(r, g, b);
    }

    bool GetRenderSilhouette() const { return m_RenderSilhouette; }
    void SetRenderSilhouette(bool render) { m_RenderSilhouette = render; }

    const glm::vec4& GetSilhouetteColor() const { return m_SilhouetteColor; }
    void SetSilhouetteColor(const glm::vec4& color) { m_SilhouetteColor = color; }

    [[nodiscard]] glm::vec4 GetCutoutFX() const noexcept {
        return glm::vec4(m_DissolveAmount, m_FlashAmount, m_FlashColorMode, m_DissolveEdgeWidth);
    }

    void ResetCombatFX() noexcept {
        m_DissolveAmount = 0.0f;
        m_DissolveEdgeWidth = 0.08f;
        m_FlashAmount = 0.0f;
        m_FlashColorMode = 0.0f;
        m_FlipX = false;
        m_FlipY = false;
        m_RenderSilhouette = false;
    }

    void OnPoolAcquire() override { ResetCombatFX(); }
    void OnPoolRelease() override { ResetCombatFX(); }

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
    float m_DissolveAmount = 0.0f;
    float m_DissolveEdgeWidth = 0.08f;
    float m_FlashAmount = 0.0f;
    float m_FlashColorMode = 0.0f;
    bool m_RenderSilhouette = false;
    glm::vec4 m_SilhouetteColor{ 0.25f, 0.35f, 0.85f, 0.28f };
    bool m_FlipX = false;
    bool m_FlipY = false;
};
} // namespace Engine

#endif // RENDERER_SPRITERENDERER_H
