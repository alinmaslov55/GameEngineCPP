#include "Scene/DemoScene.h"

#include "CameraController.h"
#include "PlayerController.h"

#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/SubTexture2D.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Scene/TerrainComponent.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/UI/Font.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/UIPanel.h>

#include <glm/glm.hpp>
#include <string>

namespace Sandbox {

namespace {
std::shared_ptr<Engine::SubTexture2D> MakeQuadrantTile(const std::shared_ptr<Engine::Texture>& atlas,
                                                       int col,
                                                       int row) {
    constexpr float kInset = 0.004f;
    const glm::vec2 minUV = { col * 0.5f + kInset, row * 0.5f + kInset };
    const glm::vec2 maxUV = { (col + 1) * 0.5f - kInset, (row + 1) * 0.5f - kInset };
    return std::make_shared<Engine::SubTexture2D>(atlas, minUV, maxUV);
}
} // namespace

std::shared_ptr<Engine::Scene> DemoScene::CreateVillageScene() {
    auto editorScene = std::make_shared<Engine::Scene>("Cult Diorama Clearing");
    auto& cam = editorScene->GetCamera();
    cam.SetFOVDegrees(32.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);
    cam.SetDistance(15.0f);
    cam.SetFocalPoint({ 6.5f, 0.0f, 7.0f });

    // 1A. Organic 4-Layer Splatmap Terrain Clearing ([-1.5, 14.5] x [-1.5, 14.5])
    Engine::GameObject* terrainObject = editorScene->CreateGameObject("Terrain");
    const glm::vec3 terrainOrigin = { -1.5f, 0.0f, -1.5f };
    const glm::vec3 terrainScale  = {  1.0f, 1.0f,  1.0f };
    terrainObject->GetTransform()->position = terrainOrigin;

    auto* terrain = terrainObject->AddComponent<Engine::TerrainComponent>(16.0f, 16.0f, 128u, 128u);
    terrain->SetBlendSharpness(0.68f);
    terrain->SetEdgeDarkening(0.16f);
    terrain->SetSplatmapPath("assets/diorama/village_splatmap.png");

    terrain->SetLayerTexture(0, Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/grass_seamless.png"));
    terrain->SetLayerTilingScale(0, 0.42f);

    terrain->SetLayerTexture(1, Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/dirt_seamless.png"));
    terrain->SetLayerTilingScale(1, 0.45f);

    terrain->SetLayerTexture(2, Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/cobble_seamless.png"));
    terrain->SetLayerTilingScale(2, 0.48f);

    terrain->SetLayerTexture(3, Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/soil_seamless.png"));
    terrain->SetLayerTilingScale(3, 0.50f);

    // Base fill: Lush Meadow Grass (Layer 0)
    terrain->FillLayer(0);

    // Forest perimeter mulch hugging the northern & side pine treeline (Layer 3)
    terrain->PaintWorldSegment({ 0.4f, 0.8f }, { 12.6f, 0.9f }, terrainOrigin, terrainScale, 3u, 1.65f, 0.72f, 0.90f);
    terrain->PaintWorldSegment({ 0.6f, 1.2f }, {  0.9f, 11.0f }, terrainOrigin, terrainScale, 3u, 1.35f, 0.75f, 0.85f);
    terrain->PaintWorldSegment({ 12.2f, 1.2f }, { 11.7f, 10.2f }, terrainOrigin, terrainScale, 3u, 1.35f, 0.75f, 0.85f);

    // West tilled garden soil beds beneath the Berry Bushes (Layer 3)
    terrain->PaintWorldSegment({ 2.2f, 5.4f }, { 2.2f, 9.5f }, terrainOrigin, terrainScale, 3u, 1.05f, 0.55f, 0.95f);
    terrain->PaintWorldSegment({ 3.8f, 5.4f }, { 3.8f, 9.5f }, terrainOrigin, terrainScale, 3u, 1.05f, 0.55f, 0.95f);

    // Winding village dirt trails (Layer 1)
    terrain->PaintWorldSegment({ 6.4f, 13.4f }, { 6.8f, 10.2f }, terrainOrigin, terrainScale, 1u, 1.30f, 0.65f, 0.95f);
    terrain->PaintWorldSegment({ 6.8f, 10.2f }, { 6.5f,  7.4f }, terrainOrigin, terrainScale, 1u, 1.35f, 0.65f, 0.95f);
    terrain->PaintWorldSegment({ 8.0f,  6.0f }, { 10.4f, 6.7f }, terrainOrigin, terrainScale, 1u, 1.15f, 0.68f, 0.92f);
    terrain->PaintWorldSegment({ 10.4f, 6.7f }, { 12.6f, 6.2f }, terrainOrigin, terrainScale, 1u, 1.10f, 0.68f, 0.90f);
    terrain->PaintWorldSegment({ 5.2f,  6.5f }, { 4.3f,  7.4f }, terrainOrigin, terrainScale, 1u, 0.95f, 0.70f, 0.88f);

    // Central Mossy Cobblestone Shrine Plaza (Layer 2)
    terrain->PaintWorldBrush(6.5f, 5.5f, terrainOrigin, terrainScale, 2u, 2.30f, 0.52f, 1.0f);
    terrain->PaintWorldBrush(6.5f, 7.2f, terrainOrigin, terrainScale, 2u, 1.40f, 0.58f, 0.92f);

    // 1B. Coexisting Walkability / Discrete Overlay Tilemap (14x14 XZ grid, ID 0 = transparent walkable)
    Engine::GameObject* mapObject = editorScene->CreateGameObject("Map");
    auto* tilemap = mapObject->AddComponent<Engine::Tilemap>(14, 14);
    auto groundAtlas = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/ground_atlas.png");

    tilemap->tileSet->AddTile(1, MakeQuadrantTile(groundAtlas, 0, 1));
    tilemap->tileSet->AddTile(2, MakeQuadrantTile(groundAtlas, 1, 1));
    tilemap->tileSet->AddTile(3, MakeQuadrantTile(groundAtlas, 0, 0));
    tilemap->tileSet->AddTile(4, MakeQuadrantTile(groundAtlas, 1, 0));

    for (uint32_t z = 0; z < tilemap->GetHeight(); ++z) {
        for (uint32_t x = 0; x < tilemap->GetWidth(); ++x) {
            tilemap->SetTile(x, z, 0, true);
        }
    }

    // 2. Player ("Hero Lamb" 2.5D Cutout Standee)
    auto heroTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/hero_lamb.png");
    Engine::GameObject* player = editorScene->CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    playerTransform->position = { 6.5f, 0.0f, 9.2f };
    playerTransform->scale = { 1.25f, 1.35f, 1.0f };

    auto* playerSprite = player->AddComponent<Engine::SpriteRenderer>();
    playerSprite->SetTexture(heroTex);
    playerSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
    playerSprite->SetAlphaCutoff(0.18f);

    player->AddComponent<Engine::ParticleSystem>(500);

    auto* playerCollider = player->AddComponent<Engine::BoxCollider2D>();
    playerCollider->size = { 0.55f, 0.45f };
    playerCollider->isDynamic = true;
    playerCollider->useGravity = true;

    auto* playerAudio = player->AddComponent<Engine::AudioSource>();
    playerAudio->SetClip(Engine::ResourceManager::Load<Engine::AudioClip>("jump.wav"));

    // 3. Central Cult Shrine Centerpiece
    auto shrineTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/cult_shrine.png");
    Engine::GameObject* shrine = editorScene->CreateGameObject("Cult Shrine");
    shrine->GetTransform()->position = { 6.5f, 0.0f, 5.5f };
    shrine->GetTransform()->scale = { 1.75f, 2.35f, 1.0f };
    auto* shrineSprite = shrine->AddComponent<Engine::SpriteRenderer>();
    shrineSprite->SetTexture(shrineTex);
    shrineSprite->SetAlphaCutoff(0.18f);
    auto* shrineCollider = shrine->AddComponent<Engine::BoxCollider2D>();
    shrineCollider->size = { 1.15f, 0.65f };
    shrineCollider->isDynamic = false;

    // 4. Storybook Pine Trees
    auto pineTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/pine_tree.png");
    const glm::vec3 treePositions[] = {
        {  1.2f, 0.0f,  1.2f },
        {  3.8f, 0.0f,  1.0f },
        {  6.5f, 0.0f,  0.9f },
        {  9.2f, 0.0f,  1.1f },
        { 11.8f, 0.0f,  1.3f },
        {  0.9f, 0.0f,  4.8f },
        { 12.1f, 0.0f,  4.4f },
        {  1.1f, 0.0f, 10.6f },
        { 11.4f, 0.0f,  9.8f }
    };
    int treeIdx = 1;
    for (const auto& pos : treePositions) {
        Engine::GameObject* tree = editorScene->CreateGameObject("Pine Tree " + std::to_string(treeIdx++));
        tree->GetTransform()->position = pos;
        tree->GetTransform()->scale = { 2.0f, 2.75f, 1.0f };
        auto* sprite = tree->AddComponent<Engine::SpriteRenderer>();
        sprite->SetTexture(pineTex);
        sprite->SetAlphaCutoff(0.18f);
        auto* trunkCollider = tree->AddComponent<Engine::BoxCollider2D>();
        trunkCollider->size = { 0.55f, 0.45f };
        trunkCollider->isDynamic = false;
    }

    // 5. Berry Bushes on Tilled Farm Plots & Clearing Edges
    auto bushTex = Engine::ResourceManager::Load<Engine::Texture>("assets/diorama/berry_bush.png");
    const glm::vec3 bushPositions[] = {
        {  2.2f, 0.0f, 5.6f },
        {  3.8f, 0.0f, 5.6f },
        {  2.2f, 0.0f, 7.6f },
        {  3.8f, 0.0f, 7.6f },
        {  2.2f, 0.0f, 9.3f },
        {  3.8f, 0.0f, 9.3f },
        { 10.2f, 0.0f, 7.6f },
        {  9.6f, 0.0f, 3.6f }
    };
    int bushIdx = 1;
    for (const auto& pos : bushPositions) {
        Engine::GameObject* bush = editorScene->CreateGameObject("Berry Bush " + std::to_string(bushIdx++));
        bush->GetTransform()->position = pos;
        bush->GetTransform()->scale = { 1.05f, 0.95f, 1.0f };
        auto* sprite = bush->AddComponent<Engine::SpriteRenderer>();
        sprite->SetTexture(bushTex);
        sprite->SetAlphaCutoff(0.18f);
    }

    // 6. Screen-Space UI HUD Canvas
    Engine::GameObject* hud = editorScene->CreateGameObject("HUD");
    hud->AddComponent<Engine::UIPanel>(
        glm::vec2(130.0f, 675.0f),
        glm::vec2(200.0f, 22.0f),
        glm::vec4(0.14f, 0.12f, 0.15f, 0.92f)
    );
    hud->AddComponent<Engine::UIPanel>(
        glm::vec2(105.0f, 675.0f),
        glm::vec2(150.0f, 18.0f),
        glm::vec4(0.82f, 0.14f, 0.18f, 1.0f)
    );

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
        glm::vec2(38.0f, 670.0f)
    );

    // Persist updated 2.5D diorama scene + village_splatmap.png to disk for standalone distribution & reload
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
                const auto& pos = goPtr->GetTransform()->position;
                runtimeScene->GetCamera().SetFocalPoint({ pos.x, 0.0f, pos.z });
            }
        }
    }
}

} // namespace Sandbox
