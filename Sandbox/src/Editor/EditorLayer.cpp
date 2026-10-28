#include "Editor/EditorLayer.h"

#include "Scene/DemoScene.h"

#include <Engine/Core/Application.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/ImGui/ImGuiLayer.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/Renderer2D.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <string>

namespace Sandbox {

EditorLayer::EditorLayer()
    : Engine::Layer("EditorLayer") {}

void EditorLayer::OnAttach() {
    ENGINE_INFO("Attaching EditorLayer...");
    Engine::ResourceManager::DiscoverAssetRoot("Sandbox");
    Engine::ImGuiLayer::SetIniFilePath(Engine::ResourceManager::GetAssetRoot() / "editor_layout.ini");

    m_EditorScene = DemoScene::CreateVillageScene();
    m_ActiveScene = m_EditorScene;
    m_SceneState = SceneState::Edit;
    Engine::SceneManager::LoadScene(m_ActiveScene);

    Engine::FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_Framebuffer = std::make_shared<Engine::Framebuffer>(fbSpec);

    m_ContentBrowserPanel.SetRootDirectory(Engine::ResourceManager::GetAssetRoot() / "assets");
}

void EditorLayer::OnDetach() {
    ENGINE_INFO("Detaching EditorLayer...");
    if (m_SceneState == SceneState::Play) {
        OnSceneStop();
    }

    m_SelectedEntity = nullptr;
    m_RuntimeScene.reset();
    m_ActiveScene.reset();
    m_EditorScene.reset();
    m_Framebuffer.reset();
}

void EditorLayer::OnUpdate(float deltaTime) {
    // 1. Resize Framebuffer and Camera projections if the Scene viewport panel changed dimensions
    const glm::vec2 viewportSize = m_ViewportPanel.GetViewportSize();
    if (m_Framebuffer && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
        const auto spec = m_Framebuffer->GetSpecification();
        const uint32_t vpW = static_cast<uint32_t>(viewportSize.x);
        const uint32_t vpH = static_cast<uint32_t>(viewportSize.y);
        if (spec.Width != vpW || spec.Height != vpH) {
            m_Framebuffer->Resize(vpW, vpH);
            m_EditorCamera.OnViewportResize(vpW, vpH);
            if (m_ActiveScene) {
                m_ActiveScene->OnViewportResize(vpW, vpH);
            }
        }
    }

    // 2. Poll Input & Step Runtime Simulation (when in Play Mode)
    Engine::Input::Update();
    if (m_SceneState == SceneState::Play && m_ActiveScene) {
        m_ActiveScene->UpdateRuntime(deltaTime);
    }

    // 3. Render Scene into Offscreen Framebuffer
    if (m_Framebuffer) {
        Engine::Renderer2D::ResetStats();
        m_Framebuffer->Bind();
        Engine::Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        Engine::Renderer::Clear();

        if (m_SceneState == SceneState::Edit) {
            Engine::SceneManager::Render(&m_EditorCamera.GetCamera());
        } else {
            Engine::SceneManager::Render(nullptr);
        }

        m_Framebuffer->Unbind();
    }
}

void EditorLayer::OnImGuiRender() {
    RenderMenuBar();
    RenderSaveLoadModals();
    RenderToolbar();

    m_ViewportPanel.OnImGuiRender(
        m_Framebuffer,
        m_ActiveScene,
        m_EditorCamera,
        m_SceneState == SceneState::Edit,
        m_SelectedEntity,
        m_TilePalettePanel.GetSelectedTileID(),
        m_TilePalettePanel.IsSelectedTileWalkable()
    );

    m_HierarchyPanel.OnImGuiRender(m_ActiveScene, m_SelectedEntity, &m_ShowHierarchy);
    m_InspectorPanel.OnImGuiRender(m_SelectedEntity, &m_ShowInspector);
    m_TilePalettePanel.OnImGuiRender(&m_ShowTilePalette);
    m_ContentBrowserPanel.OnImGuiRender(&m_ShowAssetBrowser);
    m_ProfilerPanel.OnImGuiRender(&m_ShowProfiler);
    m_ConsolePanel.OnImGuiRender(&m_ShowConsole);
}

void EditorLayer::OnScenePlay() {
    if (m_SceneState == SceneState::Play || !m_EditorScene) {
        return;
    }

    const uint64_t selectedUUID = m_SelectedEntity ? m_SelectedEntity->GetUUID() : 0;

    // Snapshot editorScene in memory and clone into runtimeScene
    const std::string snapshot = Engine::SceneSerializer(m_EditorScene).SerializeToString();
    m_RuntimeScene = std::make_shared<Engine::Scene>(m_EditorScene->GetName());
    if (!Engine::SceneSerializer(m_RuntimeScene).DeserializeFromString(snapshot)) {
        ENGINE_ERROR("Failed to clone scene for Play Mode!");
        m_RuntimeScene.reset();
        return;
    }

    const glm::vec2 viewportSize = m_ViewportPanel.GetViewportSize();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
        m_RuntimeScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x),
                                         static_cast<uint32_t>(viewportSize.y));
    }

    DemoScene::BindRuntimeGameplayScripts(m_RuntimeScene);

    m_ActiveScene = m_RuntimeScene;
    Engine::SceneManager::LoadScene(m_ActiveScene);
    m_ActiveScene->OnRuntimeStart();
    m_SelectedEntity = FindEntityByUUID(m_ActiveScene, selectedUUID);
    m_SceneState = SceneState::Play;
    ENGINE_INFO("Entered PLAY mode");
}

