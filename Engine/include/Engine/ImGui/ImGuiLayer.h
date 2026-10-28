#ifndef IMGUI_IMGUILAYER_H
#define IMGUI_IMGUILAYER_H

#include <SDL3/SDL.h>
#include <filesystem>
#include <functional>
#include <string>

namespace Engine {
class ImGuiLayer {
public:
    using LayoutBuilderFn = std::function<void(unsigned int dockspaceId, float width, float height)>;

    // Initialize ImGui context and backends
    static void Init(SDL_Window* window, SDL_GLContext glContext);

    // Cleanup ImGui
    static void Shutdown();

    // Start a new ImGui frame
    static void Begin();

    // Render the ImGui draw data
    static void End();

    // Sets up the main dockspace that covers the entire window
    static void BeginDockspace();
    static void EndDockspace();

    // Configures where Dear ImGui loads/saves workspace layout settings
    static void SetIniFilePath(const std::filesystem::path& filepath);
    static const std::string& GetIniFilePath();

    // Schedules rebuilding the workspace DockSpace layout on the next frame
    static void ResetWorkspaceLayout();

    // Overrides the default DockBuilder callback (uses ApplyDefaultWorkspaceLayout if unset)
    static void SetLayoutBuilder(LayoutBuilderFn builder);

    // Builds the standard commercial-engine 8-panel DockSpace layout
    static void ApplyDefaultWorkspaceLayout(unsigned int dockspaceId, float width, float height);
};
} // namespace Engine

#endif // IMGUI_IMGUILAYER_H
