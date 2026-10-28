#ifndef SANDBOX_SCENEVIEWPORTPANEL_H
#define SANDBOX_SCENEVIEWPORTPANEL_H

#include "Panels/ContentBrowserPanel.h"
#include "Panels/TilePalettePanel.h"

#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Renderer/Framebuffer.h>
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

    const glm::vec2& GetViewportSize() const { return m_ViewportSize; }

private:
    glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
    bool m_HasActiveSplatStroke = false;
    glm::vec2 m_LastSplatStrokeXZ = { 0.0f, 0.0f };
};

} // namespace Sandbox

#endif // SANDBOX_SCENEVIEWPORTPANEL_H
