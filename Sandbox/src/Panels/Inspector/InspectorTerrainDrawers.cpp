#include "Panels/InspectorPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/TerrainChunkManager.h>
#include <Engine/Scene/TerrainComponent.h>
#include <Engine/Scene/Tilemap.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <filesystem>
#include <string>

namespace Sandbox {

namespace {

template<typename TerrainOwnerT>
void DrawTerrainLayerSlots(TerrainOwnerT& owner) {
    ImGui::Separator();
    const char* layerLabels[Engine::TerrainComponent::MaxLayers] = {
        "Layer 0: Grass (Blackwood Thickets)",
        "Layer 1: Ash (Ash Barrens)",
        "Layer 2: Tar (Tar Bogs)",
        "Layer 3: Chalk (Chalk Highlands)",
        "Layer 4: Cobblestone (Sanctuary Cobble)",
        "Layer 5: Scorched Ash",
        "Layer 6: Spare Layer 1",
        "Layer 7: Spare Layer 2"
    };
    for (uint32_t i = 0; i < Engine::TerrainComponent::MaxLayers; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s", layerLabels[i]);

        auto& layer = owner.GetLayer(i);
        std::string texName = "Default Color";
        if (layer.LayerTexture && !layer.LayerTexture->GetFilepath().empty()) {
            texName = std::filesystem::path(layer.LayerTexture->GetFilepath()).filename().string();
        }

        ImGui::Button(texName.c_str(), ImVec2(145.0f, 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const std::string filepath(static_cast<const char*>(payload->Data));
                if (filepath.find(".png") != std::string::npos || filepath.find(".jpg") != std::string::npos) {
                    owner.SetLayerTexture(i, Engine::ResourceManager::Load<Engine::Texture>(filepath));
                    ENGINE_INFO("Inspector: Assigned terrain layer {} texture '{}'", i, filepath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(85.0f);
        float tiling = layer.TilingScale;
        if (ImGui::DragFloat("Tiling", &tiling, 0.01f, 0.05f, 10.0f, "%.2f")) {
            owner.SetLayerTilingScale(i, tiling);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(75.0f);
        float gloss = layer.Glossiness;
        if (ImGui::SliderFloat("Gloss", &gloss, 0.0f, 1.0f, "%.2f")) {
            owner.SetLayerGlossiness(i, gloss);
        }

        ImGui::PopID();
    }
}

} // namespace

void InspectorPanel::DrawTerrain(Engine::TerrainComponent& terrain, Engine::GameObject* entity) {
    (void)entity;
    float dims[2] = { terrain.GetWidth(), terrain.GetDepth() };
    if (ImGui::DragFloat2("World Size (X, Z)", dims, 0.25f, 1.0f, 500.0f, "%.1f m")) {
        terrain.SetWidth(dims[0]);
        terrain.SetDepth(dims[1]);
    }

    ImGui::Text("Splatmap Resolution: %u x %u", terrain.GetSplatWidth(), terrain.GetSplatHeight());
    if (ImGui::SmallButton("64x64")) {
        terrain.ResizeSplatmap(64, 64);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("128x128")) {
        terrain.ResizeSplatmap(128, 128);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("256x256")) {
        terrain.ResizeSplatmap(256, 256);
    }

    float sharpness = terrain.GetBlendSharpness();
    if (ImGui::SliderFloat("Blend Sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) {
        terrain.SetBlendSharpness(sharpness);
    }

    float darkening = terrain.GetEdgeDarkening();
    if (ImGui::SliderFloat("Edge Ink Border", &darkening, 0.0f, 0.45f, "%.2f")) {
        terrain.SetEdgeDarkening(darkening);
    }

    DrawTerrainLayerSlots(terrain);
}

void InspectorPanel::DrawTerrainChunkManager(Engine::TerrainChunkManager& chunkMgr, Engine::GameObject* entity) {
    (void)entity;
    ImGui::DragFloat("Chunk Size (m)", &chunkMgr.ChunkSizeMeters, 0.5f, 4.0f, 64.0f, "%.1f m");
    ImGui::SliderInt("Active Radius (Chunks)", &chunkMgr.ActiveRadiusChunks, 1, 6);
    chunkMgr.CacheRetentionRadius = std::max(chunkMgr.ActiveRadiusChunks, chunkMgr.CacheRetentionRadius);
    ImGui::SliderInt("Cache Retention Radius", &chunkMgr.CacheRetentionRadius, chunkMgr.ActiveRadiusChunks, 12);
    int uploadsPerFrame = static_cast<int>(chunkMgr.MaxGpuUploadsPerFrame);
    if (ImGui::SliderInt("Max GPU Uploads / Frame", &uploadsPerFrame, 1, 8)) {
        chunkMgr.MaxGpuUploadsPerFrame = static_cast<uint32_t>(uploadsPerFrame);
    }
    ImGui::Checkbox("Auto-Stream With Camera", &chunkMgr.AutoStreamWithCamera);
    ImGui::Text("Splat Resolution: %u x %u (C0 stitched)", chunkMgr.SplatResolution, chunkMgr.SplatResolution);
    ImGui::TextDisabled("Active: %zu | RAM Cache: %zu | Upload Queue: %zu",
                        chunkMgr.GetActiveChunkCount(),
                        chunkMgr.GetCachedChunkCount(),
                        chunkMgr.GetPendingUploadCount());

    float sharpness = chunkMgr.GetBlendSharpness();
    if (ImGui::SliderFloat("Blend Sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) {
        chunkMgr.SetBlendSharpness(sharpness);
    }
    float darkening = chunkMgr.GetEdgeDarkening();
    if (ImGui::SliderFloat("Edge Ink Border", &darkening, 0.0f, 0.45f, "%.2f")) {
        chunkMgr.SetEdgeDarkening(darkening);
    }

    ImGui::Separator();
    ImGui::Text("Procedural Biome World (TER-02)");
    auto& wg = chunkMgr.GetWorldGenConfigMutable();
    bool regenRequested = false;
    regenRequested |= ImGui::Checkbox("Enable Procedural World", &wg.Enabled);
    if (wg.Enabled) {
        ImGui::Checkbox("Spawn Pooled Props & POIs", &wg.SpawnSceneEntities);
        int seedInt = static_cast<int>(wg.Seed & 0x7FFFFFFFULL);
        if (ImGui::InputInt("World Seed", &seedInt)) {
            wg.Seed = static_cast<uint64_t>(std::max(0, seedInt));
            regenRequested = true;
        }
        regenRequested |= ImGui::SliderFloat("Origin Safe Radius", &wg.OriginSafeRadius, 2.0f, 16.0f, "%.1f m");
        regenRequested |= ImGui::SliderFloat("POI Apron Radius", &wg.PoiApronRadius, 1.5f, 8.0f, "%.1f m");
        if (ImGui::Button("Regenerate Procedural World")) {
            regenRequested = true;
        }
    }
    if (regenRequested) {
        chunkMgr.ConfigureProceduralWorld(wg);
    }

    ImGui::Separator();
    ImGui::Text("Tar & Fire Cellular Automaton");
    ImGui::Text("Active Fire Cells: %zu", chunkMgr.GetFireGrid().GetActiveFireCount());
    if (ImGui::Button("Ignite at Origin (r=2.5m)")) {
        chunkMgr.IgniteAtXZ(glm::vec2(0.0f, 0.0f), 2.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Extinguish All")) {
        chunkMgr.GetFireGrid().Clear();
    }

    DrawTerrainLayerSlots(chunkMgr);
}

void InspectorPanel::DrawTilemap(Engine::Tilemap& tilemapComp, Engine::GameObject* entity) {
    (void)entity;
    ImGui::Text("Dimensions: %u x %u (%u tiles)",
                tilemapComp.GetWidth(),
                tilemapComp.GetHeight(),
                tilemapComp.GetWidth() * tilemapComp.GetHeight());
    ImGui::TextDisabled("Switch Tile Palette to 'Tile Grid' to paint discrete tiles.");
}


} // namespace Sandbox
