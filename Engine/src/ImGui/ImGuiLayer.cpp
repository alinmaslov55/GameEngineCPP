#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Utils/Logger.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

namespace Engine {
void ImGuiLayer::Init(SDL_Window* window, SDL_GLContext glContext) {
    ENGINE_CORE_INFO("Initializing ImGuiLayer...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Enable Keyboard Controls and Docking
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Optional: io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enables tearing off windows outside the main window

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

    // Submit the actual DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
}

void ImGuiLayer::EndDockspace() {
    ENGINE_CORE_TRACE("ImGuiLayer::EndDockspace");
    ImGui::End();
}
} // namespace Engine
