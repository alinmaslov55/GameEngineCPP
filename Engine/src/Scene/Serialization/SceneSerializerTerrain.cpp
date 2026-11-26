#include "JsonHelpers.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Terrain/TerrainChunkManager.h"
#include "Engine/Terrain/TerrainComponent.h"
#include "Engine/Terrain/Tilemap.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <vector>

namespace Engine::Serialization {

namespace {

template<typename TerrainLike>
json SerializeTerrainLayers(const TerrainLike* terrainLike) {
    json layersJson = json::array();
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const auto& layer = terrainLike->GetLayer(i);
        json layerJson = {
            {"TilingScale", layer.TilingScale},
            {"Glossiness", layer.Glossiness}
        };
        if (!layer.HeightMapPath.empty()) {
            layerJson["HeightMapPath"] = ResourceManager::ToRelativePath(layer.HeightMapPath);
        }
        if (!layer.NormalMapPath.empty()) {
            layerJson["NormalMapPath"] = ResourceManager::ToRelativePath(layer.NormalMapPath);
        }
        if (layer.LayerTexture && !layer.LayerTexture->GetFilepath().empty()) {
            layerJson["TexturePath"] = ResourceManager::ToRelativePath(layer.LayerTexture->GetFilepath());
        }
        layersJson.push_back(std::move(layerJson));
    }
    return layersJson;
}

template<typename TerrainLike>
void DeserializeTerrainLayers(TerrainLike* terrainLike, const json& parentJson) {
    if (!parentJson.contains("Layers") || !parentJson["Layers"].is_array()) {
        return;
    }
    const auto& layersArr = parentJson["Layers"];
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers && i < layersArr.size(); ++i) {
        const auto& lJson = layersArr[i];
        terrainLike->SetLayerTilingScale(i, lJson.value("TilingScale", 0.45f));
        terrainLike->SetLayerGlossiness(i, lJson.value("Glossiness", TerrainComponent::DefaultLayerGlossiness(i)));
        if (lJson.contains("HeightMapPath") && lJson["HeightMapPath"].is_string()) {
            terrainLike->SetLayerHeightMapPath(i, lJson["HeightMapPath"].get<std::string>());
        }
        if (lJson.contains("NormalMapPath") && lJson["NormalMapPath"].is_string()) {
            terrainLike->SetLayerNormalMapPath(i, lJson["NormalMapPath"].get<std::string>());
        }
        if (lJson.contains("TexturePath") && lJson["TexturePath"].is_string()) {
            const std::string texPath = lJson["TexturePath"].get<std::string>();
            if (!texPath.empty()) {
                terrainLike->SetLayerTexture(i, ResourceManager::Load<Texture>(texPath));
            }
        }
    }
}


} // namespace

void SerializeTilemap(const GameObject* go, json& compsJson) {
    auto* tilemap = go->GetComponent<Tilemap>();
    if (!tilemap) return;

    json tilemapJson;
    tilemapJson["Enabled"] = tilemap->IsEnabled();
    tilemapJson["Width"] = tilemap->GetWidth();
    tilemapJson["Height"] = tilemap->GetHeight();

    // Serialize TileSet definitions sorted by ID for deterministic JSON output
    json tileSetJson = json::array();
    if (tilemap->tileSet) {
        std::vector<uint32_t> sortedIds;
        sortedIds.reserve(tilemap->tileSet->GetTiles().size());
        for (const auto& [id, _] : tilemap->tileSet->GetTiles()) {
            sortedIds.push_back(id);
        }
        std::sort(sortedIds.begin(), sortedIds.end());

        for (uint32_t id : sortedIds) {
            auto subTex = tilemap->tileSet->GetTiles().at(id);
            json subTexJson = SerializeSubTexture(subTex);
            if (!subTexJson.is_null()) {
                tileSetJson.push_back({
                    {"ID", id},
                    {"SubTexture", std::move(subTexJson)}
                });
            }
        }
    }
    tilemapJson["TileSet"] = std::move(tileSetJson);

    // Serialize Grid IDs and Walkable flags
    json tilesArray = json::array();
    json walkableArray = json::array();
    bool hasNonWalkable = false;

    for (uint32_t y = 0; y < tilemap->GetHeight(); y++) {
        for (uint32_t x = 0; x < tilemap->GetWidth(); x++) {
            const Tile& t = tilemap->GetTile(x, y);
            tilesArray.push_back(t.id);
            walkableArray.push_back(t.walkable);
            if (!t.walkable) {
                hasNonWalkable = true;
            }
        }
    }
    tilemapJson["Tiles"] = std::move(tilesArray);
    if (hasNonWalkable) {
        tilemapJson["Walkable"] = std::move(walkableArray);
    }

    compsJson["Tilemap"] = std::move(tilemapJson);
}


