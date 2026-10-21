#include <Engine/Core/Application.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/BoxCollider2D.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/SubTexture2D.h>
#include "PlayerController.h"
#include "CameraController.h"

int main(int argc, char* argv[]) {
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Main Level");

    auto spriteSheet = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png");
    auto playerSubTex = Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 1.0f, 1.0f }, { 128.0f, 128.0f });
    auto treeSubTex = Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 2.0f, 1.0f }, { 128.0f, 128.0f }, { 1.0f, 2.0f });

    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    
    auto spriteRenderer = player->AddComponent<Engine::SpriteRenderer>();
    spriteRenderer->SetSubTexture(playerSubTex);

    auto transform = player->GetComponent<Engine::Transform>();
    transform->scale = glm::vec3(0.5f, 0.5f, 1.0f);

    player->AddComponent<PlayerController>();
    auto pCol = player->AddComponent<Engine::BoxCollider2D>();
    pCol->isDynamic = true; 
    pCol->size = { 0.6f, 0.3f };    // Smaller collision box on XZ plane
    pCol->offset = { 0.0f, 0.0f };
    
    auto camController = player->AddComponent<CameraController>();
    
    camController->camera = &mainScene->GetCamera();

    Engine::GameObject* background = mainScene->CreateGameObject("Background");
    auto bgTransform = background->GetComponent<Engine::Transform>();
    bgTransform->position = glm::vec3(2.0f, 0.0f, 1.0f); // Placed further back on the XZ plane
    auto bgSprite = background->AddComponent<Engine::SpriteRenderer>();
    bgSprite->SetColor({ 0.2f, 0.8f, 0.3f, 1.0f }); // Green square

    // Obstacle 1
    Engine::GameObject* tree1 = mainScene->CreateGameObject("Tree1");
    auto t1Transform = tree1->GetComponent<Engine::Transform>();
    t1Transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
    t1Transform->scale = glm::vec3(0.4f, 0.8f, 1.0f);
    auto t1Sprite = tree1->AddComponent<Engine::SpriteRenderer>();
    t1Sprite->SetSubTexture(treeSubTex);
    auto t1Col = tree1->AddComponent<Engine::BoxCollider2D>();
    t1Col->size = { 0.6f, 0.3f };
    t1Col->offset = { 0.0f, 0.0f };

    // Obstacle 2
    Engine::GameObject* tree2 = mainScene->CreateGameObject("Tree2");
    auto t2Transform = tree2->GetComponent<Engine::Transform>();
    t2Transform->position = glm::vec3(0.8f, 0.0f, -0.3f);
    t2Transform->scale = glm::vec3(0.4f, 0.8f, 1.0f);
    auto t2Sprite = tree2->AddComponent<Engine::SpriteRenderer>();
    t2Sprite->SetSubTexture(treeSubTex);
    t2Sprite->SetColor({ 0.8f, 0.6f, 0.4f, 1.0f }); // Tinted sub-texture
    auto t2Col = tree2->AddComponent<Engine::BoxCollider2D>();
    t2Col->size = { 0.6f, 0.3f };
    t2Col->offset = { 0.0f, 0.0f };

    Engine::SceneManager::LoadScene(mainScene);
    app.Run();

    return 0;
}