#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/CameraComponent.h>
#include <Engine/Renderer/SpriteRenderer.h>

int main(int argc, char* argv[]) {
    Engine::Application app;

    auto mainScene = std::make_shared<Engine::Scene>("Main Level");

    Engine::GameObject* cameraObj = mainScene->CreateGameObject("MainCamera");
    cameraObj->AddComponent<Engine::CameraComponent>();

    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    
    auto spriteRenderer = player->AddComponent<Engine::SpriteRenderer>();
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png"); // Use your path
    spriteRenderer->SetTexture(texture);

    auto transform = player->GetComponent<Engine::Transform>();
    transform->position = glm::vec3(0.5f, 0.0f, 0.0f);
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);

    Engine::SceneManager::LoadScene(mainScene);

    while (app.IsRunning()) {
        app.Update(); 
        app.Render();
    }

    return 0;
}