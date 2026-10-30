#include <gtest/gtest.h>

#ifdef ENGINE_BUILD_EDITOR
#include "Engine/ImGui/ImGuiLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <string>

namespace {

TEST(EditorWorkspaceTest, DefaultWorkspaceDocksAllPanelsAndRoundTripsIni) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const std::filesystem::path tempIniPath = std::filesystem::temp_directory_path() / "engine_test_editor_layout.ini";
    Engine::ImGuiLayer::SetIniFilePath(tempIniPath);
    ASSERT_NE(io.IniFilename, nullptr);
    EXPECT_EQ(std::string(io.IniFilename), Engine::ImGuiLayer::GetIniFilePath());

    const ImGuiID dockspaceId = ImHashStr("MainDockSpace");
    EXPECT_EQ(ImGui::DockBuilderGetNode(dockspaceId), nullptr);

    // Build programmatic Default Workspace at 1280x720
    Engine::ImGuiLayer::ApplyDefaultWorkspaceLayout(dockspaceId, 1280.0f, 720.0f);

    ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
    ASSERT_NE(rootNode, nullptr);
    EXPECT_TRUE(rootNode->IsSplitNode());

    ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId);
    ASSERT_NE(centralNode, nullptr);

    const char* expectedPanels[] = {
        "Toolbar", "Scene", "Hierarchy", "Inspector",
        "Tile Palette", "Asset Browser", "Profiler", "Console"
    };
    for (const char* panelName : expectedPanels) {
        ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(panelName));
        ASSERT_NE(settings, nullptr) << "Missing window settings for panel: " << panelName;
        EXPECT_NE(settings->DockId, 0u) << "Panel was not docked: " << panelName;
    }

    // Scene must occupy the Central Node
    ImGuiWindowSettings* sceneSettings = ImGui::FindWindowSettingsByID(ImHashStr("Scene"));
    EXPECT_EQ(sceneSettings->DockId, centralNode->ID);

    // Asset Browser and Console must be tabbed together in the same bottom dock node
    ImGuiWindowSettings* assetSettings = ImGui::FindWindowSettingsByID(ImHashStr("Asset Browser"));
    ImGuiWindowSettings* consoleSettings = ImGui::FindWindowSettingsByID(ImHashStr("Console"));
    EXPECT_EQ(assetSettings->DockId, consoleSettings->DockId);

    // Toolbar dock node must enforce NoTabBar and NoResizeY
    ImGuiWindowSettings* toolbarSettings = ImGui::FindWindowSettingsByID(ImHashStr("Toolbar"));
    ImGuiDockNode* toolbarNode = ImGui::DockBuilderGetNode(toolbarSettings->DockId);
    ASSERT_NE(toolbarNode, nullptr);
    EXPECT_NE(toolbarNode->LocalFlags & ImGuiDockNodeFlags_NoTabBar, 0);
    EXPECT_NE(toolbarNode->LocalFlags & ImGuiDockNodeFlags_NoResizeY, 0);

    // Verify .ini serialization round-trip preserves MainDockSpace across sessions
    size_t iniSize = 0;
    const char* serializedIni = ImGui::SaveIniSettingsToMemory(&iniSize);
    ASSERT_NE(serializedIni, nullptr);
    const std::string iniSnapshot(serializedIni, iniSize);
    EXPECT_NE(iniSnapshot.find("[Docking][Data]"), std::string::npos);

    Engine::ImGuiLayer::SetIniFilePath({});
    ImGui::DestroyContext(ctx);

    ImGuiContext* reloadedCtx = ImGui::CreateContext();
    ImGuiIO& reloadedIo = ImGui::GetIO();
    reloadedIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    reloadedIo.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    reloadedIo.DisplaySize = ImVec2(1280.0f, 720.0f);
    reloadedIo.DeltaTime = 1.0f / 60.0f;
    reloadedIo.IniFilename = nullptr;
    ImGui::LoadIniSettingsFromMemory(iniSnapshot.c_str(), iniSnapshot.size());

    // Simulate a live frame + ResetWorkspaceLayout trigger through BeginDockspace/EndDockspace
    ImGui::NewFrame();
    Engine::ImGuiLayer::BeginDockspace();
    const ImGuiID liveDockspaceId = ImGui::GetID("MainDockSpace");
    EXPECT_NE(ImGui::DockBuilderGetNode(liveDockspaceId), nullptr);
    Engine::ImGuiLayer::EndDockspace();
    ImGui::EndFrame();

    Engine::ImGuiLayer::ResetWorkspaceLayout();
    ImGui::NewFrame();
    Engine::ImGuiLayer::BeginDockspace();
    ASSERT_NE(ImGui::DockBuilderGetCentralNode(liveDockspaceId), nullptr);
    Engine::ImGuiLayer::EndDockspace();
    ImGui::EndFrame();

    ImGui::DestroyContext(reloadedCtx);

    std::error_code ec;
    std::filesystem::remove(tempIniPath, ec);
}

} // namespace
#endif

