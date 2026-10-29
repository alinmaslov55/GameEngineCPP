#include "Panels/SceneViewportPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/TerrainRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/FoliageWindComponents.h>
#include <Engine/Scene/TerrainComponent.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Sandbox {

namespace {

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string PrettifyStemName(const std::string& stem) {
    std::string out = stem;
    bool capitalizeNext = true;
    for (char& ch : out) {
        if (ch == '_' || ch == '-') {
            ch = ' ';
            capitalizeNext = true;
        } else if (capitalizeNext) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            capitalizeNext = false;
        }
    }
    return out.empty() ? "Standee Prop" : out;
}

DioramaDropArchetype ResolveDropArchetype(DioramaDropArchetype requested, const std::string& lowerStem) {
    if (requested != DioramaDropArchetype::AutoDetect) {
        return requested;
    }
    if (lowerStem.find("grass") != std::string::npos || lowerStem.find("bush") != std::string::npos ||
        lowerStem.find("fern") != std::string::npos || lowerStem.find("shrub") != std::string::npos ||
        lowerStem.find("crop") != std::string::npos || lowerStem.find("flower") != std::string::npos) {
        return DioramaDropArchetype::SwayingFoliage;
    }
    if (lowerStem.find("tree") != std::string::npos || lowerStem.find("pine") != std::string::npos ||
        lowerStem.find("oak") != std::string::npos) {
        return DioramaDropArchetype::PineOrTree;
    }
    if (lowerStem.find("banner") != std::string::npos || lowerStem.find("flag") != std::string::npos ||
        lowerStem.find("standard") != std::string::npos || lowerStem.find("pennant") != std::string::npos) {
        return DioramaDropArchetype::RitualBanner;
    }
    if (lowerStem.find("decal") != std::string::npos || lowerStem.find("rug") != std::string::npos ||
        lowerStem.find("carpet") != std::string::npos || lowerStem.find("floor") != std::string::npos) {
        return DioramaDropArchetype::FlatDecal;
    }
    return DioramaDropArchetype::SolidStandee;
}

} // namespace

