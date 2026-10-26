#ifndef IMGUI_IMGUILAYER_H
#define IMGUI_IMGUILAYER_H
#include <SDL3/SDL.h>

namespace Engine {
class ImGuiLayer {
public:
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
};
} // namespace Engine

#endif // IMGUI_IMGUILAYER_H