void SerializeTerrain(const GameObject* go, json& compsJson) {
    auto* terrain = go->GetComponent<TerrainComponent>();
    if (!terrain) return;

    json terrainJson;
    terrainJson["Enabled"] = terrain->IsEnabled();
    terrainJson["Width"] = terrain->GetWidth();
    terrainJson["Depth"] = terrain->GetDepth();
    terrainJson["SplatWidth"] = terrain->GetSplatWidth();
    terrainJson["SplatHeight"] = terrain->GetSplatHeight();
    terrainJson["BlendSharpness"] = terrain->GetBlendSharpness();
    terrainJson["EdgeDarkening"] = terrain->GetEdgeDarkening();

    if (!terrain->GetSplatmapPath().empty()) {
        terrainJson["SplatmapPath"] = ResourceManager::ToRelativePath(terrain->GetSplatmapPath());
    }
    if (terrain->HasUnsavedMemoryEdits() || terrain->GetSplatmapPath().empty()) {
        terrainJson["SplatDataBase64"] = terrain->EncodeSplatBase64();
        const bool hasSecondaryWeights = std::any_of(
            terrain->GetSplatData1().begin(), terrain->GetSplatData1().end(), [](uint8_t b) { return b != 0u; });
        if (hasSecondaryWeights) {
            terrainJson["SplatData1Base64"] = terrain->EncodeSplat1Base64();
        }
    }

    terrainJson["Layers"] = SerializeTerrainLayers(terrain);
    compsJson["Terrain"] = std::move(terrainJson);
}

void SerializeTerrainChunkManager(const GameObject* go, json& compsJson) {
    auto* mgr = go->GetComponent<TerrainChunkManager>();
    if (!mgr) return;

    json tcmJson;
    tcmJson["Enabled"] = mgr->IsEnabled();
    tcmJson["ChunkSizeMeters"] = mgr->ChunkSizeMeters;
    tcmJson["SplatResolution"] = mgr->SplatResolution;
    tcmJson["ActiveRadiusChunks"] = mgr->ActiveRadiusChunks;
    tcmJson["CacheRetentionRadius"] = mgr->CacheRetentionRadius;
    tcmJson["MaxGpuUploadsPerFrame"] = mgr->MaxGpuUploadsPerFrame;
    tcmJson["AutoStreamWithCamera"] = mgr->AutoStreamWithCamera;
    tcmJson["BlendSharpness"] = mgr->GetBlendSharpness();
    tcmJson["EdgeDarkening"] = mgr->GetEdgeDarkening();
    const auto& wg = mgr->GetWorldGenConfig();
    tcmJson["WorldGen"] = {
        {"Enabled", wg.Enabled},
        {"SpawnSceneEntities", wg.SpawnSceneEntities},
        {"Seed", wg.Seed},
        {"OriginSafeRadius", wg.OriginSafeRadius},
        {"PoiApronRadius", wg.PoiApronRadius},
        {"PoiSuperCellChunks", wg.PoiSuperCellChunks}
    };

    tcmJson["Layers"] = SerializeTerrainLayers(mgr);

    json modChunksJson = json::array();
    for (const auto& [coord, chunk] : mgr->GetChunks()) {
        if (!chunk.UserModified || chunk.SplatData.empty()) {
            continue;
        }
        json chunkJson = {
            {"cx", coord.cx},
            {"cz", coord.cz},
            {"Resolution", chunk.SplatResolution},
            {"SplatDataBase64", TerrainComponent::EncodeRGBABase64(chunk.SplatData)}
        };
        const bool hasSecondaryWeights = std::any_of(
            chunk.SplatData1.begin(), chunk.SplatData1.end(), [](uint8_t b) { return b != 0u; });
        if (hasSecondaryWeights && chunk.SplatData1.size() == chunk.SplatData.size()) {
            chunkJson["SplatData1Base64"] = TerrainComponent::EncodeRGBABase64(chunk.SplatData1);
        }
        modChunksJson.push_back(std::move(chunkJson));
    }
    if (!modChunksJson.empty()) {
        tcmJson["ModifiedChunks"] = std::move(modChunksJson);
    }

    compsJson["TerrainChunkManager"] = std::move(tcmJson);
}


