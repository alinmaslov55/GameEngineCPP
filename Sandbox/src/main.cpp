#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <iostream>

int main(int argc, char* argv[]) {
    Engine::Application app;

    auto mainScene = std::make_shared<Engine::Scene>("Main Level");

    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    Engine::Transform* transform = player->GetComponent<Engine::Transform>();

    if (transform) {
        transform->Translate(glm::vec3(5.0f, 0.0f, 0.0f));
        std::cout << player->GetName() << " initialized at X: " << transform->position.x << '\n';
    }

    Engine::SceneManager::LoadScene(mainScene);

    app.Run();
    return 0;
}
