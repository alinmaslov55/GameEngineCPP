#include <Engine/Core/Application.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/Time.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/SceneSerializer.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Math/Isometric.h>
#include <Engine/Renderer/Renderer.h>
#include <Engine/Renderer/Renderer2D.h>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Renderer/Framebuffer.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/UI/UIPanel.h>
#include <Engine/UI/UILabel.h>
#include <Engine/UI/Font.h>
#include <Engine/Audio/AudioClip.h>
#include <Engine/Audio/AudioSource.h>
#include <Engine/Particles/ParticleSystem.h>
#include <Engine/ImGui/ImGuiLayer.h>
#include <Engine/Utils/Logger.h>

#include "PlayerController.h"
#include "CameraController.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <cmath>

namespace {
enum class SceneState {
    Edit = 0,
    Play = 1
};

Engine::GameObject* FindEntityByUUID(const std::shared_ptr<Engine::Scene>& scene, uint64_t uuid) {
    if (!scene || uuid == 0) return nullptr;
    for (const auto& go : scene->GetGameObjects()) {
        if (go->GetUUID() == uuid) {
            return go.get();
        }
    }
    return nullptr;
}

void BindRuntimeGameplayScripts(const std::shared_ptr<Engine::Scene>& runtimeScene) {
    if (!runtimeScene) return;
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
} // namespace

int main(int argc, char* argv[]) {
    Engine::Application app;
    Engine::ResourceManager::DiscoverAssetRoot("Sandbox");

    auto editorScene = std::make_shared<Engine::Scene>("Village");
    editorScene->SetOrthographicSize(5.0f);

    // 1. Setup initial scene
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

    // Create the Environment/Map GameObject
    Engine::GameObject* mapObject = editorScene->CreateGameObject("Map");
    auto tilemapComponent = mapObject->AddComponent<Engine::Tilemap>(10, 10);
    auto atlas = Engine::ResourceManager::Load<Engine::Texture>("test.png");
    auto grassTile = Engine::SubTexture2D::CreateFromCoords(atlas, {0, 0}, {128.0f, 128.0f});
    auto stoneTile = Engine::SubTexture2D::CreateFromCoords(atlas, {1, 0}, {128.0f, 128.0f});
    tilemapComponent->tileSet->AddTile(1, grassTile);
    tilemapComponent->tileSet->AddTile(2, stoneTile);

    for (uint32_t y = 0; y < tilemapComponent->GetHeight(); y++) {
        for (uint32_t x = 0; x < tilemapComponent->GetWidth(); x++) {
            tilemapComponent->SetTile(x, y, 1, true);
        }
    }
    // Add a couple of non-walkable Stone obstacle tiles to demonstrate Tilemap collision
    tilemapComponent->SetTile(4, 3, 2, false);
    tilemapComponent->SetTile(6, 3, 2, false);

    // Create a generic UI Canvas GameObject
    Engine::GameObject* hud = editorScene->CreateGameObject("HUD");

    // Create a Health Bar Background (Dark Gray)
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

    // Setup a basic Font (using a pre-made texture atlas with letters)
    auto fontTexture = Engine::ResourceManager::Load<Engine::Texture>("font_atlas.png");
    auto mainFont = std::make_shared<Engine::Font>();
    mainFont->atlasTexture = fontTexture;

    // Map 'H', 'P', and ':' (Assuming a 16x16 pixel grid for each letter on the atlas)
    Engine::Character charH = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {0, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charP = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {1, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    Engine::Character charColon = { Engine::SubTexture2D::CreateFromCoords(fontTexture, {2, 0}, {16, 16}), {16.0f, 16.0f}, {0.0f, 16.0f}, 16.0f };
    
    mainFont->AddCharacter('H', charH);
    mainFont->AddCharacter('P', charP);
    mainFont->AddCharacter(':', charColon);

    // Attach the Label to the HUD GameObject
    hud->AddComponent<Engine::UILabel>(
        "HP:", 
        mainFont, 
        glm::vec2(60.0f, 652.0f)
    );

    // Serialize initial scene to disk
    Engine::SceneSerializer serializer(editorScene);
    serializer.Serialize("Village.scene");

    // Active scene tracking for Edit vs Play mode
    std::shared_ptr<Engine::Scene> activeScene = editorScene;
    std::shared_ptr<Engine::Scene> runtimeScene = nullptr;
    SceneState sceneState = SceneState::Edit;

    Engine::SceneManager::LoadScene(activeScene);

    // Editor Camera for free pan & zoom in Edit mode
    Engine::EditorCamera editorCamera(1280.0f / 720.0f, 3.5f);

    // Framebuffer setup for offscreen Scene rendering into ImGui viewport
    Engine::FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    auto framebuffer = std::make_shared<Engine::Framebuffer>(fbSpec);
    glm::vec2 viewportSize = { 1280.0f, 720.0f };

    Engine::Time::Init();

    Engine::GameObject* selectedContext = nullptr;
    uint32_t s_SelectedTileID = 1; // Default to Grass
    bool s_SelectedTileWalkable = true;

    // State variables for our ImGui file popups (relative to AssetRoot)
    bool openSavePopup = false;
    bool openLoadPopup = false;
    char filepathBuffer[256] = "assets/level.scene";

    // Helper lambdas for Play / Stop transitions
    auto onScenePlay = [&]() {
        if (sceneState == SceneState::Play) return;

        uint64_t selectedUUID = selectedContext ? selectedContext->GetUUID() : 0;

        // Snapshot editorScene in memory and clone into runtimeScene
        std::string snapshot = Engine::SceneSerializer(editorScene).SerializeToString();
        runtimeScene = std::make_shared<Engine::Scene>(editorScene->GetName());
        if (!Engine::SceneSerializer(runtimeScene).DeserializeFromString(snapshot)) {
            ENGINE_ERROR("Failed to clone scene for Play Mode!");
            runtimeScene.reset();
            return;
        }

        if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            runtimeScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x), static_cast<uint32_t>(viewportSize.y));
        }

        BindRuntimeGameplayScripts(runtimeScene);

        activeScene = runtimeScene;
        Engine::SceneManager::LoadScene(activeScene);
        activeScene->OnRuntimeStart();
        selectedContext = FindEntityByUUID(activeScene, selectedUUID);
        sceneState = SceneState::Play;
        ENGINE_INFO("Entered PLAY mode");
    };

