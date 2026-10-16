#include <Engine/Core/Application.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/Texture.h>

int main(int argc, char* argv[]) {
    Engine::Application app;

    // 1. Create and load a Scene
    auto scene = std::make_shared<Engine::Scene>("Sandbox Scene");
    Engine::SceneManager::LoadScene(scene);

    // 2. Create the Player GameObject inside the Scene
    Engine::GameObject* player = scene->CreateGameObject("Player");

    auto* spriteRenderer = player->AddComponent<Engine::SpriteRenderer>();
    auto texture = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png");
    spriteRenderer->SetTexture(texture);

    auto* transform = player->GetComponent<Engine::Transform>();
    transform->position = glm::vec3(0.5f, 0.0f, 0.0f); // Move right
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);    // Shrink to 50% size

    // 3. Run the application loop (handles Time, SceneManager::Update, and SceneManager::Render)
    app.Run();

    return 0;
}