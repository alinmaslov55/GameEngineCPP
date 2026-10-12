#include <Engine/Core/Application.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>

#include <iostream>

int main(int argc, char* argv[]) {
    Engine::GameObject player("Player");

    Engine::Transform* transform = player.GetComponent<Engine::Transform>();

    if (transform) {
        transform->Translate(glm::vec3(5.0f, 0.0f, 0.0f));
        std::cout << player.GetName() << " position X: " << transform->position.x << '\n';
    }

    Engine::Application app;
    app.Run();
    return 0;
}
