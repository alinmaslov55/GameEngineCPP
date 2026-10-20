#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/CameraComponent.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Utils/Logger.h>
#include "PlayerController.h"

int main(int argc, char* argv[]) {
    ENGINE_INFO("Starting Sandbox Application...");
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Main Level");

    Engine::GameObject* camera = mainScene->CreateGameObject("Main Camera");
    camera->AddComponent<Engine::CameraComponent>();

    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    
    auto spriteRenderer = player->AddComponent<Engine::SpriteRenderer>();
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png"); 
    spriteRenderer->SetTexture(texture);

    auto transform = player->GetComponent<Engine::Transform>();
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);

    player->AddComponent<PlayerController>();

    Engine::SceneManager::LoadScene(mainScene);
    ENGINE_INFO("Sandbox setup complete. Entering application run loop...");
    app.Run();
    ENGINE_INFO("Sandbox application exited gracefully");

    return 0;
}