void DeserializeTilemap(GameObject* go, const json& tmJson, const std::string&) {
    if (!tmJson.is_object()) return;
    uint32_t width = tmJson.value("Width", 10u);
    uint32_t height = tmJson.value("Height", 10u);
    auto* tilemap = go->AddComponent<Tilemap>(width, height);
    tilemap->SetEnabled(tmJson.value("Enabled", true));

    if (tmJson.contains("TileSet") && tmJson["TileSet"].is_array()) {
        for (const auto& entry : tmJson["TileSet"]) {
            uint32_t id = entry.value("ID", 0u);
            if (id != 0 && entry.contains("SubTexture")) {
                if (auto subTex = DeserializeSubTexture(entry["SubTexture"])) {
                    tilemap->tileSet->AddTile(id, subTex);
                }
            }
        }
    }

    if (tmJson.contains("Tiles") && tmJson["Tiles"].is_array()) {
        const auto& tilesArr = tmJson["Tiles"];
        bool hasWalkable = tmJson.contains("Walkable") && tmJson["Walkable"].is_array();
        const json* walkableArr = hasWalkable ? &tmJson["Walkable"] : nullptr;

        size_t idx = 0;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                if (idx < tilesArr.size()) {
                    uint32_t tileId = tilesArr[idx].get<uint32_t>();
                    bool walkable = (walkableArr && idx < walkableArr->size())
                        ? (*walkableArr)[idx].get<bool>()
                        : true;
                    tilemap->SetTile(x, y, tileId, walkable);
                }
                ++idx;
            }
        }
    }
}

void DeserializeTerrain(GameObject* go, const json& trJson, const std::string& name) {
    if (!trJson.is_object()) return;
    const float width = trJson.value("Width", 14.0f);
    const float depth = trJson.value("Depth", 14.0f);
    const uint32_t splatW = trJson.value("SplatWidth", 128u);
    const uint32_t splatH = trJson.value("SplatHeight", 128u);

    auto* terrain = go->AddComponent<TerrainComponent>(width, depth, splatW, splatH);
    terrain->SetEnabled(trJson.value("Enabled", true));
    terrain->SetBlendSharpness(trJson.value("BlendSharpness", 0.68f));
    terrain->SetEdgeDarkening(trJson.value("EdgeDarkening", 0.16f));

    const std::string splatPath = trJson.value("SplatmapPath", "");
    if (!splatPath.empty()) {
        terrain->SetSplatmapPath(splatPath);
    }

    bool loadedFromBase64 = false;
    if (trJson.contains("SplatDataBase64") && trJson["SplatDataBase64"].is_string()) {
        loadedFromBase64 = terrain->DecodeSplatBase64(
            trJson["SplatDataBase64"].get<std::string>(), splatW, splatH
        );
        if (loadedFromBase64 && trJson.contains("SplatData1Base64") && trJson["SplatData1Base64"].is_string()) {
            terrain->DecodeSplat1Base64(trJson["SplatData1Base64"].get<std::string>(), splatW, splatH);
        }
    }
    if (!loadedFromBase64 && !splatPath.empty()) {
        terrain->LoadSplatmapFromPNG(ResourceManager::ResolvePath(splatPath));
    }

    DeserializeTerrainLayers(terrain, trJson);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored TerrainComponent on '{}' ({:.1f}x{:.1f}m, Splatmap={}x{})",
                      name, width, depth, splatW, splatH);
}

