#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Renderer/SpriteRenderer.h>

int main(int argc, char* argv[]) {
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Village");

    // 1. Hardcode a scene once
    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    auto transform = player->GetComponent<Engine::Transform>();
    transform->position = glm::vec3(5.0f, 0.0f, 2.0f);
    player->AddComponent<Engine::SpriteRenderer>();

    // 2. Serialize it to disk
    Engine::SceneSerializer serializer(mainScene);
    serializer.Serialize("C:/DEV/GameEngineCPP/Sandbox/Village.scene");

    // 3. Create a brand new empty scene
    auto loadedScene = std::make_shared<Engine::Scene>("Empty");
    Engine::SceneSerializer deserializer(loadedScene);
    
    // 4. Load the file back into the engine
    deserializer.Deserialize("C:/DEV/GameEngineCPP/Sandbox/Village.scene");

    // Load the deserialized scene into the game loop
    Engine::SceneManager::LoadScene(loadedScene);
    app.Run();

    return 0;
}