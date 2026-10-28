#ifndef SANDBOX_EDITORLAYER_H
#define SANDBOX_EDITORLAYER_H

#include "Panels/ConsolePanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ProfilerPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/SceneViewportPanel.h"
#include "Panels/TilePalettePanel.h"

#include <Engine/Core/Layer.h>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Renderer/Framebuffer.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>

#include <cstdint>
#include <memory>

namespace Sandbox {

class EditorLayer : public Engine::Layer {
public:
    EditorLayer();
    ~EditorLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnImGuiRender() override;

private:
    enum class SceneState {
        Edit = 0,
        Play = 1
    };

    void OnScenePlay();
    void OnSceneStop();

    void RenderMenuBar();
    void RenderSaveLoadModals();
    void RenderToolbar();

    static Engine::GameObject* FindEntityByUUID(const std::shared_ptr<Engine::Scene>& scene, uint64_t uuid);

private:
    std::shared_ptr<Engine::Scene> m_EditorScene;
    std::shared_ptr<Engine::Scene> m_RuntimeScene;
    std::shared_ptr<Engine::Scene> m_ActiveScene;
    SceneState m_SceneState = SceneState::Edit;

    Engine::EditorCamera m_EditorCamera{1280.0f / 720.0f, 3.5f};
    std::shared_ptr<Engine::Framebuffer> m_Framebuffer;
    Engine::GameObject* m_SelectedEntity = nullptr;

    // Save / Load modal state
    bool m_OpenSavePopup = false;
    bool m_OpenLoadPopup = false;
    char m_FilepathBuffer[256] = "assets/level.scene";

    // Workspace panel visibility toggles
    bool m_ShowHierarchy = true;
    bool m_ShowInspector = true;
    bool m_ShowTilePalette = true;
    bool m_ShowAssetBrowser = true;
    bool m_ShowProfiler = true;
    bool m_ShowConsole = true;

    // Modular Editor Panels
    SceneViewportPanel m_ViewportPanel;
    SceneHierarchyPanel m_HierarchyPanel;
    InspectorPanel m_InspectorPanel;
    TilePalettePanel m_TilePalettePanel;
    ContentBrowserPanel m_ContentBrowserPanel;
    ProfilerPanel m_ProfilerPanel;
    ConsolePanel m_ConsolePanel;
};

} // namespace Sandbox

#endif // SANDBOX_EDITORLAYER_H

