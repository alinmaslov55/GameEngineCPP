#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include "PlayerController.h"
#include "CameraController.h"

int main(int argc, char* argv[]) {
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Main Level");

    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    
    auto spriteRenderer = player->AddComponent<Engine::SpriteRenderer>();
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png"); 
    spriteRenderer->SetTexture(texture);

    auto transform = player->GetComponent<Engine::Transform>();
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);

    player->AddComponent<PlayerController>();

    auto camController = player->AddComponent<CameraController>();
    
    camController->camera = &mainScene->GetCamera();

    Engine::GameObject* background = mainScene->CreateGameObject("Background");
    auto bgTransform = background->GetComponent<Engine::Transform>();
    bgTransform->position = glm::vec3(2.0f, 1.0f, -0.1f); // Placed off to the top right
    auto bgSprite = background->AddComponent<Engine::SpriteRenderer>();
    bgSprite->SetColor({ 0.2f, 0.8f, 0.3f, 1.0f }); // Green square

    Engine::SceneManager::LoadScene(mainScene);
    app.Run();

    return 0;
}