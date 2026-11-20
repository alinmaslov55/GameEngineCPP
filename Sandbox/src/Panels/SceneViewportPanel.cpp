#include "Panels/SceneViewportPanel.h"

#include <Engine/Core/Input.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Renderer/TerrainRenderer.h>
#include <Engine/Renderer/SceneRenderer.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/Components/FoliageWindComponents.h>
#include <Engine/Terrain/TerrainComponent.h>
#include <Engine/Terrain/TerrainChunkManager.h>
#include <Engine/Terrain/Tilemap.h>
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

void DrawDashedLine(ImDrawList* drawList, const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness, float dashLen, float gapLen) {
    if (!drawList) return;
    const float dx = p2.x - p1.x;
    const float dy = p2.y - p1.y;
    const float dist = std::hypot(dx, dy);
    if (dist <= 0.001f) return;

    const float dirX = dx / dist;
    const float dirY = dy / dist;

    float current = 0.0f;
    while (current < dist) {
        const float segEnd = std::min(current + dashLen, dist);
        const ImVec2 start{ p1.x + dirX * current, p1.y + dirY * current };
        const ImVec2 end{ p1.x + dirX * segEnd, p1.y + dirY * segEnd };
        drawList->AddLine(start, end, col, thickness);
        current += dashLen + gapLen;
    }
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

    // 0. Simulator Toolbar: Aspect Combo Box & 5% Safe Area Toggle
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

    ImGui::SetNextItemWidth(150.0f);
    const char* aspectNames[] = {
        "Aspect: Free",
        "16:9 Landscape",
        "16:10 Landscape",
        "9:16 Portrait",
        "10:16 Portrait"
    };
    int currentAspect = static_cast<int>(m_AspectMode);
    if (ImGui::Combo("##AspectModeCombo", &currentAspect, aspectNames, IM_ARRAYSIZE(aspectNames))) {
        m_AspectMode = static_cast<Engine::ViewportAspectMode>(currentAspect);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show 5% Safe Area", &m_ShowSafeArea);

    ImGui::SameLine();
    bool showGizmos = Engine::SceneRenderer::IsShowingDebugGizmos();
    if (ImGui::Checkbox("Debug Gizmos", &showGizmos)) {
        Engine::SceneRenderer::SetShowDebugGizmos(showGizmos);
    }

    // Live display of resolution and aspect ratio in toolbar
    ImGui::SameLine();
    const float curRatio = (m_ViewportSize.y > 0.0f) ? (m_ViewportSize.x / m_ViewportSize.y) : 0.0f;
    ImGui::TextDisabled("| %dx%d (%.2f:1)",
                        static_cast<int>(m_ViewportSize.x),
                        static_cast<int>(m_ViewportSize.y),
                        curRatio);

    ImGui::PopStyleVar(); // FramePadding

    // 1. Calculate Letterboxed / Pillarboxed dimensions inside remaining panel space
    const ImVec2 contentAvail = ImGui::GetContentRegionAvail();
    const glm::vec2 availSize = { std::max(contentAvail.x, 1.0f), std::max(contentAvail.y, 1.0f) };

    const auto metrics = Engine::ViewportAspectSimulator::CalculateMetrics(availSize, m_AspectMode);
    m_ViewportSize = metrics.RenderSize;
    m_ViewportOffset = metrics.Offset;

    // Apply offset to center image within panel
    const ImVec2 cursorOrigin = ImGui::GetCursorPos();
    if (m_ViewportOffset.x > 0.0f || m_ViewportOffset.y > 0.0f) {
        ImGui::SetCursorPos(ImVec2(cursorOrigin.x + m_ViewportOffset.x, cursorOrigin.y + m_ViewportOffset.y));
    }

    if (framebuffer) {
        const uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(textureID)),
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
                     ImVec2{ 0.0f, 1.0f },
                     ImVec2{ 1.0f, 0.0f });
    }
    const ImVec2 imageMin = ImGui::GetItemRectMin();

    if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
        Engine::Input::SetViewportRect({ imageMin.x, imageMin.y }, m_ViewportSize);
    }

    // 2. Draw Dashed Amber 5% Safe Area Overlay
    if (m_ShowSafeArea && m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
        const auto safeRect = Engine::ViewportAspectSimulator::CalculateSafeArea(
            { imageMin.x, imageMin.y },
            m_ViewportSize,
            Engine::ViewportAspectSimulator::kDefaultSafeAreaMargin
        );

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 amberColor = IM_COL32(245, 166, 35, 230); // Dashed amber (RGB: 245, 166, 35)
        const float dashLen = 8.0f;
        const float gapLen = 5.0f;
        const float thickness = 1.5f;

        const ImVec2 sMin = { safeRect.Min.x, safeRect.Min.y };
        const ImVec2 sMax = { safeRect.Max.x, safeRect.Max.y };

        // 4 dashed boundary edges
        DrawDashedLine(drawList, { sMin.x, sMin.y }, { sMax.x, sMin.y }, amberColor, thickness, dashLen, gapLen);
        DrawDashedLine(drawList, { sMax.x, sMin.y }, { sMax.x, sMax.y }, amberColor, thickness, dashLen, gapLen);
        DrawDashedLine(drawList, { sMax.x, sMax.y }, { sMin.x, sMax.y }, amberColor, thickness, dashLen, gapLen);
        DrawDashedLine(drawList, { sMin.x, sMax.y }, { sMin.x, sMin.y }, amberColor, thickness, dashLen, gapLen);

        // Corner tick marks and label
        const float tickSize = 12.0f;
        drawList->AddLine({ sMin.x, sMin.y }, { sMin.x + tickSize, sMin.y }, amberColor, 2.0f);
        drawList->AddLine({ sMin.x, sMin.y }, { sMin.x, sMin.y + tickSize }, amberColor, 2.0f);

        drawList->AddLine({ sMax.x, sMin.y }, { sMax.x - tickSize, sMin.y }, amberColor, 2.0f);
        drawList->AddLine({ sMax.x, sMin.y }, { sMax.x, sMin.y + tickSize }, amberColor, 2.0f);

        drawList->AddLine({ sMin.x, sMax.y }, { sMin.x + tickSize, sMax.y }, amberColor, 2.0f);
        drawList->AddLine({ sMin.x, sMax.y }, { sMin.x, sMax.y - tickSize }, amberColor, 2.0f);

        drawList->AddLine({ sMax.x, sMax.y }, { sMax.x - tickSize, sMax.y }, amberColor, 2.0f);
        drawList->AddLine({ sMax.x, sMax.y }, { sMax.x, sMax.y - tickSize }, amberColor, 2.0f);

        drawList->AddText(ImVec2(sMin.x + 4.0f, sMin.y + 4.0f), amberColor, "5% Safe Area");
    }

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
                    auto* chunkMgr = terrainEntity->GetComponent<Engine::TerrainChunkManager>();
                    auto* terrain = !chunkMgr ? terrainEntity->GetComponent<Engine::TerrainComponent>() : nullptr;
                    auto* trTransform = terrainEntity->GetTransform();
                    const glm::vec3 origin = trTransform ? trTransform->position : glm::vec3(0.0f);
                    const glm::vec3 scale  = trTransform ? trTransform->scale    : glm::vec3(1.0f);

                    glm::vec3 hitWorldPos{ 0.0f };
                    if ((chunkMgr || terrain) && viewCamera.RaycastGroundPlane(ndc, origin.y, hitWorldPos)) {
                        const glm::vec2 hitXZ = { hitWorldPos.x, hitWorldPos.z };
                        Engine::TerrainRenderer::SetBrushPreview(
                            true,
                            hitXZ,
                            palettePanel.GetBrushRadius(),
                            palettePanel.GetBrushFalloff()
                        );
                        showSplatRing = true;

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            if (chunkMgr) {
                                chunkMgr->PaintWorldBrush(
                                    hitXZ.x,
                                    hitXZ.y,
                                    palettePanel.GetSelectedSplatLayer(),
                                    palettePanel.GetBrushRadius(),
                                    palettePanel.GetBrushFalloff(),
                                    palettePanel.GetBrushStrength()
                                );
                            } else {
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
                            }
                            m_HasActiveSplatStroke = true;
                            m_LastSplatStrokeXZ = hitXZ;
                        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_HasActiveSplatStroke) {
                            const float stepThreshold = std::max(0.08f, palettePanel.GetBrushRadius() * 0.18f);
                            if (glm::length(hitXZ - m_LastSplatStrokeXZ) >= stepThreshold) {
                                if (chunkMgr) {
                                    chunkMgr->PaintWorldSegment(
                                        m_LastSplatStrokeXZ,
                                        hitXZ,
                                        palettePanel.GetSelectedSplatLayer(),
                                        palettePanel.GetBrushRadius(),
                                        palettePanel.GetBrushFalloff(),
                                        palettePanel.GetBrushStrength() * 0.80f
                                    );
                                } else {
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
                                }
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
