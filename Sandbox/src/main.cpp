#include <Engine/Core/Application.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/UI/UIPanel.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/Font.h>
#include <Engine/Particles/ParticleSystem.h>
#include <imgui.h>

int main(int argc, char* argv[]) {
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Village");

    // 1. Hardcode a scene once
    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    auto transform = player->GetComponent<Engine::Transform>();
    transform->position = glm::vec3(5.0f, 0.0f, 2.0f);
    player->AddComponent<Engine::SpriteRenderer>();
    player->AddComponent<Engine::ParticleSystem>(500); // Pool of 500 particles

    // Create a generic UI Canvas GameObject
    Engine::GameObject* hud = mainScene->CreateGameObject("HUD");

    // Create a Health Bar Background (Dark Gray)
    // Anchored at the top left of a 1280x720 screen
    auto hpBg = hud->AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 650.0f),  // Position (X, Y)
        glm::vec2(200.0f, 20.0f),  // Size (Width, Height)
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f) // Color
    );

    // Create a Health Bar Foreground (Red)
    auto hpFill = hud->AddComponent<Engine::UIPanel>(
        glm::vec2(50.0f, 650.0f), 
        glm::vec2(150.0f, 20.0f),  // Slightly less width to show damage
        glm::vec4(0.8f, 0.1f, 0.1f, 1.0f)
    );

    // 1. Setup a basic Font (using a pre-made texture atlas with letters)
    auto fontTexture = Engine::ResourceManager::Load<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/font_atlas.png");
    auto mainFont = std::make_shared<Engine::Font>();
    mainFont->atlasTexture = fontTexture;

    // 2. Map 'H', 'P', and ':' (Assuming a 16x16 pixel grid for each letter on the atlas)
    // In a real engine, stb_truetype calculates these UVs and sizes for you.
    Engine::Character charH = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {0, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charP = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {1, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charColon = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {2, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    
    mainFont->AddCharacter('H', charH);
    mainFont->AddCharacter('P', charP);
    mainFont->AddCharacter(':', charColon);

    // 3. Attach the Label to the HUD GameObject
    hud->AddComponent<Engine::UILabel>(
        "HP:", 
        mainFont, 
        glm::vec2(60.0f, 652.0f) // Positioned slightly inside the health bar panel
    );

    // 2. Serialize it to disk
    Engine::SceneSerializer serializer(mainScene);
    serializer.Serialize("C:/DEV/GameEngineCPP/Sandbox/Village.scene");

    // Load into the game loop
    Engine::SceneManager::LoadScene(mainScene);

    // Editor UI panels registered to render within the dockspace
    app.SetImGuiRenderCallback([&]() {
        // Menu Bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) { app.Close(); }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Panel 1: Hierarchy (List of GameObjects)
        ImGui::Begin("Hierarchy");
        ImGui::Text("Player");
        ImGui::Text("Tree 1");
        ImGui::Text("Map");
        ImGui::End();

        // Panel 2: Inspector (Properties of selected object)
        ImGui::Begin("Inspector");
        float pos[3] = { 5.0f, 0.0f, 2.0f };
        ImGui::DragFloat3("Position", pos, 0.1f);
        if (auto sprite = player->GetComponent<Engine::SpriteRenderer>()) {
            ImGui::ColorEdit4("Sprite Color", (float*)&sprite->GetColor());
        }
        ImGui::End();

        // Panel 3: Performance Profiler
        ImGui::Begin("Profiler");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    });

    app.Run();

    return 0;
}