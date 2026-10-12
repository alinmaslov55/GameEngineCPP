#include "Engine/Core/Application.h"
#include "Engine/Scene/SceneManager.h"

namespace Engine {

Application::Application() {
    Initialize();
}

Application::~Application() {
    Shutdown();
}

void Application::Initialize() {
    m_Window = std::make_unique<Window>("Isometric Engine", 1280, 720);
}

void Application::Run() {
    while (m_Running) {
        if (m_Window->ShouldClose()) {
            m_Running = false;
        }

        Update();
        Render();
    }
}

void Application::Update() {
    m_Window->Update();

    float deltaTime = 0.016f;
    SceneManager::Update(deltaTime);
}

void Application::Render() {
    SceneManager::Render();
}

// @todo Cleanup logic
void Application::Shutdown() {
}
} // Engine
