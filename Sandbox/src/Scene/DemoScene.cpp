#include "Scene/DemoScene.h"

#include "CameraController.h"
#include "PlayerController.h"

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Math/Isometric.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/SubTexture2D.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/UI/Font.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>

#include <glm/glm.hpp>

namespace Sandbox {

std::shared_ptr<Engine::Scene> DemoScene::CreateVillageScene() {
    auto editorScene = std::make_shared<Engine::Scene>("Village");
    editorScene->SetOrthographicSize(5.0f);

    // 1. Player GameObject
    Engine::GameObject* player = editorScene->CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = glm::vec3(5.0f, 0.0f, 2.0f);
    player->AddComponent<Engine::SpriteRenderer>();
    player->AddComponent<Engine::ParticleSystem>(500); // Pool of 500 particles

    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->size = { 0.6f, 0.6f };
    playerCollider->isDynamic = true;
    playerCollider->useGravity = true;

    auto* playerAudio = player->AddComponent<Engine::AudioSource>();
    playerAudio->SetClip(Engine::ResourceManager::Load<Engine::AudioClip>("jump.wav"));

    // 2. Environment / Tilemap GameObject
    Engine::GameObject* mapObject = editorScene->CreateGameObject("Map");
    auto* tilemapComponent = mapObject->AddComponent<Engine::Tilemap>(10, 10);
    auto atlas = Engine::ResourceManager::Load<Engine::Texture>("test.png");
    auto grassTile = Engine::SubTexture2D::CreateFromCoords(atlas, {0, 0}, {128.0f, 128.0f});
    auto stoneTile = Engine::SubTexture2D::CreateFromCoords(atlas, {1, 0}, {128.0f, 128.0f});
    tilemapComponent->tileSet->AddTile(1, grassTile);
    tilemapComponent->tileSet->AddTile(2, stoneTile);

    for (uint32_t y = 0; y < tilemapComponent->GetHeight(); ++y) {
        for (uint32_t x = 0; x < tilemapComponent->GetWidth(); ++x) {
            tilemapComponent->SetTile(x, y, 1, true);
        }
    }
    // Add non-walkable Stone obstacle tiles to demonstrate Tilemap collision
    tilemapComponent->SetTile(4, 3, 2, false);
    tilemapComponent->SetTile(6, 3, 2, false);

    // 3. UI HUD Canvas GameObject
    Engine::GameObject* hud = editorScene->CreateGameObject("HUD");

    // Health Bar Background (Dark Gray)
    hud->AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 650.0f),
        glm::vec2(200.0f, 20.0f),
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f)
    );

    // Health Bar Foreground (Red)
    hud->AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 650.0f),
        glm::vec2(150.0f, 20.0f),
        glm::vec4(0.8f, 0.1f, 0.1f, 1.0f)
    );

    // Basic Font (16x16 pixel grid on font_atlas.png)
    auto fontTexture = Engine::ResourceManager::Load<Engine::Texture>("font_atlas.png");
    auto mainFont = std::make_shared<Engine::Font>();
    mainFont->atlasTexture = fontTexture;

    Engine::Character charH = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {0, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charP = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {1, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charColon = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {2, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };

    mainFont->AddCharacter('H', charH);
    mainFont->AddCharacter('P', charP);
    mainFont->AddCharacter(':', charColon);

    hud->AddComponent<Engine::UILabel>(
        "HP:",
        mainFont,
        glm::vec2(60.0f, 652.0f)
    );

    // Persist initial scene to disk
    Engine::SceneSerializer serializer(editorScene);
    serializer.Serialize("Village.scene");

    return editorScene;
}

void DemoScene::BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene) {
    if (!runtimeScene) {
        return;
    }

    for (const auto& goPtr : runtimeScene->GetGameObjects()) {
        if (goPtr->GetName() == "Player") {
            if (!goPtr->GetComponent<PlayerController>()) {
                goPtr->AddComponent<PlayerController>();
            }
            if (!goPtr->GetComponent<CameraController>()) {
                auto* camCtrl = goPtr->AddComponent<CameraController>();
                camCtrl->camera = &runtimeScene->GetCamera();
                glm::vec2 isoPos = Engine::Isometric::WorldToScreen(goPtr->GetTransform()->position);
                runtimeScene->GetCamera().SetPosition({ isoPos.x, isoPos.y, 0.0f });
            }
        }
    }
}

} // namespace Sandbox