void DeserializeTerrainChunkManager(GameObject* go, const json& tcmJson, const std::string& name) {
    if (!tcmJson.is_object()) return;

    const float chunkSize = tcmJson.value("ChunkSizeMeters", 16.0f);
    const uint32_t splatRes = tcmJson.value("SplatResolution", 65u);
    const int32_t activeRadius = tcmJson.value("ActiveRadiusChunks", 3);
    const int32_t retentionRadius = tcmJson.value("CacheRetentionRadius", 6);

    auto* mgr = go->AddComponent<TerrainChunkManager>(chunkSize, splatRes, activeRadius, retentionRadius);
    mgr->SetEnabled(tcmJson.value("Enabled", true));
    mgr->MaxGpuUploadsPerFrame = std::max(1u, tcmJson.value("MaxGpuUploadsPerFrame", 2u));
    mgr->AutoStreamWithCamera = tcmJson.value("AutoStreamWithCamera", true);
    mgr->SetBlendSharpness(tcmJson.value("BlendSharpness", 0.68f));
    mgr->SetEdgeDarkening(tcmJson.value("EdgeDarkening", 0.16f));

    if (tcmJson.contains("WorldGen") && tcmJson["WorldGen"].is_object()) {
        const auto& wgJson = tcmJson["WorldGen"];
        WorldGenConfig wgConfig;
        wgConfig.Enabled = wgJson.value("Enabled", false);
        wgConfig.SpawnSceneEntities = wgJson.value("SpawnSceneEntities", true);
        wgConfig.Seed = wgJson.value("Seed", 42ULL);
        wgConfig.OriginSafeRadius = wgJson.value("OriginSafeRadius", 6.5f);
        wgConfig.PoiApronRadius = wgJson.value("PoiApronRadius", 3.5f);
        wgConfig.PoiSuperCellChunks = wgJson.value("PoiSuperCellChunks", 3);
        mgr->ConfigureProceduralWorld(wgConfig);
    }

    DeserializeTerrainLayers(mgr, tcmJson);

    if (tcmJson.contains("ModifiedChunks") && tcmJson["ModifiedChunks"].is_array()) {
        for (const auto& mcJson : tcmJson["ModifiedChunks"]) {
            if (!mcJson.is_object()) continue;
            const int32_t cx = mcJson.value("cx", 0);
            const int32_t cz = mcJson.value("cz", 0);
            const uint32_t cRes = std::clamp(mcJson.value("Resolution", mgr->SplatResolution), 5u, 513u);
            if (!mcJson.contains("SplatDataBase64") || !mcJson["SplatDataBase64"].is_string()) {
                continue;
            }
            std::vector<uint8_t> decodedSplat;
            if (!TerrainComponent::DecodeRGBABase64(mcJson["SplatDataBase64"].get<std::string>(), cRes, cRes, decodedSplat)) {
                ENGINE_CORE_WARN("SceneSerializer: Invalid SplatDataBase64 on chunk ({}, {})", cx, cz);
                continue;
            }
            TerrainChunk& chunk = mgr->EnsureEditableChunk({ cx, cz });
            chunk.SplatResolution = cRes;
            chunk.SplatData = std::move(decodedSplat);
            if (mcJson.contains("SplatData1Base64") && mcJson["SplatData1Base64"].is_string()) {
                if (!TerrainComponent::DecodeRGBABase64(mcJson["SplatData1Base64"].get<std::string>(), cRes, cRes, chunk.SplatData1)) {
                    ENGINE_CORE_WARN("SceneSerializer: Invalid SplatData1Base64 on chunk ({}, {})", cx, cz);
                }
            }
            chunk.MarkDirtyRect(0u, 0u, cRes - 1u, cRes - 1u, true);
        }
    }

    ENGINE_CORE_DEBUG("SceneSerializer: Restored TerrainChunkManager on '{}' (ChunkSize={:.1f}m, Splat={}x{}, ActiveRadius={})",
                      name, mgr->ChunkSizeMeters, mgr->SplatResolution, mgr->SplatResolution, mgr->ActiveRadiusChunks);
}


} // namespace Engine::Serialization
