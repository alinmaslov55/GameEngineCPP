#include <Engine/Core/Application.h>
#include <Engine/Core/Time.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Math/Isometric.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/Framebuffer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/UI/UIPanel.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/Font.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/ImGui/ImGuiLayer.h>
#include <imgui.h>
#include <filesystem>
#include <iostream>
#include <cmath>

int main(int argc, char* argv[]) {
    Engine::Application app;
    auto mainScene = std::make_shared<Engine::Scene>("Village");

    // 1. Hardcode a scene once
    Engine::GameObject* player = mainScene->CreateGameObject("Player");
    auto transform = player->GetComponent<Engine::Transform>();
    transform->position = glm::vec3(5.0f, 0.0f, 2.0f);
    player->AddComponent<Engine::SpriteRenderer>();
    player->AddComponent<Engine::ParticleSystem>(500); // Pool of 500 particles

    // Create the Environment/Map GameObject
    Engine::GameObject* mapObject = mainScene->CreateGameObject("Map");
    auto tilemapComponent = mapObject->AddComponent<Engine::Tilemap>(10, 10);
    auto atlas = Engine::ResourceManager::Load<Engine::Texture>("C:/DEV/GameEngineCPP/Sandbox/test.png");
    auto grassTile = Engine::SubTexture2D::CreateFromCoords(atlas, {0, 0}, {128.0f, 128.0f});
    auto stoneTile = Engine::SubTexture2D::CreateFromCoords(atlas, {1, 0}, {128.0f, 128.0f});
    tilemapComponent->tileSet->AddTile(1, grassTile);
    tilemapComponent->tileSet->AddTile(2, stoneTile);

    for (uint32_t y = 0; y < tilemapComponent->GetHeight(); y++) {
        for (uint32_t x = 0; x < tilemapComponent->GetWidth(); x++) {
            tilemapComponent->SetTile(x, y, 1);
        }
    }

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

    // Framebuffer setup for offscreen Scene rendering into ImGui viewport
    Engine::FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    auto framebuffer = std::make_shared<Engine::Framebuffer>(fbSpec);
    glm::vec2 viewportSize = { 1280.0f, 720.0f };

    Engine::Time::Init();

    Engine::GameObject* selectedContext = nullptr;
    uint32_t s_SelectedTileID = 1; // Default to Grass

    // State variables for our ImGui file popups
    bool openSavePopup = false;
    bool openLoadPopup = false;
    char filepathBuffer[256] = "C:/DEV/GameEngineCPP/Sandbox/assets/level.scene";

    // Define the root asset directory (Create an 'assets' folder next to your executable or in your project)
    std::filesystem::path s_AssetDirectory = "C:/DEV/GameEngineCPP/Sandbox/assets";
    if (!std::filesystem::exists(s_AssetDirectory)) {
        std::filesystem::create_directories(s_AssetDirectory);
    }
    std::filesystem::path s_CurrentDirectory = s_AssetDirectory;

    while (app.IsRunning()) {
        // 1. Process Events & Update Scene
        app.GetWindow().PollEvents();
        if (app.GetWindow().ShouldClose()) {
            app.Close();
            break;
        }

        Engine::Time::Update();
        app.Update();

        // 2. Resize Framebuffer if the ImGui window changed size
        if (Engine::FramebufferSpecification spec = framebuffer->GetSpecification();
            viewportSize.x > 0.0f && viewportSize.y > 0.0f && // Zero sized framebuffer is invalid
            (spec.Width != (uint32_t)viewportSize.x || spec.Height != (uint32_t)viewportSize.y))
        {
            framebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
            // Optional: Update your orthographic camera bounds here based on new viewportSize
        }

        // 3. Render Game Scene into Framebuffer
        framebuffer->Bind();
        Engine::Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        Engine::Renderer::Clear(); 
        
        // --- YOUR SCENE RENDER CODE GOES HERE ---
        Engine::SceneManager::Render();
        // ----------------------------------------
        
        framebuffer->Unbind();

        // 4. Render UI / Editor
        Engine::ImGuiLayer::Begin();
        Engine::ImGuiLayer::BeginDockspace();

        // ==========================================
        // MENU BAR
        // ==========================================
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Scene As...")) { openSavePopup = true; }
                if (ImGui::MenuItem("Load Scene..."))    { openLoadPopup = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))             { app.Close(); }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // ==========================================
        // SAVE MODAL
        // ==========================================
        if (openSavePopup) { ImGui::OpenPopup("Save Scene"); openSavePopup = false; }
        
        if (ImGui::BeginPopupModal("Save Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter filepath:");
            ImGui::InputText("##savepath", filepathBuffer, sizeof(filepathBuffer));
            
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                Engine::SceneSerializer serializer(mainScene);
                serializer.Serialize(filepathBuffer);
                std::cout << "Saved scene to: " << filepathBuffer << '\n';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // ==========================================
        // LOAD MODAL
        // ==========================================
        if (openLoadPopup) { ImGui::OpenPopup("Load Scene"); openLoadPopup = false; }
        
        if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter filepath:");
            ImGui::InputText("##loadpath", filepathBuffer, sizeof(filepathBuffer));
            
            if (ImGui::Button("Load", ImVec2(120, 0))) {
                // 1. Create a brand new empty scene
                auto loadedScene = std::make_shared<Engine::Scene>("Loaded Level");
                
                // 2. Attempt to deserialize the JSON file into it
                Engine::SceneSerializer deserializer(loadedScene);
                if (deserializer.Deserialize(filepathBuffer)) {
                    // 3. Swap the active scene in the engine
                    mainScene = loadedScene;
                    Engine::SceneManager::LoadScene(mainScene);
                    
                    // 4. CRITICAL: Clear the inspector so it doesn't read dead memory!
                    selectedContext = nullptr; 
                    
                    std::cout << "Loaded scene from: " << filepathBuffer << '\n';
                } else {
                    std::cerr << "Failed to load scene!\n";
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // --- Scene Viewport Panel ---
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 }); // Remove borders inside panel
        ImGui::Begin("Scene");
        
        // Read the available panel space
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        viewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        // Draw the Framebuffer texture using ImGui::Image
        // Cast the renderer ID to void* (required by ImGui API)
        // OpenGL textures are flipped vertically by default, so we invert the UV coordinates (0, 1) to (1, 0)
        uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((uintptr_t)textureID), ImVec2{ viewportSize.x, viewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        // ==========================================
        // TILE PAINTING LOGIC
        // ==========================================
        // Only paint if the mouse is hovering over the Scene panel, not blocking other UI
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Get mouse position relative to the actual rendered image inside the panel
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 viewportBoundsMin = ImGui::GetWindowContentRegionMin();
            
            glm::vec2 relativeMouse = { 
                mousePos.x - (windowPos.x + viewportBoundsMin.x), 
                mousePos.y - (windowPos.y + viewportBoundsMin.y) 
            };

            // OpenGL Framebuffers have (0,0) at the bottom left, but ImGui puts (0,0) at the top left.
            // We must invert the Y axis to match the camera's coordinate system.
            relativeMouse.y = viewportSize.y - relativeMouse.y; 

            // NOTE: If you implemented a movable Camera, you must ADD the camera's screen-space offset to relativeMouse here.

            // Convert to World Space
            glm::vec3 worldPos = Engine::Isometric::ScreenToWorld(relativeMouse);
            
            // Round to nearest integer to get the exact Grid Cell index
            int gridX = static_cast<int>(std::round(worldPos.x));
            int gridY = static_cast<int>(std::round(worldPos.z));

            // Check if the currently selected Hierarchy object has a Tilemap
            if (selectedContext) {
                auto tilemap = selectedContext->GetComponent<Engine::Tilemap>();
                if (tilemap) {
                    // Prevent out-of-bounds array crashes
                    if (gridX >= 0 && gridX < (int)tilemap->GetWidth() && gridY >= 0 && gridY < (int)tilemap->GetHeight()) {
                        // Paint the tile!
                        tilemap->SetTile(gridX, gridY, s_SelectedTileID);
                    }
                }
            }
        }
        
        ImGui::End();
        ImGui::PopStyleVar();

        // ==========================================
        // HIERARCHY PANEL
        // ==========================================
        ImGui::Begin("Hierarchy");

        // Iterate through every GameObject in the active scene
        for (const auto& gameObjectPtr : mainScene->GetGameObjects()) {
            Engine::GameObject* go = gameObjectPtr.get();
            
            // ImGui::Selectable requires a unique string ID. 
            // If two objects have the same name, ImGui might get confused. 
            // In a production engine, you would append a UUID to the string, e.g., "Player##12345"
            std::string label = go->GetName();
            
            // The second parameter determines if this item is currently highlighted
            bool isSelected = (selectedContext == go);
            
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedContext = go;
            }
        }

        // Optional: Click in the empty space of the window to deselect
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            selectedContext = nullptr;
        }

        // Optional: Right-click context menu to create new GameObjects
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", 1 | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty GameObject")) {
                auto newGo = mainScene->CreateGameObject("New GameObject");
                selectedContext = newGo; // Auto-select the newly created object
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        // ==========================================
        // INSPECTOR PANEL
        // ==========================================
        ImGui::Begin("Inspector");

        if (selectedContext) {
            // 1. Entity Name
            // In a full implementation, you would use ImGui::InputText here to allow renaming.
            ImGui::TextDisabled("Name:");
            ImGui::SameLine();
            ImGui::Text("%s", selectedContext->GetName().c_str());
            ImGui::Separator();

            // 2. Transform Component
            // Every GameObject has a Transform by default in this engine
            auto transform = selectedContext->GetComponent<Engine::Transform>();
            if (transform) {
                // ImGuiTreeNodeFlags_DefaultOpen ensures the menu is expanded by default
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    // DragFloat3 creates an X,Y,Z slider. The 0.1f is the speed/sensitivity.
                    ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                    
                    // Rotation is often stored in radians internally, but users expect degrees in the UI
                    glm::vec3 rotationDegrees = glm::degrees(transform->rotation);
                    if (ImGui::DragFloat3("Rotation", &rotationDegrees.x, 1.0f)) {
                        // Convert back to radians if the user dragged the slider
                        transform->rotation = glm::radians(rotationDegrees);
                    }
                    
                    ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
                }
            }

            // 3. Sprite Renderer Component (Updated for Drag & Drop)
            auto spriteRenderer = selectedContext->GetComponent<Engine::SpriteRenderer>();
            if (spriteRenderer) {
                if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                    glm::vec4 color = spriteRenderer->GetColor();
                    if (ImGui::ColorEdit4("Color", &color.x)) spriteRenderer->SetColor(color);
                    
                    // Display the current texture or a placeholder
                    std::string texLabel = "None";
                    if (auto tex = spriteRenderer->GetTexture()) {
                        texLabel = std::to_string(tex->GetRendererID()); // Or store the filepath in the Texture class
                    }

                    // Create a visual drop zone
                    ImGui::Button(texLabel.c_str(), ImVec2(100.0f, 0.0f));
                    ImGui::SameLine();
                    ImGui::TextDisabled("<- Drop Texture Here");

                    // --- DRAG AND DROP TARGET ---
                    if (ImGui::BeginDragDropTarget()) {
                        // Check if the incoming payload matches our specific identifier
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            
                            // Extract the string from the payload
                            const char* path = (const char*)payload->Data;
                            std::string filepath(path);

                            // Simple validation to ensure it's an image file
                            if (filepath.find(".png") != std::string::npos || filepath.find(".jpg") != std::string::npos) {
                                // Load it via your centralized cache!
                                auto newTexture = Engine::ResourceManager::Load<Engine::Texture>(filepath);
                                spriteRenderer->SetTexture(newTexture);
                            } else {
                                // In a real engine, trigger an ImGui error modal or log warning here
                                std::cout << "Invalid file type dropped on SpriteRenderer!\n";
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }

            // 4. Particle System Component
            auto particleSystem = selectedContext->GetComponent<Engine::ParticleSystem>();
            if (particleSystem) {
                if (ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Active Particles: %zu", particleSystem->GetParticles().size());
                    if (ImGui::Button("Emit Test Particles")) {
                        Engine::ParticleProps props;
                        if (transform) props.position = transform->position;
                        props.velocity = { 0.0f, 1.0f, 0.0f };
                        particleSystem->Emit(props);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 5. Add Component Button
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    if (!selectedContext->GetComponent<Engine::SpriteRenderer>())
                        selectedContext->AddComponent<Engine::SpriteRenderer>();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Particle System")) {
                    if (!selectedContext->GetComponent<Engine::ParticleSystem>())
                        selectedContext->AddComponent<Engine::ParticleSystem>();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

        } else {
            ImGui::TextDisabled("Select an entity to view its properties.");
        }

        ImGui::End();

        // ==========================================
        // TILE PALETTE PANEL
        // ==========================================
        ImGui::Begin("Tile Palette");
        ImGui::Text("Select a brush:");
        ImGui::Separator();

        // In a full implementation, you'd loop over tilemap->tileSet and draw ImGui::ImageButtons using their textures.
        // For now, we map them directly to standard UI buttons.
        if (ImGui::Button("Eraser (ID 0)", ImVec2(120, 30))) s_SelectedTileID = 0;
        if (ImGui::Button("Grass (ID 1)", ImVec2(120, 30)))  s_SelectedTileID = 1;
        if (ImGui::Button("Stone (ID 2)", ImVec2(120, 30)))  s_SelectedTileID = 2; // (Assuming you add a stone texture)
        
        ImGui::Spacing();
        ImGui::TextDisabled("Current Tool: ID %u", s_SelectedTileID);
        ImGui::End();

        // ==========================================
        // ASSET BROWSER PANEL
        // ==========================================
        ImGui::Begin("Asset Browser");

        // Back Button
        if (s_CurrentDirectory != s_AssetDirectory) {
            if (ImGui::Button("<- Back")) {
                s_CurrentDirectory = s_CurrentDirectory.parent_path();
            }
            ImGui::Separator();
        }

        // Iterate over the current directory
        if (std::filesystem::exists(s_CurrentDirectory)) {
            for (auto& directoryEntry : std::filesystem::directory_iterator(s_CurrentDirectory)) {
                const auto& path = directoryEntry.path();
                std::string filenameString = path.filename().string();

                if (directoryEntry.is_directory()) {
                    // Render folders as clickable buttons to navigate deeper
                    if (ImGui::Button(filenameString.c_str())) {
                        s_CurrentDirectory /= path.filename();
                    }
                } else {
                    // Render files as selectable items
                    ImGui::Selectable(filenameString.c_str());

                    // --- DRAG AND DROP SOURCE ---
                    if (ImGui::BeginDragDropSource()) {
                        std::string itemPath = path.string();
                        
                        // Package the filepath string into the ImGui payload
                        // +1 ensures we include the null-terminator in the payload size
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                        
                        // Render a tooltip next to the mouse cursor while dragging
                        ImGui::Text("Drag %s", filenameString.c_str());
                        
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }

        ImGui::End();

        // Panel 4: Performance Profiler
        ImGui::Begin("Profiler");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();

        Engine::ImGuiLayer::EndDockspace();
        Engine::ImGuiLayer::End();

        app.GetWindow().SwapBuffers();
    }

    return 0;
}
