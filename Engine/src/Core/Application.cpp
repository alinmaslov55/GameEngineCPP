#include "Engine/Core/Application.h"
#include "Engine/Core/Time.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/TerrainRenderer.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Utils/Logger.h"

#ifdef ENGINE_BUILD_EDITOR
#include "Engine/ImGui/ImGuiLayer.h"
#endif

namespace Engine {

Application* Application::s_Instance = nullptr;

Application::Application() {
    s_Instance = this;
    ENGINE_CORE_TRACE("Application constructor called");
    Initialize();
}

Application::~Application() {
    ENGINE_CORE_TRACE("Application destructor called");
    Shutdown();
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
}

void Application::Initialize() {
    Logger::Init();
    ENGINE_CORE_INFO("Initializing Isometric Engine...");

    ResourceManager::DiscoverAssetRoot("Sandbox");

    m_Window = std::make_unique<Window>("Isometric Engine", 1280, 720);
    Time::Init();
    Input::Init();
    AudioManager::Init();
    Renderer::Init();
    Renderer2D::Init();
    TerrainRenderer::Init();
    ShadowMap::Init(2048);
    PostProcessor::Init();

#ifdef ENGINE_BUILD_EDITOR
    ImGuiLayer::Init(m_Window->GetSDLWindow(), m_Window->GetGLContext());
#endif

    ENGINE_CORE_INFO("Isometric Engine initialized successfully");
}

void Application::PushLayer(std::unique_ptr<Layer> layer) {
    if (!layer) {
        return;
    }
    ENGINE_CORE_INFO("Application: Pushing layer '{}'", layer->GetName());
    layer->OnAttach();
    m_Layers.push_back(std::move(layer));
}

void Application::Run() {
    ENGINE_CORE_INFO("Starting application run loop");
    Time::Init();

    while (m_Running) {
        // 1. Process Events
        m_Window->PollEvents();
        if (m_Window->ShouldClose()) {
            ENGINE_CORE_INFO("Window close requested, terminating run loop");
            m_Running = false;
            break;
        }

        // 2. Update Engine / Layers
        Time::Update();
        const float deltaTime = Time::GetDeltaTime();

        if (!m_Layers.empty()) {
#ifdef ENGINE_BUILD_EDITOR
            for (auto& layer : m_Layers) {
                layer->OnUpdate(deltaTime);
            }

            // Prepare default window backbuffer for ImGui presentation
            Renderer::SetViewport(
                0, 0,
                static_cast<uint32_t>(m_Window->GetWidth()),
                static_cast<uint32_t>(m_Window->GetHeight())
            );
            Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            Renderer::Clear();
#else
            // Distribution mode: prepare default OS window backbuffer before layers render directly to it
            Renderer::SetViewport(
                0, 0,
                static_cast<uint32_t>(m_Window->GetWidth()),
                static_cast<uint32_t>(m_Window->GetHeight())
            );
            Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            Renderer::Clear();

            for (auto& layer : m_Layers) {
                layer->OnUpdate(deltaTime);
            }
#endif
        } else {
            Update();
            Renderer::SetViewport(
                0, 0,
                static_cast<uint32_t>(m_Window->GetWidth()),
                static_cast<uint32_t>(m_Window->GetHeight())
            );
            Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
            Renderer::Clear();
            Renderer2D::ResetStats();
            SceneManager::Render();
        }

#ifdef ENGINE_BUILD_EDITOR
        // 3. Render UI Layer & Dockspace
        ImGuiLayer::Begin();
        ImGuiLayer::BeginDockspace();

        for (auto& layer : m_Layers) {
            layer->OnImGuiRender();
        }
        if (m_ImGuiRenderCallback) {
            m_ImGuiRenderCallback();
        }

        ImGuiLayer::EndDockspace();
        ImGuiLayer::End();
#endif

        // 4. Swap Buffers
        m_Window->SwapBuffers();
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
    return m_Running && !m_Window->ShouldClose();
}

void Application::Close() {
    ENGINE_CORE_INFO("Application close requested");
    m_Running = false;
}

void Application::Shutdown() {
    ENGINE_CORE_INFO("Shutting down Isometric Engine...");

    // Detach and destroy layers first while OpenGL context and subsystems are still active
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it) {
        if (*it) {
            (*it)->OnDetach();
        }
    }
    m_Layers.clear();

    SceneManager::Shutdown();

    // Clear caches to gracefully destroy OpenGL textures and SDL Audio buffers
    ResourceManager::ClearType<Texture>();
    ResourceManager::ClearType<AudioClip>();

#ifdef ENGINE_BUILD_EDITOR
    ImGuiLayer::Shutdown();
#endif

    PostProcessor::Shutdown();
    ShadowMap::Shutdown();
    TerrainRenderer::Shutdown();
    Renderer2D::Shutdown();
    AudioManager::Shutdown();
    Input::Shutdown();
    m_Window.reset();
    ENGINE_CORE_INFO("Isometric Engine shutdown complete");
    Logger::Shutdown();
}
} // namespace Engine
