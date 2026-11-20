#ifndef SANDBOX_SCENEVIEWPORTPANEL_H
#define SANDBOX_SCENEVIEWPORTPANEL_H

#include "Panels/ContentBrowserPanel.h"
#include "Panels/TilePalettePanel.h"

#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Renderer/RHI/Framebuffer.h>
#include <Engine/Renderer/ViewportAspectSimulator.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace Sandbox {

class SceneViewportPanel {
public:
    void OnImGuiRender(const std::shared_ptr<Engine::Framebuffer>& framebuffer,
                       const std::shared_ptr<Engine::Scene>& activeScene,
                       Engine::EditorCamera& editorCamera,
                       bool isEditMode,
                       Engine::GameObject*& selectedEntity,
                       TilePalettePanel& palettePanel,
                       const ContentBrowserPanel& contentBrowser);

    void SetAspectMode(Engine::ViewportAspectMode mode) noexcept { m_AspectMode = mode; }
    [[nodiscard]] Engine::ViewportAspectMode GetAspectMode() const noexcept { return m_AspectMode; }

    void SetShowSafeArea(bool show) noexcept { m_ShowSafeArea = show; }
    [[nodiscard]] bool IsShowingSafeArea() const noexcept { return m_ShowSafeArea; }

    [[nodiscard]] const glm::vec2& GetViewportSize() const noexcept { return m_ViewportSize; }
    [[nodiscard]] const glm::vec2& GetViewportOffset() const noexcept { return m_ViewportOffset; }

private:
    glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
    glm::vec2 m_ViewportOffset = { 0.0f, 0.0f };
    Engine::ViewportAspectMode m_AspectMode = Engine::ViewportAspectMode::Free;
    bool m_ShowSafeArea = false;
    bool m_HasActiveSplatStroke = false;
    glm::vec2 m_LastSplatStrokeXZ = { 0.0f, 0.0f };
};

} // namespace Sandbox

#endif // SANDBOX_SCENEVIEWPORTPANEL_H
