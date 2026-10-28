#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace Engine {

namespace {
std::string s_IniFilePath;
bool s_ResetLayoutRequested = false;
ImGuiLayer::LayoutBuilderFn s_LayoutBuilder = nullptr;
} // namespace

void ImGuiLayer::Init(SDL_Window* window, SDL_GLContext glContext) {
    ENGINE_CORE_INFO("Initializing ImGuiLayer...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable Keyboard Controls and Docking
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Optional: io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enables tearing off windows outside the main window

    if (!s_IniFilePath.empty()) {
        io.IniFilename = s_IniFilePath.c_str();
    }
    s_ResetLayoutRequested = false;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplSDL3_InitForOpenGL(window, glContext)) {
        ENGINE_CORE_ERROR("Failed to initialize ImGui SDL3 backend for OpenGL");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 410 core")) {
        ENGINE_CORE_ERROR("Failed to initialize ImGui OpenGL3 backend");
    }

    ENGINE_CORE_INFO("ImGuiLayer initialized successfully (Keyboard Nav & Docking enabled)");
}

void ImGuiLayer::Shutdown() {
    ENGINE_CORE_INFO("Shutting down ImGuiLayer...");
    s_ResetLayoutRequested = false;
    s_LayoutBuilder = nullptr;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ENGINE_CORE_INFO("ImGuiLayer shut down");
}

void ImGuiLayer::Begin() {
    ENGINE_CORE_TRACE("ImGuiLayer::Begin");
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::End() {
    ENGINE_CORE_TRACE("ImGuiLayer::End");
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLayer::SetIniFilePath(const std::filesystem::path& filepath) {
    if (filepath.empty()) {
        s_IniFilePath.clear();
        if (ImGui::GetCurrentContext()) {
            ImGui::GetIO().IniFilename = nullptr;
        }
        return;
    }

    s_IniFilePath = filepath.lexically_normal().generic_string();
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().IniFilename = s_IniFilePath.c_str();
        std::error_code ec;
        if (GImGui && GImGui->SettingsLoaded && std::filesystem::exists(filepath, ec)) {
            ImGui::LoadIniSettingsFromDisk(s_IniFilePath.c_str());
        }
    }
    ENGINE_CORE_INFO("ImGuiLayer: Workspace layout file set to '{}'", s_IniFilePath);
}

const std::string& ImGuiLayer::GetIniFilePath() {
    return s_IniFilePath;
}

void ImGuiLayer::ResetWorkspaceLayout() {
    s_ResetLayoutRequested = true;
    ENGINE_CORE_INFO("ImGuiLayer: Scheduled reset to Default Workspace layout");
}

void ImGuiLayer::SetLayoutBuilder(LayoutBuilderFn builder) {
    s_LayoutBuilder = std::move(builder);
}

void ImGuiLayer::ApplyDefaultWorkspaceLayout(unsigned int dockspaceId, float width, float height) {
    const ImVec2 workSize(std::max(width, 640.0f), std::max(height, 360.0f));

    ImGui::DockBuilderRemoveNode(dockspaceId);
    if (GImGui && GImGui->CurrentWindow != nullptr) {
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    } else {
        ImGui::DockBuilderAddNode(dockspaceId, 0);
        if (ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId)) {
            rootNode->SetLocalFlags(ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_CentralNode);
        }
    }
    ImGui::DockBuilderSetNodeSize(dockspaceId, workSize);

    ImGuiID dockMainId = dockspaceId;

    // 1. Top Toolbar strip (~40px height across resolutions, tabless, fixed vertical height)
    const float toolbarRatio = std::clamp(40.0f / workSize.y, 0.025f, 0.07f);
    ImGuiID dockTopId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, toolbarRatio, nullptr, &dockMainId);
    if (ImGuiDockNode* topNode = ImGui::DockBuilderGetNode(dockTopId)) {
        topNode->SetLocalFlags(topNode->LocalFlags | ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoResizeY);
    }

    // 2. Left Column (18% width): Hierarchy (top 62%) + Tile Palette (bottom 38%)
    ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.18f, nullptr, &dockMainId);
    ImGuiID dockLeftBottomId = ImGui::DockBuilderSplitNode(dockLeftId, ImGuiDir_Down, 0.38f, nullptr, &dockLeftId);

    // 3. Right Column (29% of remaining ~82% = ~23.8% total width): Inspector (top 76%) + Profiler (bottom 24%)
    ImGuiID dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.29f, nullptr, &dockMainId);
    ImGuiID dockRightBottomId = ImGui::DockBuilderSplitNode(dockRightId, ImGuiDir_Down, 0.24f, nullptr, &dockRightId);

    // 4. Center Column: Bottom Dock (28% height for Asset Browser & Console tabs) + Scene (72% Central Node)
    ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.28f, nullptr, &dockMainId);

    // 5. Dock all 8 editor panels
    ImGui::DockBuilderDockWindow("Toolbar", dockTopId);
    ImGui::DockBuilderDockWindow("Hierarchy", dockLeftId);
    ImGui::DockBuilderDockWindow("Tile Palette", dockLeftBottomId);
    ImGui::DockBuilderDockWindow("Inspector", dockRightId);
    ImGui::DockBuilderDockWindow("Profiler", dockRightBottomId);
    ImGui::DockBuilderDockWindow("Asset Browser", dockBottomId);
    ImGui::DockBuilderDockWindow("Console", dockBottomId);
    ImGui::DockBuilderDockWindow("Scene", dockMainId);

    ImGui::DockBuilderFinish(dockspaceId);
    ENGINE_CORE_INFO("ImGuiLayer: Applied Default Workspace layout ({:.0f}x{:.0f})", workSize.x, workSize.y);
}

void ImGuiLayer::BeginDockspace() {
    ENGINE_CORE_TRACE("ImGuiLayer::BeginDockspace");
    // Create a fullscreen window with no title bar or borders to act as the dockspace host
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Editor Dockspace", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // Submit the actual DockSpace (building the Default Workspace first if needed)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        const bool needsLayoutBuild = (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) || s_ResetLayoutRequested;
        if (needsLayoutBuild) {
            s_ResetLayoutRequested = false;
            if (s_LayoutBuilder) {
                s_LayoutBuilder(dockspace_id, viewport->WorkSize.x, viewport->WorkSize.y);
            } else {
                ApplyDefaultWorkspaceLayout(dockspace_id, viewport->WorkSize.x, viewport->WorkSize.y);
            }
            ImGui::MarkIniSettingsDirty();
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
}

void ImGuiLayer::EndDockspace() {
    ENGINE_CORE_TRACE("ImGuiLayer::EndDockspace");
    ImGui::End();
}

} // namespace Engine