void SceneViewportPanel::OnImGuiRender(const std::shared_ptr<Engine::Framebuffer>& framebuffer,
                                       const std::shared_ptr<Engine::Scene>& activeScene,
                                       Engine::EditorCamera& editorCamera,
                                       bool isEditMode,
                                       Engine::GameObject*& selectedEntity,
                                       TilePalettePanel& palettePanel,
                                       const ContentBrowserPanel& contentBrowser) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Scene");

    const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    if (framebuffer) {
        const uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(textureID)),
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
                     ImVec2{ 0.0f, 1.0f },
                     ImVec2{ 1.0f, 0.0f });
    }
    const ImVec2 imageMin = ImGui::GetItemRectMin();

    // 0. Drag-and-Drop 2.5D Prop Spawning from Asset Browser directly onto the 3D Ground Plane
    if (isEditMode && activeScene && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const std::filesystem::path droppedPath(static_cast<const char*>(payload->Data));
            const std::string ext = ToLowerCopy(droppedPath.extension().string());

            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                const Engine::PerspectiveCamera& viewCamera = editorCamera.GetCamera();
                float groundY = 0.0f;
                if (Engine::GameObject* terrainObj = TilePalettePanel::ResolveTargetTerrainEntity(activeScene, selectedEntity)) {
                    if (auto* tr = terrainObj->GetTransform()) {
                        groundY = tr->position.y;
                    }
                }

                glm::vec3 spawnPos = viewCamera.GetFocalPoint();
                spawnPos.y = groundY;

                if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    const glm::vec2 relMouse = {
                        mousePos.x - imageMin.x,
                        m_ViewportSize.y - (mousePos.y - imageMin.y)
                    };
                    const glm::vec2 ndc = {
                        (relMouse.x / m_ViewportSize.x) * 2.0f - 1.0f,
                        (relMouse.y / m_ViewportSize.y) * 2.0f - 1.0f
                    };
                    glm::vec3 hitPos{ 0.0f };
                    if (viewCamera.RaycastGroundPlane(ndc, groundY, hitPos)) {
                        spawnPos = hitPos;
                    }
                }

                auto tex = Engine::ResourceManager::Load<Engine::Texture>(droppedPath.string());
                const float aspect = (tex && tex->GetWidth() > 0 && tex->GetHeight() > 0)
                    ? (static_cast<float>(tex->GetWidth()) / static_cast<float>(tex->GetHeight()))
                    : 1.0f;

                const std::string stemLower = ToLowerCopy(droppedPath.stem().string());
                const DioramaDropArchetype archetype = ResolveDropArchetype(contentBrowser.GetDropArchetype(), stemLower);

                const std::string baseName = PrettifyStemName(droppedPath.stem().string());
                uint32_t duplicateCount = 0;
                for (const auto& existing : activeScene->GetGameObjects()) {
                    if (existing->GetName().rfind(baseName, 0) == 0) {
                        ++duplicateCount;
                    }
                }
                const std::string entityName = (duplicateCount == 0)
                    ? baseName
                    : (baseName + " " + std::to_string(duplicateCount + 1));

                Engine::GameObject* spawned = activeScene->CreateGameObject(entityName);
                auto* transform = spawned->GetTransform();
                transform->position = spawnPos;

                auto* sprite = spawned->AddComponent<Engine::SpriteRenderer>();
                sprite->SetTexture(tex);
                sprite->SetAlphaCutoff(0.18f);
                sprite->SetReceiveLighting(true);

                const float phaseSeed = std::fmod(std::abs(spawnPos.x * 1.73f + spawnPos.z * 2.41f), 6.28318f);
                sprite->SetWindPhaseOffset(phaseSeed);

                switch (archetype) {
                    case DioramaDropArchetype::SwayingFoliage: {
                        const float h = (stemLower.find("grass") != std::string::npos) ? 0.72f : 0.96f;
                        transform->scale = { h * aspect, h, 1.0f };
                        sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
                        sprite->SetCastShadows(true);
                        sprite->SetWindSwayWeight(0.82f);
                        break;
                    }
                    case DioramaDropArchetype::PineOrTree: {
                        const float h = 2.70f;
                        transform->scale = { h * aspect, h, 1.0f };
                        sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
                        sprite->SetCastShadows(true);
                        sprite->SetWindSwayWeight(0.38f);
                        auto* col = spawned->AddComponent<Engine::BoxCollider2D>();
                        col->size = { 0.55f, 0.45f };
                        col->isDynamic = false;
                        break;
                    }
                    case DioramaDropArchetype::RitualBanner: {
                        const float h = 2.15f;
                        transform->scale = { h * aspect, h, 1.0f };
                        sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
                        sprite->SetCastShadows(true);
                        sprite->SetWindSwayWeight(0.72f);
                        auto* col = spawned->AddComponent<Engine::BoxCollider2D>();
                        col->size = { 0.45f, 0.40f };
                        col->isDynamic = false;
                        spawned->AddComponent<Engine::SquashStretchComponent>();
                        break;
                    }
                    case DioramaDropArchetype::FlatDecal: {
                        const float w = 1.60f;
                        transform->scale = { w, 1.0f, w / std::max(aspect, 0.1f) };
                        sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::FlatGround);
                        sprite->SetCastShadows(false);
                        sprite->SetWindSwayWeight(0.0f);
                        break;
                    }
                    case DioramaDropArchetype::SolidStandee:
                    case DioramaDropArchetype::AutoDetect:
                    default: {
                        const float h = 1.45f;
                        transform->scale = { h * aspect, h, 1.0f };
                        sprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
                        sprite->SetCastShadows(true);
                        sprite->SetWindSwayWeight(0.0f);
                        auto* col = spawned->AddComponent<Engine::BoxCollider2D>();
                        col->size = { std::clamp(0.45f * transform->scale.x, 0.35f, 1.40f), 0.48f };
                        col->isDynamic = false;
                        break;
                    }
                }

                selectedEntity = spawned;
                const char* archName = (archetype == DioramaDropArchetype::SwayingFoliage) ? "SwayingFoliage" :
                                       (archetype == DioramaDropArchetype::PineOrTree) ? "PineOrTree" :
                                       (archetype == DioramaDropArchetype::RitualBanner) ? "RitualBanner" :
                                       (archetype == DioramaDropArchetype::FlatDecal) ? "FlatDecal" : "SolidStandee";
                ENGINE_INFO("Scene Viewport: Spawned 2.5D standee '{}' [{}] at ({:.2f}, {:.2f}, {:.2f}), scale=({:.2f}, {:.2f}, {:.2f}), aspect={:.2f}",
                            entityName, archName, spawnPos.x, spawnPos.y, spawnPos.z,
                            transform->scale.x, transform->scale.y, transform->scale.z, aspect);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool viewportHovered = ImGui::IsWindowHovered();
    ImGuiIO& io = ImGui::GetIO();

    // 1. 3D Diorama Editor Camera Pan, Zoom, Orbit & Brush Radius Shortcuts (Edit Mode)
    if (isEditMode && viewportHovered) {
        if (io.MouseWheel != 0.0f) {
            editorCamera.OnMouseScroll(io.MouseWheel);
        }

        if (io.KeyAlt && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                          ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            editorCamera.Orbit(-io.MouseDelta.x * 0.35f, io.MouseDelta.y * 0.25f);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
            editorCamera.Pan({ io.MouseDelta.x, io.MouseDelta.y }, m_ViewportSize);
        }

        const float orbitSpeed = 65.0f * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            editorCamera.Orbit(-orbitSpeed, 0.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            editorCamera.Orbit(orbitSpeed, 0.0f);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
            palettePanel.SetBrushRadius(palettePanel.GetBrushRadius() - 0.20f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
            palettePanel.SetBrushRadius(palettePanel.GetBrushRadius() + 0.20f);
        }
    }

    // 2. 3D Ground Raycasting for Splatmap Brush Preview + Painting & Tile Grid Painting
    bool showSplatRing = false;
    if (activeScene && viewportHovered && !io.KeyAlt &&
        m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 relativeMouse = {
            mousePos.x - imageMin.x,
            mousePos.y - imageMin.y
        };
        relativeMouse.y = m_ViewportSize.y - relativeMouse.y;

        if (relativeMouse.x >= 0.0f && relativeMouse.x <= m_ViewportSize.x &&
            relativeMouse.y >= 0.0f && relativeMouse.y <= m_ViewportSize.y) {
            const glm::vec2 ndc = {
                (relativeMouse.x / m_ViewportSize.x) * 2.0f - 1.0f,
                (relativeMouse.y / m_ViewportSize.y) * 2.0f - 1.0f
            };

            const Engine::PerspectiveCamera& viewCamera = isEditMode
                ? editorCamera.GetCamera()
                : activeScene->GetActiveCamera();

            if (isEditMode && palettePanel.GetPaintMode() == GroundPaintMode::TerrainSplat) {
                if (Engine::GameObject* terrainEntity = TilePalettePanel::ResolveTargetTerrainEntity(activeScene, selectedEntity)) {
                    auto* terrain = terrainEntity->GetComponent<Engine::TerrainComponent>();
                    auto* trTransform = terrainEntity->GetTransform();
                    const glm::vec3 origin = trTransform ? trTransform->position : glm::vec3(0.0f);
                    const glm::vec3 scale  = trTransform ? trTransform->scale    : glm::vec3(1.0f);

                    glm::vec3 hitWorldPos{ 0.0f };
                    if (terrain && viewCamera.RaycastGroundPlane(ndc, origin.y, hitWorldPos)) {
                        const glm::vec2 hitXZ = { hitWorldPos.x, hitWorldPos.z };
                        Engine::TerrainRenderer::SetBrushPreview(
                            true,
                            hitXZ,
                            palettePanel.GetBrushRadius(),
                            palettePanel.GetBrushFalloff()
                        );
                        showSplatRing = true;

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            terrain->PaintWorldBrush(
                                hitXZ.x,
                                hitXZ.y,
                                origin,
                                scale,
                                palettePanel.GetSelectedSplatLayer(),
                                palettePanel.GetBrushRadius(),
                                palettePanel.GetBrushFalloff(),
                                palettePanel.GetBrushStrength()
                            );
                            m_HasActiveSplatStroke = true;
                            m_LastSplatStrokeXZ = hitXZ;
                        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_HasActiveSplatStroke) {
                            const float stepThreshold = std::max(0.08f, palettePanel.GetBrushRadius() * 0.18f);
                            if (glm::length(hitXZ - m_LastSplatStrokeXZ) >= stepThreshold) {
                                terrain->PaintWorldSegment(
                                    m_LastSplatStrokeXZ,
                                    hitXZ,
                                    origin,
                                    scale,
                                    palettePanel.GetSelectedSplatLayer(),
                                    palettePanel.GetBrushRadius(),
                                    palettePanel.GetBrushFalloff(),
                                    palettePanel.GetBrushStrength() * 0.80f
                                );
                                m_LastSplatStrokeXZ = hitXZ;
                            }
                        }
                    }
                }
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && selectedEntity) {
                if (auto* tilemap = selectedEntity->GetComponent<Engine::Tilemap>()) {
                    const glm::vec3 mapOrigin = selectedEntity->GetTransform()
                        ? selectedEntity->GetTransform()->position
                        : glm::vec3(0.0f);
                    const glm::vec3 mapScale = selectedEntity->GetTransform()
                        ? selectedEntity->GetTransform()->scale
                        : glm::vec3(1.0f);

                    glm::vec3 hitWorldPos{ 0.0f };
                    if (viewCamera.RaycastGroundPlane(ndc, mapOrigin.y, hitWorldPos)) {
                        const float localX = (hitWorldPos.x - mapOrigin.x) / (mapScale.x != 0.0f ? mapScale.x : 1.0f);
                        const float localZ = (hitWorldPos.z - mapOrigin.z) / (mapScale.z != 0.0f ? mapScale.z : 1.0f);

                        const int gridX = static_cast<int>(std::round(localX));
                        const int gridZ = static_cast<int>(std::round(localZ));

                        if (gridX >= 0 && gridX < static_cast<int>(tilemap->GetWidth()) &&
                            gridZ >= 0 && gridZ < static_cast<int>(tilemap->GetHeight())) {
                            tilemap->SetTile(static_cast<uint32_t>(gridX),
                                             static_cast<uint32_t>(gridZ),
                                             palettePanel.GetSelectedTileID(),
                                             palettePanel.IsSelectedTileWalkable());
                        }
                    }
                }
            }
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_HasActiveSplatStroke = false;
    }
    if (!showSplatRing) {
        Engine::TerrainRenderer::ClearBrushPreview();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Sandbox