    auto onSceneStop = [&]() {
        if (sceneState == SceneState::Edit) return;

        uint64_t selectedUUID = selectedContext ? selectedContext->GetUUID() : 0;

        if (runtimeScene && runtimeScene->IsRuntimeRunning()) {
            runtimeScene->OnRuntimeStop();
        }

        activeScene = editorScene;
        if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            activeScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x), static_cast<uint32_t>(viewportSize.y));
        }
        Engine::SceneManager::LoadScene(activeScene);
        runtimeScene.reset();

        selectedContext = FindEntityByUUID(activeScene, selectedUUID);
        sceneState = SceneState::Edit;
        ENGINE_INFO("Stopped PLAY mode and restored pre-play Editor scene");
    };

    // Define the root asset directory relative to the discovered AssetRoot
    std::filesystem::path s_AssetDirectory = Engine::ResourceManager::GetAssetRoot() / "assets";
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
        if (sceneState == SceneState::Play) {
            // Run input + Box2D physics simulation, gameplay scripts, and particles
            Engine::Input::Update();
            activeScene->UpdateRuntime(Engine::Time::GetDeltaTime());
        } else {
            // Edit mode: poll input devices without stepping gameplay or physics
            Engine::Input::Update();
        }

        // 2. Resize Framebuffer and Cameras/UI projection if the ImGui viewport changed size
        if (Engine::FramebufferSpecification spec = framebuffer->GetSpecification();
            viewportSize.x > 0.0f && viewportSize.y > 0.0f && // Zero sized framebuffer is invalid
            (spec.Width != (uint32_t)viewportSize.x || spec.Height != (uint32_t)viewportSize.y))
        {
            uint32_t vpW = static_cast<uint32_t>(viewportSize.x);
            uint32_t vpH = static_cast<uint32_t>(viewportSize.y);
            framebuffer->Resize(vpW, vpH);
            editorCamera.OnViewportResize(vpW, vpH);
            activeScene->OnViewportResize(vpW, vpH);
        }

        // 3. Render Game Scene into Framebuffer
        Engine::Renderer2D::ResetStats();
        framebuffer->Bind();
        Engine::Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        Engine::Renderer::Clear(); 

        if (sceneState == SceneState::Edit) {
            Engine::SceneManager::Render(&editorCamera.GetCamera());
        } else {
            Engine::SceneManager::Render(nullptr);
        }

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
                if (ImGui::MenuItem("Save Scene As...", nullptr, false, sceneState == SceneState::Edit)) {
                    openSavePopup = true;
                }
                if (ImGui::MenuItem("Load Scene...")) {
                    openLoadPopup = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    app.Close();
                }
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
                Engine::SceneSerializer saveSerializer(editorScene);
                if (saveSerializer.Serialize(filepathBuffer)) {
                    ENGINE_INFO("Saved scene to: '{}'", filepathBuffer);
                } else {
                    ENGINE_ERROR("Failed to save scene to: '{}'", filepathBuffer);
                }
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
                if (sceneState == SceneState::Play) {
                    onSceneStop();
                }

                auto loadedScene = std::make_shared<Engine::Scene>("Loaded Level");
                Engine::SceneSerializer deserializer(loadedScene);
                if (deserializer.Deserialize(filepathBuffer)) {
                    editorScene = loadedScene;
                    activeScene = editorScene;
                    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
                        activeScene->OnViewportResize(static_cast<uint32_t>(viewportSize.x), static_cast<uint32_t>(viewportSize.y));
                    }
                    Engine::SceneManager::LoadScene(activeScene);
                    selectedContext = nullptr;
                    ENGINE_INFO("Loaded scene from: '{}'", filepathBuffer);
                } else {
                    ENGINE_ERROR("Failed to load scene from: '{}'", filepathBuffer);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // ==========================================
        // EDIT / PLAY TOOLBAR PANEL
        // ==========================================
        ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            if (sceneState == SceneState::Edit) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 1.0f));
                if (ImGui::Button("Play", ImVec2(80.0f, 0.0f))) {
                    onScenePlay();
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("Stop", ImVec2(80.0f, 0.0f))) {
                    onSceneStop();
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            if (sceneState == SceneState::Edit) {
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "[EDIT MODE]");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Camera")) {
                    editorCamera.Reset();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| MMB/RMB Drag: Pan | Scroll: Zoom (%.2fx)", editorCamera.GetZoomLevel());
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[PLAY MODE]");
                ImGui::SameLine();
                ImGui::TextDisabled("| WASD: Move | SPACE: Jump | Changes revert on Stop");
            }
        }
        ImGui::End();

        // ==========================================
        // SCENE VIEWPORT PANEL
        // ==========================================
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Scene");

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        viewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((uintptr_t)textureID), ImVec2{ viewportSize.x, viewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
        ImVec2 imageMin = ImGui::GetItemRectMin();

        const bool viewportHovered = ImGui::IsWindowHovered();
        ImGuiIO& io = ImGui::GetIO();

        // --- EDITOR CAMERA PAN & ZOOM (Edit Mode) ---
        if (sceneState == SceneState::Edit && viewportHovered) {
            if (io.MouseWheel != 0.0f) {
                editorCamera.OnMouseScroll(io.MouseWheel);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                editorCamera.Pan({ io.MouseDelta.x, io.MouseDelta.y }, viewportSize);
            }
        }

        // --- TILE PAINTING LOGIC ---
        if (viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            ImVec2 mousePos = ImGui::GetMousePos();
            glm::vec2 relativeMouse = {
                mousePos.x - imageMin.x,
                mousePos.y - imageMin.y
            };

            // Flip Y from ImGui top-left origin to OpenGL bottom-left origin
            relativeMouse.y = viewportSize.y - relativeMouse.y;

            if (relativeMouse.x >= 0.0f && relativeMouse.x <= viewportSize.x &&
                relativeMouse.y >= 0.0f && relativeMouse.y <= viewportSize.y) {
                glm::vec4 ndc = {
                    (relativeMouse.x / viewportSize.x) * 2.0f - 1.0f,
                    (relativeMouse.y / viewportSize.y) * 2.0f - 1.0f,
                    0.0f,
                    1.0f
                };

                const Engine::OrthographicCamera& viewCamera = (sceneState == SceneState::Edit)
                    ? editorCamera.GetCamera()
                    : activeScene->GetActiveCamera();
                glm::vec4 isoScreenPos = glm::inverse(viewCamera.GetViewProjectionMatrix()) * ndc;

                if (selectedContext) {
                    if (auto* tilemap = selectedContext->GetComponent<Engine::Tilemap>()) {
                        glm::vec3 mapOffset = selectedContext->GetTransform()
                            ? selectedContext->GetTransform()->position
                            : glm::vec3(0.0f);

                        glm::vec3 worldPos = Engine::Isometric::ScreenToWorld({
                            isoScreenPos.x,
                            isoScreenPos.y - mapOffset.y
                        }) - glm::vec3(mapOffset.x, 0.0f, mapOffset.z);

                        int gridX = static_cast<int>(std::round(worldPos.x));
                        int gridY = static_cast<int>(std::round(worldPos.z));

                        if (gridX >= 0 && gridX < (int)tilemap->GetWidth() && gridY >= 0 && gridY < (int)tilemap->GetHeight()) {
                            tilemap->SetTile(gridX, gridY, s_SelectedTileID, s_SelectedTileWalkable);
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
        {
            Engine::GameObject* entityToDelete = nullptr;

            for (const auto& gameObjectPtr : activeScene->GetGameObjects()) {
                Engine::GameObject* go = gameObjectPtr.get();

                std::string uuidStr = std::to_string(go->GetUUID());
                ImGui::PushID(uuidStr.c_str());

                bool isSelected = (selectedContext == go);
                if (ImGui::Selectable(go->GetName().c_str(), isSelected)) {
                    selectedContext = go;
                    ENGINE_DEBUG("Hierarchy: Selected GameObject '{}' (UUID: {})", go->GetName(), go->GetUUID());
                }

                // Right-click on an entity row to delete it
                if (ImGui::BeginPopupContextItem("EntityContextMenu")) {
                    if (ImGui::MenuItem("Delete GameObject")) {
                        entityToDelete = go;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            // Support Delete key when Hierarchy window is focused
            if (!entityToDelete && selectedContext && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                entityToDelete = selectedContext;
            }

            // Click in empty space to deselect
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
                selectedContext = nullptr;
            }

            // Right-click in empty space to create new GameObjects
            if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", 1 | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Create Empty GameObject")) {
                    auto* newGo = activeScene->CreateGameObject("New GameObject");
                    selectedContext = newGo;
                    ENGINE_INFO("Hierarchy: Created new GameObject '{}'", newGo->GetName());
                }
                ImGui::EndPopup();
            }

            // Perform deferred entity deletion safely outside iteration loop
            if (entityToDelete) {
                std::string deletedName = entityToDelete->GetName();
                if (selectedContext == entityToDelete) {
                    selectedContext = nullptr;
                }
                activeScene->DestroyGameObject(entityToDelete);
                ENGINE_INFO("Hierarchy: Deleted GameObject '{}'", deletedName);
            }
        }
        ImGui::End();

        // ==========================================
        // INSPECTOR PANEL
        // ==========================================
        ImGui::Begin("Inspector");

        if (selectedContext) {
            // 1. Editable Entity Name wired to Object::SetName
            char nameBuffer[256];
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", selectedContext->GetName().c_str());
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                if (nameBuffer[0] != '\0') {
                    selectedContext->SetName(nameBuffer);
                }
            }
            ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(selectedContext->GetUUID()));
            ImGui::Separator();

            // 2. Transform Component (cached O(1) pointer, mandatory)
            if (auto* transform = selectedContext->GetTransform()) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                    ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
                    ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
                }
            }

            // 3. Sprite Renderer Component (with Remove Component & Drag-and-Drop)
            if (auto* spriteRenderer = selectedContext->GetComponent<Engine::SpriteRenderer>()) {
                bool removeComp = false;
                bool open = ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("SpriteRendererContext")) {
                    if (ImGui::MenuItem("Remove Component")) removeComp = true;
                    ImGui::EndPopup();
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
                if (ImGui::SmallButton("X##RemoveSpriteRenderer")) removeComp = true;

                if (open && !removeComp) {
                    glm::vec4 color = spriteRenderer->GetColor();
                    if (ImGui::ColorEdit4("Color", &color.x)) spriteRenderer->SetColor(color);

                    std::string texLabel = "None";
                    auto currentTex = spriteRenderer->GetTexture();
                    if (currentTex && !currentTex->GetFilepath().empty()) {
                        texLabel = std::filesystem::path(currentTex->GetFilepath()).filename().string();
                    } else if (currentTex) {
                        texLabel = std::to_string(currentTex->GetRendererID());
                    }

                    ImGui::Button(texLabel.c_str(), ImVec2(140.0f, 0.0f));
                    if (currentTex && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Path: %s\nRenderer ID: %u (%dx%d)",
                                          Engine::ResourceManager::ToRelativePath(currentTex->GetFilepath()).c_str(),
                                          currentTex->GetRendererID(),
                                          currentTex->GetWidth(),
                                          currentTex->GetHeight());
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("<- Drop Texture Here");

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* path = static_cast<const char*>(payload->Data);
                            std::string filepath(path);

                            if (filepath.find(".png") != std::string::npos || filepath.find(".jpg") != std::string::npos) {
                                auto newTexture = Engine::ResourceManager::Load<Engine::Texture>(filepath);
                                spriteRenderer->SetTexture(newTexture);
                                ENGINE_INFO("Inspector: Assigned texture '{}' to '{}'", filepath, selectedContext->GetName());
                            } else {
                                ENGINE_WARN("Invalid file type dropped on SpriteRenderer: '{}'", filepath);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                if (removeComp) {
                    selectedContext->RemoveComponent<Engine::SpriteRenderer>();
                    ENGINE_INFO("Inspector: Removed SpriteRenderer from '{}'", selectedContext->GetName());
                }
            }

            // 4. Particle System Component
            if (auto* particleSystem = selectedContext->GetComponent<Engine::ParticleSystem>()) {
                bool removeComp = false;
                bool open = ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("ParticleSystemContext")) {
                    if (ImGui::MenuItem("Remove Component")) removeComp = true;
                    ImGui::EndPopup();
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
                if (ImGui::SmallButton("X##RemoveParticleSystem")) removeComp = true;

                if (open && !removeComp) {
                    ImGui::Text("Active Particles: %u / %zu",
                                particleSystem->GetActiveParticleCount(),
                                particleSystem->GetParticles().size());
                    if (ImGui::Button("Emit Test Particles")) {
                        Engine::ParticleProps props;
                        if (auto* t = selectedContext->GetTransform()) props.position = t->position;
                        props.velocity = { 0.0f, 1.0f, 0.0f };
                        particleSystem->Emit(props);
                        ENGINE_DEBUG("Inspector: Emitted test particle on '{}'", selectedContext->GetName());
                    }
                }

                if (removeComp) {
                    selectedContext->RemoveComponent<Engine::ParticleSystem>();
                    ENGINE_INFO("Inspector: Removed ParticleSystem from '{}'", selectedContext->GetName());
                }
            }

            // 5. Rigidbody2D Component
            if (auto* rb2d = selectedContext->GetComponent<Engine::Rigidbody2D>()) {
                bool removeComp = false;
                bool open = ImGui::CollapsingHeader("Rigidbody 2D", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("Rigidbody2DContext")) {
                    if (ImGui::MenuItem("Remove Component")) removeComp = true;
                    ImGui::EndPopup();
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
                if (ImGui::SmallButton("X##RemoveRigidbody2D")) removeComp = true;

                if (open && !removeComp) {
                    const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                    const char* currentBodyTypeString = bodyTypeStrings[(int)rb2d->Type];

                    if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                        for (int i = 0; i < 3; i++) {
                            bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                            if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                                rb2d->Type = static_cast<Engine::Rigidbody2D::BodyType>(i);
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Checkbox("Fixed Rotation", &rb2d->FixedRotation);
                }

                if (removeComp) {
                    selectedContext->RemoveComponent<Engine::Rigidbody2D>();
                    ENGINE_INFO("Inspector: Removed Rigidbody2D from '{}'", selectedContext->GetName());
                }
            }

            // 6. BoxCollider2D Component
            if (auto* boxCollider = selectedContext->GetComponent<Engine::BoxCollider2D>()) {
                bool removeComp = false;
                bool open = ImGui::CollapsingHeader("Box Collider 2D", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("BoxColliderContext")) {
                    if (ImGui::MenuItem("Remove Component")) removeComp = true;
                    ImGui::EndPopup();
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
                if (ImGui::SmallButton("X##RemoveBoxCollider")) removeComp = true;

                if (open && !removeComp) {
                    ImGui::DragFloat2("Size", &boxCollider->size.x, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat2("Offset", &boxCollider->offset.x, 0.05f);
                    ImGui::DragFloat("Density", &boxCollider->density, 0.01f, 0.0f, 100.0f);
                    ImGui::DragFloat("Friction", &boxCollider->friction, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("Restitution", &boxCollider->restitution, 0.01f, 0.0f, 1.0f);
                    ImGui::Separator();
                    ImGui::Checkbox("Is Dynamic (Isometric)", &boxCollider->isDynamic);
                    if (boxCollider->isDynamic) {
                        ImGui::Checkbox("Use Gravity", &boxCollider->useGravity);
                        ImGui::DragFloat("Gravity", &boxCollider->gravity, 0.1f, -50.0f, 0.0f);
                        ImGui::DragFloat("Ground Y", &boxCollider->groundLevel, 0.05f);
                    }
                }

                if (removeComp) {
                    selectedContext->RemoveComponent<Engine::BoxCollider2D>();
                    ENGINE_INFO("Inspector: Removed BoxCollider2D from '{}'", selectedContext->GetName());
                }
            }

            // 7. Tilemap Component
            if (auto* tilemapComp = selectedContext->GetComponent<Engine::Tilemap>()) {
                bool removeComp = false;
                bool open = ImGui::CollapsingHeader("Tilemap", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("TilemapContext")) {
                    if (ImGui::MenuItem("Remove Component")) removeComp = true;
                    ImGui::EndPopup();
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
                if (ImGui::SmallButton("X##RemoveTilemap")) removeComp = true;

                if (open && !removeComp) {
                    ImGui::Text("Dimensions: %u x %u (%u tiles)",
                                tilemapComp->GetWidth(),
                                tilemapComp->GetHeight(),
                                tilemapComp->GetWidth() * tilemapComp->GetHeight());
                    ImGui::TextDisabled("Select a brush in Tile Palette and click in Scene to paint.");
                }

                if (removeComp) {
                    selectedContext->RemoveComponent<Engine::Tilemap>();
                    ENGINE_INFO("Inspector: Removed Tilemap from '{}'", selectedContext->GetName());
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 7. Add Component Button
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
                if (ImGui::MenuItem("Rigidbody 2D")) {
                    if (!selectedContext->GetComponent<Engine::Rigidbody2D>()) {
                        selectedContext->AddComponent<Engine::Rigidbody2D>();
                        ENGINE_INFO("Inspector: Added Rigidbody2D to '{}'", selectedContext->GetName());
                    } else {
                        ENGINE_WARN("Inspector: '{}' already has a Rigidbody2D", selectedContext->GetName());
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Box Collider 2D")) {
                    if (!selectedContext->GetComponent<Engine::BoxCollider2D>()) {
                        selectedContext->AddComponent<Engine::BoxCollider2D>();
                        ENGINE_INFO("Inspector: Added BoxCollider2D to '{}'", selectedContext->GetName());
                    } else {
                        ENGINE_WARN("Inspector: '{}' already has a BoxCollider2D", selectedContext->GetName());
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

        if (ImGui::Button("Eraser (ID 0)", ImVec2(140, 30))) {
            s_SelectedTileID = 0;
            s_SelectedTileWalkable = false;
            ENGINE_DEBUG("Tile Palette: Selected Eraser (ID 0, Non-walkable)");
        }
        if (ImGui::Button("Grass (ID 1)", ImVec2(140, 30))) {
            s_SelectedTileID = 1;
            s_SelectedTileWalkable = true;
            ENGINE_DEBUG("Tile Palette: Selected Grass (ID 1, Walkable)");
        }
        if (ImGui::Button("Stone Wall (ID 2)", ImVec2(140, 30))) {
            s_SelectedTileID = 2;
            s_SelectedTileWalkable = false;
            ENGINE_DEBUG("Tile Palette: Selected Stone Wall (ID 2, Non-walkable)");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Walkable Tile", &s_SelectedTileWalkable);
        ImGui::TextDisabled("Current Brush: ID %u (%s)",
                            s_SelectedTileID,
                            s_SelectedTileWalkable ? "Walkable" : "Blocked");
        ImGui::End();

        // ==========================================
        // ASSET BROWSER PANEL
        // ==========================================
        ImGui::Begin("Asset Browser");

        if (s_CurrentDirectory != s_AssetDirectory) {
            if (ImGui::Button("<- Back")) {
                s_CurrentDirectory = s_CurrentDirectory.parent_path();
                ENGINE_DEBUG("Asset Browser: Navigated back to '{}'", s_CurrentDirectory.string());
            }
            ImGui::Separator();
        }

        if (std::filesystem::exists(s_CurrentDirectory)) {
            for (auto& directoryEntry : std::filesystem::directory_iterator(s_CurrentDirectory)) {
                const auto& path = directoryEntry.path();
                std::string filenameString = path.filename().string();

                if (directoryEntry.is_directory()) {
                    if (ImGui::Button(filenameString.c_str())) {
                        s_CurrentDirectory /= path.filename();
                        ENGINE_DEBUG("Asset Browser: Opened directory '{}'", s_CurrentDirectory.string());
                    }
                } else {
                    ImGui::Selectable(filenameString.c_str());

                    if (ImGui::BeginDragDropSource()) {
                        std::string itemPath = path.string();
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
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
