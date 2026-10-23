#include <Engine/Animation/Animator.h>
#include <Engine/Animation/AnimationClip.h>
#include <Engine/Core/Application.h>
#include <Engine/Scene/BoxCollider2D.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/SubTexture2D.h>
#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioSource.h>

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

    // Create the Animator
    auto animator = player->AddComponent<Engine::Animator>();

    // Setup an Idle Clip (using column 0 of your atlas)
    auto idleClip = std::make_shared<Engine::AnimationClip>("Idle", 0.5f, true);
    idleClip->AddFrame(Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 0.0f, 0.0f }, { 128.0f, 128.0f }));
    idleClip->AddFrame(Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 0.0f, 1.0f }, { 128.0f, 128.0f }));
    
    // Setup a Walk Clip (using column 1 of your atlas, faster frame rate)
    auto walkClip = std::make_shared<Engine::AnimationClip>("Walk", 0.15f, true);
    walkClip->AddFrame(Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 1.0f, 0.0f }, { 128.0f, 128.0f }));
    walkClip->AddFrame(Engine::SubTexture2D::CreateFromCoords(spriteSheet, { 1.0f, 1.0f }, { 128.0f, 128.0f }));

    auto audioSource = player->AddComponent<Engine::AudioSource>();
    auto jumpSound = std::make_shared<Engine::AudioClip>("C:/DEV/GameEngineCPP/Sandbox/jump.wav");
    audioSource->SetClip(jumpSound);

    animator->AddClip(idleClip);
    animator->AddClip(walkClip);
    animator->Play("Idle"); // Set default state

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

    // Create the Environment/Map GameObject
    Engine::GameObject* mapObject = mainScene->CreateGameObject("Map");
    auto mapTransform = mapObject->GetComponent<Engine::Transform>();
    
    // Scale the tiles to match your art style
    mapTransform->scale = glm::vec3(1.0f, 1.0f, 1.0f); 

    // Initialize a 10x10 Tilemap component
    auto tilemap = mapObject->AddComponent<Engine::Tilemap>(10, 10);

    // Define the TileSet
    // For now, we will reuse your test.png as a "grass" tile SubTexture
    auto atlas = std::make_shared<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png");
    auto grassTile = Engine::SubTexture2D::CreateFromCoords(atlas, {0, 0}, {atlas->GetWidth(), atlas->GetHeight()});
    
    // Register ID 1 as Grass
    tilemap->tileSet->AddTile(1, grassTile);

    // 4. Paint the Grid
    for (uint32_t y = 0; y < tilemap->GetHeight(); y++) {
        for (uint32_t x = 0; x < tilemap->GetWidth(); x++) {
            // Paint a 10x10 square of grass
            tilemap->SetTile(x, y, 1);
        }
    }

    Engine::SceneManager::LoadScene(mainScene);
    app.Run();

    return 0;
}