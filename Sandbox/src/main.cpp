#include <Engine/Core/Application.h>
#include <Engine/Core/Time.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Math/Isometric.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/Renderer2D.h>
#include <Engine/Renderer/Framebuffer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/UI/UIPanel.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/Font.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/ImGui/ImGuiLayer.h>
#include <Engine/Utils/Logger.h>
#include <glm/glm.hpp>
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
        ENGINE_INFO("Creating missing asset directory: '{}'", s_AssetDirectory.string());
        std::filesystem::create_directories(s_AssetDirectory);
    }
    std::filesystem::path s_CurrentDirectory = s_AssetDirectory;

    ENGINE_INFO("Entering Sandbox main loop");
    while (app.IsRunning()) {
        // 1. Process Events & Update Scene
        app.GetWindow().PollEvents();
        if (app.GetWindow().ShouldClose()) {
            app.Close();
            break;
        }

        Engine::Time::Update();
        app.Update();

        // 2. Resize Framebuffer and Scene Camera/UI projection if the ImGui viewport changed size
        if (Engine::FramebufferSpecification spec = framebuffer->GetSpecification();
            viewportSize.x > 0.0f && viewportSize.y > 0.0f && // Zero sized framebuffer is invalid
            (spec.Width != (uint32_t)viewportSize.x || spec.Height != (uint32_t)viewportSize.y))
        {
            framebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
            mainScene->OnViewportResize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }

        // 3. Render Game Scene into Framebuffer
        Engine::Renderer2D::ResetStats();
        framebuffer->Bind();
        Engine::Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        Engine::Renderer::Clear(); 
        
        // --- YOUR SCENE RENDER CODE GOES HERE ---
        Engine::SceneManager::Render();
        // ----------------------------------------
        
        framebuffer->Unbind();
        Engine::Renderer::SetViewport(
            0, 0,
            static_cast<uint32_t>(app.GetWindow().GetWidth()),
            static_cast<uint32_t>(app.GetWindow().GetHeight())
        );

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
                if (serializer.Serialize(filepathBuffer)) {
                    ENGINE_INFO("Saved scene to: '{}'", filepathBuffer);
                } else {
                    ENGINE_ERROR("Failed to save scene to: '{}'", filepathBuffer);
                }
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
                    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
                        mainScene->OnViewportResize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
                    }
                    Engine::SceneManager::LoadScene(mainScene);
                    
                    // 4. CRITICAL: Clear the inspector so it doesn't read dead memory!
                    selectedContext = nullptr; 
                    
                    ENGINE_INFO("Loaded scene from: '{}'", filepathBuffer);
                    std::cout << "Loaded scene from: " << filepathBuffer << '\n';
                } else {
                    ENGINE_ERROR("Failed to load scene from: '{}'", filepathBuffer);
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
        ImVec2 imageMin = ImGui::GetItemRectMin();

        // ==========================================
        // TILE PAINTING LOGIC
        // ==========================================
        // Only paint if the mouse is hovering over the Scene panel, not blocking other UI
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            // Get mouse position relative to the actual rendered image inside the panel
            ImVec2 mousePos = ImGui::GetMousePos();
            glm::vec2 relativeMouse = {
                mousePos.x - imageMin.x,
                mousePos.y - imageMin.y
            };

            // OpenGL Framebuffers have (0,0) at the bottom left, but ImGui puts (0,0) at the top left.
            // Invert the Y axis to match OpenGL's coordinate system.
            relativeMouse.y = viewportSize.y - relativeMouse.y;

            if (relativeMouse.x >= 0.0f && relativeMouse.x <= viewportSize.x &&
                relativeMouse.y >= 0.0f && relativeMouse.y <= viewportSize.y) {
                // Convert viewport pixel coordinates to Normalized Device Coordinates (-1.0 to +1.0)
                glm::vec4 ndc = {
                    (relativeMouse.x / viewportSize.x) * 2.0f - 1.0f,
                    (relativeMouse.y / viewportSize.y) * 2.0f - 1.0f,
                    0.0f,
                    1.0f
                };

                // Unproject NDC through the active camera's inverse ViewProjection matrix into 2D isometric space
                const Engine::OrthographicCamera& activeCamera = mainScene->GetActiveCamera();
                glm::vec4 isoScreenPos = glm::inverse(activeCamera.GetViewProjectionMatrix()) * ndc;

                // Check if the currently selected Hierarchy object has a Tilemap
                if (selectedContext) {
                    auto tilemap = selectedContext->GetComponent<Engine::Tilemap>();
                    if (tilemap) {
                        glm::vec3 mapOffset(0.0f);
                        if (auto mapTransform = selectedContext->GetComponent<Engine::Transform>()) {
                            mapOffset = mapTransform->position;
                        }

                        // Convert 2D isometric coordinates (adjusted for map elevation) back to 3D World/Grid coordinates
                        glm::vec3 worldPos = Engine::Isometric::ScreenToWorld({
                            isoScreenPos.x,
                            isoScreenPos.y - mapOffset.y
                        }) - glm::vec3(mapOffset.x, 0.0f, mapOffset.z);

                        // Round to nearest integer to get the exact Grid Cell index
                        int gridX = static_cast<int>(std::round(worldPos.x));
                        int gridY = static_cast<int>(std::round(worldPos.z));

                        // Prevent out-of-bounds array crashes
                        if (gridX >= 0 && gridX < (int)tilemap->GetWidth() && gridY >= 0 && gridY < (int)tilemap->GetHeight()) {
                            // Paint the tile!
                            tilemap->SetTile(gridX, gridY, s_SelectedTileID);
                        }
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

            std::string uuidStr = std::to_string(go->GetUUID());
            ImGui::PushID(uuidStr.c_str());

            bool isSelected = (selectedContext == go);
            if (ImGui::Selectable(go->GetName().c_str(), isSelected)) {
                selectedContext = go;
                ENGINE_DEBUG("Hierarchy: Selected GameObject '{}' (UUID: {})", go->GetName(), go->GetUUID());
            }

            ImGui::PopID();
        }

        // Click in the empty space of the window (not on an item) to deselect
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            selectedContext = nullptr;
        }

        // Optional: Right-click context menu to create new GameObjects
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", 1 | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty GameObject")) {
                auto newGo = mainScene->CreateGameObject("New GameObject");
                selectedContext = newGo; // Auto-select the newly created object
                ENGINE_INFO("Hierarchy: Created new GameObject '{}'", newGo->GetName());
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
                    
                    // Transform::rotation is stored in degrees
                    ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
                    
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
                                ENGINE_INFO("Inspector: Assigned texture '{}' to '{}'", filepath, selectedContext->GetName());
                            } else {
                                // In a real engine, trigger an ImGui error modal or log warning here
                                ENGINE_WARN("Invalid file type dropped on SpriteRenderer: '{}'", filepath);
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
                    ImGui::Text("Active Particles: %u / %zu",
                                particleSystem->GetActiveParticleCount(),
                                particleSystem->GetParticles().size());
                    if (ImGui::Button("Emit Test Particles")) {
                        Engine::ParticleProps props;
                        if (transform) props.position = transform->position;
                        props.velocity = { 0.0f, 1.0f, 0.0f };
                        particleSystem->Emit(props);
                        ENGINE_DEBUG("Inspector: Emitted test particle on '{}'", selectedContext->GetName());
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
                    if (!selectedContext->GetComponent<Engine::SpriteRenderer>()) {
                        selectedContext->AddComponent<Engine::SpriteRenderer>();
                        ENGINE_INFO("Inspector: Added SpriteRenderer to '{}'", selectedContext->GetName());
                    } else {
                        ENGINE_WARN("Inspector: '{}' already has a SpriteRenderer", selectedContext->GetName());
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Particle System")) {
                    if (!selectedContext->GetComponent<Engine::ParticleSystem>()) {
                        selectedContext->AddComponent<Engine::ParticleSystem>();
                        ENGINE_INFO("Inspector: Added ParticleSystem to '{}'", selectedContext->GetName());
                    } else {
                        ENGINE_WARN("Inspector: '{}' already has a ParticleSystem", selectedContext->GetName());
                    }
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
        if (ImGui::Button("Eraser (ID 0)", ImVec2(120, 30))) { s_SelectedTileID = 0; ENGINE_DEBUG("Tile Palette: Selected Eraser (ID 0)"); }
        if (ImGui::Button("Grass (ID 1)", ImVec2(120, 30)))  { s_SelectedTileID = 1; ENGINE_DEBUG("Tile Palette: Selected Grass (ID 1)"); }
        if (ImGui::Button("Stone (ID 2)", ImVec2(120, 30)))  { s_SelectedTileID = 2; ENGINE_DEBUG("Tile Palette: Selected Stone (ID 2)"); } // (Assuming you add a stone texture)
        
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
                ENGINE_DEBUG("Asset Browser: Navigated back to '{}'", s_CurrentDirectory.string());
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
                        ENGINE_DEBUG("Asset Browser: Opened directory '{}'", s_CurrentDirectory.string());
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

        // ==========================================
        // PROFILER PANEL
        // ==========================================
        ImGui::Begin("Profiler");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::Separator();
        auto stats = Engine::Renderer2D::GetStats();
        ImGui::Text("Renderer2D Stats:");
        ImGui::Text("  Draw Calls: %u", stats.DrawCalls);
        ImGui::Text("  Quads:      %u", stats.QuadCount);
        ImGui::Text("  Vertices:   %u", stats.GetTotalVertexCount());
        ImGui::Text("  Indices:    %u", stats.GetTotalIndexCount());
        ImGui::End();

        // ==========================================
        // CONSOLE PANEL (Ring-Buffer Log Sink)
        // ==========================================
        ImGui::Begin("Console");
        {
            static ImGuiTextFilter logFilter;
            static bool autoScroll = true;

            if (ImGui::Button("Clear")) {
                Engine::Logger::ClearLogs();
            }
            ImGui::SameLine();

            const char* levelNames[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
            int currentLevel = static_cast<int>(Engine::Logger::GetLevel());
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("Level", &currentLevel, levelNames, IM_ARRAYSIZE(levelNames))) {
                Engine::Logger::SetLevel(static_cast<spdlog::level::level_enum>(currentLevel));
            }
            ImGui::SameLine();

            logFilter.Draw("Filter", 180.0f);
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &autoScroll);
            ImGui::Separator();

            ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            auto logs = Engine::Logger::GetRecentLogs();
            for (const auto& entry : logs) {
                if (!logFilter.PassFilter(entry.Message.c_str())) {
                    continue;
                }

                ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                switch (entry.Level) {
                    case spdlog::level::trace:    color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break;
                    case spdlog::level::debug:    color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f); break;
                    case spdlog::level::info:     color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); break;
                    case spdlog::level::warn:     color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); break;
                    case spdlog::level::err:      color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f); break;
                    case spdlog::level::critical: color = ImVec4(1.0f, 0.1f, 0.5f, 1.0f); break;
                    default: break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry.Message.c_str());
                ImGui::PopStyleColor();
            }

            if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        ImGui::End();

        Engine::ImGuiLayer::EndDockspace();
        Engine::ImGuiLayer::End();

        app.GetWindow().SwapBuffers();
    }

    return 0;
}