void EditorLayer::OnSceneStop() {
    if (m_SceneState == SceneState::Edit) {
        return;
    }

    const uint64_t selectedUUID = m_SelectedEntity ? m_SelectedEntity->GetUUID() : 0;

    if (m_RuntimeScene && m_RuntimeScene->IsRuntimeRunning()) {
        m_RuntimeScene->OnRuntimeStop();
    }

    m_ActiveScene = m_EditorScene;
    const glm::vec2 viewportSize = m_ViewportPanel.GetViewportSize();
    if (m_ActiveScene && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
        m_ActiveScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x),
                                        static_cast<uint32_t>(viewportSize.y));
    }

    Engine::SceneManager::LoadScene(m_ActiveScene);
    m_RuntimeScene.reset();

    m_SelectedEntity = FindEntityByUUID(m_ActiveScene, selectedUUID);
    m_SceneState = SceneState::Edit;
    ENGINE_INFO("Stopped PLAY mode and restored pre-play Editor scene");
}

void EditorLayer::RenderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene As...", nullptr, false, m_SceneState == SceneState::Edit)) {
                m_OpenSavePopup = true;
            }
            if (ImGui::MenuItem("Load Scene...")) {
                m_OpenLoadPopup = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                Engine::Application::Get().Close();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
            ImGui::MenuItem("Tile Palette", nullptr, &m_ShowTilePalette);
            ImGui::MenuItem("Asset Browser", nullptr, &m_ShowAssetBrowser);
            ImGui::MenuItem("Profiler", nullptr, &m_ShowProfiler);
            ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Default Workspace")) {
                m_ShowHierarchy = true;
                m_ShowInspector = true;
                m_ShowTilePalette = true;
                m_ShowAssetBrowser = true;
                m_ShowProfiler = true;
                m_ShowConsole = true;
                Engine::ImGuiLayer::ResetWorkspaceLayout();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void EditorLayer::RenderSaveLoadModals() {
    if (m_OpenSavePopup) {
        ImGui::OpenPopup("Save Scene");
        m_OpenSavePopup = false;
    }

    if (ImGui::BeginPopupModal("Save Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter filepath:");
        ImGui::InputText("##savepath", m_FilepathBuffer, sizeof(m_FilepathBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            Engine::SceneSerializer saveSerializer(m_EditorScene);
            if (saveSerializer.Serialize(m_FilepathBuffer)) {
                ENGINE_INFO("Saved scene to: '{}'", m_FilepathBuffer);
            } else {
                ENGINE_ERROR("Failed to save scene to: '{}'", m_FilepathBuffer);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_OpenLoadPopup) {
        ImGui::OpenPopup("Load Scene");
        m_OpenLoadPopup = false;
    }

    if (ImGui::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter filepath:");
        ImGui::InputText("##loadpath", m_FilepathBuffer, sizeof(m_FilepathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0))) {
            if (m_SceneState == SceneState::Play) {
                OnSceneStop();
            }

            auto loadedScene = std::make_shared<Engine::Scene>("Loaded Level");
            Engine::SceneSerializer deserializer(loadedScene);
            if (deserializer.Deserialize(m_FilepathBuffer)) {
                m_EditorScene = loadedScene;
                m_ActiveScene = m_EditorScene;
                const glm::vec2 viewportSize = m_ViewportPanel.GetViewportSize();
                if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
                    m_ActiveScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x),
                                                    static_cast<uint32_t>(viewportSize.y));
                }
                Engine::SceneManager::LoadScene(m_ActiveScene);
                m_SelectedEntity = nullptr;
                ENGINE_INFO("Loaded scene from: '{}'", m_FilepathBuffer);
            } else {
                ENGINE_ERROR("Failed to load scene from: '{}'", m_FilepathBuffer);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorLayer::RenderToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        if (m_SceneState == SceneState::Edit) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 1.0f));
            if (ImGui::Button("Play", ImVec2(80.0f, 0.0f))) {
                OnScenePlay();
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(80.0f, 0.0f))) {
                OnSceneStop();
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (m_SceneState == SceneState::Edit) {
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "[EDIT MODE]");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Camera")) {
                m_EditorCamera.Reset();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("| MMB/RMB Drag: Pan | Scroll: Zoom (%.2fx)", m_EditorCamera.GetZoomLevel());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[PLAY MODE]");
            ImGui::SameLine();
            ImGui::TextDisabled("| WASD: Move | SPACE: Jump | Changes revert on Stop");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

Engine::GameObject* EditorLayer::FindEntityByUUID(const std::shared_ptr<Engine::Scene>& scene, uint64_t uuid) {
    if (!scene || uuid == 0) {
        return nullptr;
    }
    for (const auto& go : scene->GetGameObjects()) {
        if (go->GetUUID() == uuid) {
            return go.get();
        }
    }
    return nullptr;
}

} // namespace Sandbox

