#include "Runtime/RuntimeLayer.h"

#include "Scene/DemoScene.h"

#include <Engine/Core/Application.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Renderer/Renderer2D.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Utils/Logger.h>

#include <utility>

namespace Sandbox {

RuntimeLayer::RuntimeLayer(std::string startupScenePath)
    : Engine::Layer("RuntimeLayer"),
      m_StartupScenePath(std::move(startupScenePath)) {}

void RuntimeLayer::OnAttach() {
    ENGINE_INFO("Attaching RuntimeLayer (Standalone Distribution Mode)...");
    Engine::ResourceManager::DiscoverAssetRoot("Sandbox");

    m_ActiveScene = std::make_shared<Engine::Scene>("Runtime Level");
    Engine::SceneSerializer deserializer(m_ActiveScene);
    if (!deserializer.Deserialize(m_StartupScenePath)) {
        ENGINE_CRITICAL("Fatal Error: Could not load initial scene '{}'!", m_StartupScenePath);
        m_ActiveScene.reset();
        m_StartupFailed = true;
        Engine::Application::Get().Close();
        return;
    }

    const auto& window = Engine::Application::Get().GetWindow();
    if (window.GetWidth() > 0 && window.GetHeight() > 0) {
        m_ViewportWidth = static_cast<uint32_t>(window.GetWidth());
        m_ViewportHeight = static_cast<uint32_t>(window.GetHeight());
        m_ActiveScene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
    }

    DemoScene::BindRuntimeGameplayScripts(m_ActiveScene);
    Engine::SceneManager::LoadScene(m_ActiveScene);
    m_ActiveScene->OnRuntimeStart();

    ENGINE_INFO("Standalone game started: scene '{}' ({}x{})",
                m_ActiveScene->GetName(), m_ViewportWidth, m_ViewportHeight);
}

void RuntimeLayer::OnDetach() {
    ENGINE_INFO("Detaching RuntimeLayer...");
    if (m_ActiveScene && m_ActiveScene->IsRuntimeRunning()) {
        m_ActiveScene->OnRuntimeStop();
    }
    m_ActiveScene.reset();
}

void RuntimeLayer::OnUpdate(float deltaTime) {
    if (!m_ActiveScene) {
        return;
    }

    // 1. Keep scene camera & UI projection synced with OS window size
    const auto& window = Engine::Application::Get().GetWindow();
    if (window.GetWidth() > 0 && window.GetHeight() > 0) {
        const uint32_t winW = static_cast<uint32_t>(window.GetWidth());
        const uint32_t winH = static_cast<uint32_t>(window.GetHeight());
        if (winW != m_ViewportWidth || winH != m_ViewportHeight) {
            m_ViewportWidth = winW;
            m_ViewportHeight = winH;
            m_ActiveScene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
        }
    }

    // 2. Poll Input & Step Runtime Physics/Scripts
    Engine::Input::Update();
    m_ActiveScene->UpdateRuntime(deltaTime);

    // 3. Render Directly to the OS Window Backbuffer (No Offscreen Framebuffer)
    Engine::Renderer2D::ResetStats();
    Engine::SceneManager::Render(nullptr);
}

} // namespace Sandbox

