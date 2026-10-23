#include "Engine/Core/Application.h"
#include "Engine/Core/Time.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Utils/Logger.h"

namespace Engine {

Application::Application() {
    ENGINE_CORE_TRACE("Application constructor called");
    Initialize();
}

Application::~Application() {
    ENGINE_CORE_TRACE("Application destructor called");
    Shutdown();
}

void Application::Initialize() {
    Logger::Init();
    ENGINE_CORE_INFO("Initializing Isometric Engine...");

    m_Window = std::make_unique<Window>("Isometric Engine", 1280, 720);
    Time::Init();
    Input::Init();
    AudioManager::Init();
    Renderer::Init();
    Renderer2D::Init();
    ENGINE_CORE_INFO("Isometric Engine initialized successfully");
}

void Application::Run() {
    ENGINE_CORE_INFO("Starting application run loop");
    Time::Init();

    while (m_Running) {
        m_Window->PollEvents();
        if (m_Window->ShouldClose()) {
            ENGINE_CORE_INFO("Window close requested, terminating run loop");
            m_Running = false;
            break;
        }

        Time::Update();
        Update();
        Render();
    }
    ENGINE_CORE_INFO("Application run loop finished");
}

void Application::Update() {
    ENGINE_CORE_TRACE("Application::Update");
    Input::Update();
    SceneManager::Update(Time::GetDeltaTime());
}

void Application::Render() {
    ENGINE_CORE_TRACE("Application::Render");
    Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    Renderer::Clear();

    SceneManager::Render();
    m_Window->SwapBuffers();
}

bool Application::IsRunning() const {
    return !m_Window->ShouldClose(); 
}

void Application::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Isometric Engine...");
    SceneManager::Shutdown();

    // Clear caches to gracefully destroy OpenGL textures and SDL Audio buffers
    ResourceManager::ClearType<Texture>();
    ResourceManager::ClearType<AudioClip>();

    Renderer2D::Shutdown();
    AudioManager::Shutdown();
    Input::Shutdown();
    m_Window.reset();
    ENGINE_CORE_INFO("Isometric Engine shutdown complete");
    Logger::Shutdown();
}
} // namespace Engine
