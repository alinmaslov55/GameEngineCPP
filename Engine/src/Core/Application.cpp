#include "Engine/Core/Application.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Time.h"

namespace Engine {

Application::Application() {
    Initialize();
}

Application::~Application() {
    Shutdown();
}

void Application::Initialize() {
    m_Window = std::make_unique<Window>("Isometric Engine", 1280, 720);
    Time::Init();
}

void Application::Run() {
    while (m_Running) {
        if (m_Window->ShouldClose()) {
            m_Running = false;
        }

        Time::Update();
        Update();
        Render();
    }
}

void Application::Update() {
    m_Window->Update();

    SceneManager::Update(Time::GetDeltaTime());
}

void Application::Render() {
    SceneManager::Render();
}

// @todo Cleanup logic
void Application::Shutdown() {
}
} // Engine
