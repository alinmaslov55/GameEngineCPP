#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include "PlayerController.h"

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

    Engine::SceneManager::LoadScene(mainScene);
    app.Run();

    return 0;
}