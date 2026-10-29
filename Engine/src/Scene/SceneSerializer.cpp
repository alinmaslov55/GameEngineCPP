#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Utils/Logger.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace Engine {

namespace {

// =========================================================================
// GLM Vector <-> JSON Serialization Helpers
// =========================================================================

inline json ToJson(const glm::vec2& v) { return { v.x, v.y }; }
inline json ToJson(const glm::vec3& v) { return { v.x, v.y, v.z }; }
inline json ToJson(const glm::vec4& v) { return { v.r, v.g, v.b, v.a }; }

inline glm::vec2 AsVec2(const json& j, const glm::vec2& def = glm::vec2(0.0f)) {
    if (j.is_array() && j.size() >= 2) {
        return { j[0].get<float>(), j[1].get<float>() };
    }
    return def;
}

inline glm::vec3 AsVec3(const json& j, const glm::vec3& def = glm::vec3(0.0f)) {
    if (j.is_array() && j.size() >= 3) {
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    }
    return def;
}

inline glm::vec4 AsVec4(const json& j, const glm::vec4& def = glm::vec4(0.0f)) {
    if (j.is_array() && j.size() >= 4) {
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
    }
    return def;
}

// =========================================================================
// SubTexture2D Helpers
// =========================================================================

json SerializeSubTexture(const std::shared_ptr<SubTexture2D>& subTex) {
    if (!subTex || !subTex->GetTexture()) {
        return nullptr;
    }
    return {
        {"TexturePath", ResourceManager::ToRelativePath(subTex->GetTexture()->GetFilepath())},
        {"MinUV", ToJson(subTex->GetMin())},
        {"MaxUV", ToJson(subTex->GetMax())}
    };
}

std::shared_ptr<SubTexture2D> DeserializeSubTexture(const json& subTexJson) {
    if (!subTexJson.is_object() || !subTexJson.contains("TexturePath")) {
        return nullptr;
    }
    std::string texPath = subTexJson["TexturePath"].get<std::string>();
    if (texPath.empty()) {
        return nullptr;
    }
    auto tex = ResourceManager::Load<Texture>(texPath);
    if (!tex) {
        return nullptr;
    }
    glm::vec2 minUV = AsVec2(subTexJson["MinUV"]);
    glm::vec2 maxUV = AsVec2(subTexJson["MaxUV"], glm::vec2(1.0f));
    return std::make_shared<SubTexture2D>(tex, minUV, maxUV);
}

// =========================================================================
// Scene Camera Serialization
// =========================================================================

void SerializeSceneCamera(const PerspectiveCamera& sceneCam, json& outJson) {
    const auto& focal = sceneCam.GetFocalPoint();
    outJson = {
        {"FOV", sceneCam.GetFOVDegrees()},
        {"Distance", sceneCam.GetDistance()},
        {"Pitch", sceneCam.GetPitchDegrees()},
        {"Yaw", sceneCam.GetYawDegrees()},
        {"FocalPoint", ToJson(focal)}
    };
}

void DeserializeSceneCamera(const json& camJson, PerspectiveCamera& sceneCam) {
    sceneCam.SetFOVDegrees(camJson.value("FOV", 32.0f));
    sceneCam.SetDistance(camJson.value("Distance", 15.0f));
    sceneCam.SetPitchDegrees(camJson.value("Pitch", 40.0f));
    sceneCam.SetYawDegrees(camJson.value("Yaw", 0.0f));
    if (camJson.contains("FocalPoint")) {
        sceneCam.SetFocalPoint(AsVec3(camJson["FocalPoint"], sceneCam.GetFocalPoint()));
    }
    ENGINE_CORE_DEBUG("SceneSerializer: Restored Camera (FOV={:.1f}, Dist={:.1f}, Pitch={:.1f}, Yaw={:.1f})",
                      sceneCam.GetFOVDegrees(), sceneCam.GetDistance(), sceneCam.GetPitchDegrees(), sceneCam.GetYawDegrees());
}

// =========================================================================
// Per-Component Serializers
// =========================================================================

void SerializeTransform(const GameObject* go, json& compsJson) {
    if (auto* transform = go->GetTransform()) {
        compsJson["Transform"] = {
            {"Position", ToJson(transform->position)},
            {"Rotation", ToJson(transform->rotation)},
            {"Scale",    ToJson(transform->scale)}
        };
    }
}

void SerializeSpriteRenderer(const GameObject* go, json& compsJson) {
    auto* spriteRenderer = go->GetComponent<SpriteRenderer>();
    if (!spriteRenderer) return;

    json spriteJson;
    spriteJson["Color"] = ToJson(spriteRenderer->GetColor());
    spriteJson["BillboardMode"] = static_cast<int>(spriteRenderer->GetBillboardMode());
    spriteJson["AlphaCutoff"] = spriteRenderer->GetAlphaCutoff();
    spriteJson["CastShadows"] = spriteRenderer->GetCastShadows();
    spriteJson["ReceiveLighting"] = spriteRenderer->GetReceiveLighting();
    spriteJson["Emissive"] = spriteRenderer->GetEmissive();
    spriteJson["WindSwayWeight"] = spriteRenderer->GetWindSwayWeight();
    spriteJson["WindPhaseOffset"] = spriteRenderer->GetWindPhaseOffset();

    if (auto tex = spriteRenderer->GetTexture()) {
        spriteJson["TexturePath"] = ResourceManager::ToRelativePath(tex->GetFilepath());
    }
    if (auto subTex = spriteRenderer->GetSubTexture()) {
        spriteJson["SubTexture"] = SerializeSubTexture(subTex);
    }
    compsJson["SpriteRenderer"] = std::move(spriteJson);
}

void SerializeTilemap(const GameObject* go, json& compsJson) {
    auto* tilemap = go->GetComponent<Tilemap>();
    if (!tilemap) return;

    json tilemapJson;
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
    }

    json layersJson = json::array();
    for (uint32_t i = 0; i < TerrainComponent::MaxLayers; ++i) {
        const auto& layer = terrain->GetLayer(i);
        json layerJson = {
            {"TilingScale", layer.TilingScale}
        };
        if (layer.LayerTexture && !layer.LayerTexture->GetFilepath().empty()) {
            layerJson["TexturePath"] = ResourceManager::ToRelativePath(layer.LayerTexture->GetFilepath());
        }
        layersJson.push_back(std::move(layerJson));
    }
    terrainJson["Layers"] = std::move(layersJson);
    compsJson["Terrain"] = std::move(terrainJson);
}

void SerializeParticleSystem(const GameObject* go, json& compsJson) {
    auto* particleSystem = go->GetComponent<ParticleSystem>();
    if (!particleSystem) return;

    const auto& tp = particleSystem->TemplateProps;
    compsJson["ParticleSystem"] = {
        {"PoolSize", particleSystem->GetParticles().size()},
        {"AutoEmit", particleSystem->AutoEmit},
        {"EmitRate", particleSystem->EmitRate},
        {"SpawnOffset", ToJson(particleSystem->SpawnOffset)},
        {"EmissiveParticles", particleSystem->EmissiveParticles},
        {"TemplateProps", {
            {"Velocity", ToJson(tp.velocity)},
            {"VelocityVariation", ToJson(tp.velocityVariation)},
            {"ColorBegin", ToJson(tp.colorBegin)},
            {"ColorEnd", ToJson(tp.colorEnd)},
            {"SizeBegin", tp.sizeBegin},
            {"SizeEnd", tp.sizeEnd},
            {"SizeVariation", tp.sizeVariation},
            {"LifeTime", tp.lifeTime}
        }}
    };
}

void SerializeDirectionalLight(const GameObject* go, json& compsJson) {
    auto* dirLight = go->GetComponent<DirectionalLightComponent>();
    if (!dirLight) return;

    compsJson["DirectionalLight"] = {
        {"Elevation", dirLight->ElevationDegrees},
        {"Azimuth", dirLight->AzimuthDegrees},
        {"SunColor", ToJson(dirLight->SunColor)},
        {"SunIntensity", dirLight->SunIntensity},
        {"AmbientColor", ToJson(dirLight->AmbientColor)},
        {"AmbientIntensity", dirLight->AmbientIntensity},
        {"CastShadows", dirLight->CastShadows},
        {"ShadowSoftness", dirLight->ShadowSoftness},
        {"ShadowBias", dirLight->ShadowBias}
    };
}

void SerializePointLight(const GameObject* go, json& compsJson) {
    auto* pointLight = go->GetComponent<PointLightComponent>();
    if (!pointLight) return;

    compsJson["PointLight"] = {
        {"Color", ToJson(pointLight->Color)},
        {"Intensity", pointLight->Intensity},
        {"Radius", pointLight->Radius},
        {"Falloff", pointLight->Falloff},
        {"HeightOffset", pointLight->HeightOffset},
        {"Flicker", pointLight->Flicker},
        {"FlickerSpeed", pointLight->FlickerSpeed},
        {"FlickerAmount", pointLight->FlickerAmount}
    };
}

void SerializePostProcess(const GameObject* go, json& compsJson) {
    auto* pp = go->GetComponent<PostProcessComponent>();
    if (!pp) return;

    compsJson["PostProcess"] = {
        {"Enabled", pp->Enabled},
        {"BackdropColor", ToJson(pp->BackdropColor)},
        {"BloomEnabled", pp->BloomEnabled},
        {"BloomThreshold", pp->BloomThreshold},
        {"BloomKnee", pp->BloomKnee},
        {"BloomIntensity", pp->BloomIntensity},
        {"BloomScatter", pp->BloomScatter},
        {"ToneMapping", static_cast<int>(pp->ToneMapping)},
        {"Exposure", pp->Exposure},
        {"Temperature", pp->Temperature},
        {"Tint", pp->Tint},
        {"Contrast", pp->Contrast},
        {"Saturation", pp->Saturation},
        {"ShadowTint", ToJson(pp->ShadowTint)},
        {"HighlightTint", ToJson(pp->HighlightTint)},
        {"VignetteEnabled", pp->VignetteEnabled},
        {"VignetteIntensity", pp->VignetteIntensity},
        {"VignetteSmoothness", pp->VignetteSmoothness},
        {"VignetteColor", ToJson(pp->VignetteColor)}
    };
}

void SerializeWind(const GameObject* go, json& compsJson) {
    auto* wind = go->GetComponent<WindComponent>();
    if (!wind) return;

    compsJson["Wind"] = {
        {"Enabled", wind->Enabled},
        {"DirectionDegrees", wind->DirectionDegrees},
        {"Strength", wind->Strength},
        {"Speed", wind->Speed},
        {"Gustiness", wind->Gustiness},
        {"GustFrequency", wind->GustFrequency},
        {"SpatialWaveScale", wind->SpatialWaveScale},
        {"InteractiveRustle", wind->InteractiveRustle},
        {"PlayerInteractionRadius", wind->PlayerInteractionRadius},
        {"PlayerPushStrength", wind->PlayerPushStrength}
    };
}

void SerializeSquashStretch(const GameObject* go, json& compsJson) {
    auto* ss = go->GetComponent<SquashStretchComponent>();
    if (!ss) return;

    compsJson["SquashStretch"] = {
        {"Enabled", ss->Enabled},
        {"Stiffness", ss->Stiffness},
        {"Damping", ss->Damping},
        {"MaxSquash", ss->MaxSquash},
        {"MaxStretch", ss->MaxStretch},
        {"EnableWalkHop", ss->EnableWalkHop},
        {"WalkBobFrequency", ss->WalkBobFrequency},
        {"WalkSquashAmount", ss->WalkSquashAmount},
        {"WalkTiltDegrees", ss->WalkTiltDegrees},
        {"JumpLaunchStretch", ss->JumpLaunchStretch},
        {"LandingSquashImpact", ss->LandingSquashImpact}
    };
}

void SerializeRigidbody2D(const GameObject* go, json& compsJson) {
    auto* rb2d = go->GetComponent<Rigidbody2D>();
    if (!rb2d) return;

    compsJson["Rigidbody2D"] = {
        {"BodyType", static_cast<int>(rb2d->Type)},
        {"FixedRotation", rb2d->FixedRotation}
    };
}

void SerializeBoxCollider2D(const GameObject* go, json& compsJson) {
    auto* collider = go->GetComponent<BoxCollider2D>();
    if (!collider) return;

    compsJson["BoxCollider2D"] = {
        {"Size", ToJson(collider->size)},
        {"Offset", ToJson(collider->offset)},
        {"Density", collider->density},
        {"Friction", collider->friction},
        {"Restitution", collider->restitution},
        {"RestitutionThreshold", collider->restitutionThreshold},
        {"IsDynamic", collider->isDynamic},
        {"UseGravity", collider->useGravity},
        {"Gravity", collider->gravity},
        {"GroundLevel", collider->groundLevel}
    };
}

void SerializeCameraComponent(const GameObject* go, json& compsJson) {
    auto* cameraComp = go->GetComponent<CameraComponent>();
    if (!cameraComp) return;

    compsJson["CameraComponent"] = {
        {"FOV", cameraComp->FOV},
        {"Distance", cameraComp->Distance},
        {"Pitch", cameraComp->Pitch},
        {"Yaw", cameraComp->Yaw},
        {"FixedAspectRatio", cameraComp->FixedAspectRatio}
    };
}

void SerializeUIPanels(const GameObject* go, json& compsJson) {
    auto uiPanels = go->GetComponents<UIPanel>();
    if (uiPanels.empty()) return;

    json panelsJson = json::array();
    for (const auto* panel : uiPanels) {
        json panelJson = {
            {"Position", ToJson(panel->position)},
            {"Size",     ToJson(panel->size)},
            {"Color",    ToJson(panel->color)}
        };
        if (panel->texture) {
            panelJson["TexturePath"] = ResourceManager::ToRelativePath(panel->texture->GetFilepath());
        }
        panelsJson.push_back(std::move(panelJson));
    }
    compsJson["UIPanels"] = std::move(panelsJson);
}

void SerializeUILabels(const GameObject* go, json& compsJson) {
    auto uiLabels = go->GetComponents<UILabel>();
    if (uiLabels.empty()) return;

    json labelsJson = json::array();
    for (const auto* label : uiLabels) {
        json labelJson = {
            {"Text",     label->text},
            {"Position", ToJson(label->position)},
            {"Scale",    label->scale},
            {"Color",    ToJson(label->color)}
        };

        if (label->font) {
            json fontJson;
            if (label->font->atlasTexture) {
                fontJson["AtlasTexturePath"] = ResourceManager::ToRelativePath(label->font->atlasTexture->GetFilepath());
            }

            std::vector<char> sortedChars;
            sortedChars.reserve(label->font->GetCharacters().size());
            for (const auto& [chKey, _] : label->font->GetCharacters()) {
                sortedChars.push_back(chKey);
            }
            std::sort(sortedChars.begin(), sortedChars.end());

            json charsJson = json::array();
            for (char chKey : sortedChars) {
                const Character& chData = label->font->GetCharacters().at(chKey);
                json charEntry = {
                    {"Char",    std::string(1, chKey)},
                    {"Size",    ToJson(chData.size)},
                    {"Bearing", ToJson(chData.bearing)},
                    {"Advance", chData.advance}
                };
                if (chData.subTexture) {
                    charEntry["SubTexture"] = SerializeSubTexture(chData.subTexture);
                }
                charsJson.push_back(std::move(charEntry));
            }
            fontJson["Characters"] = std::move(charsJson);
            labelJson["Font"] = std::move(fontJson);
        }

        labelsJson.push_back(std::move(labelJson));
    }
    compsJson["UILabels"] = std::move(labelsJson);
}

void SerializeAudioSource(const GameObject* go, json& compsJson) {
    auto* audioSource = go->GetComponent<AudioSource>();
    if (!audioSource) return;

    json audioJson = json::object();
    if (auto clip = audioSource->GetClip()) {
        audioJson["ClipPath"] = ResourceManager::ToRelativePath(clip->GetFilepath());
    }
    compsJson["AudioSource"] = std::move(audioJson);
}

void SerializeAnimator(const GameObject* go, json& compsJson) {
    auto* animator = go->GetComponent<Animator>();
    if (!animator) return;

    json animJson;
    animJson["CurrentClip"] = animator->GetCurrentClipName();
    json clipsJson = json::array();

    for (const auto& [clipName, clipPtr] : animator->GetClips()) {
        if (!clipPtr) continue;
        json framesJson = json::array();
        for (const auto& frame : clipPtr->frames) {
            json fJson = SerializeSubTexture(frame);
            if (!fJson.is_null()) {
                framesJson.push_back(std::move(fJson));
            }
        }
        clipsJson.push_back({
            {"Name", clipPtr->name},
            {"FrameDuration", clipPtr->frameDuration},
            {"Loop", clipPtr->loop},
            {"Frames", std::move(framesJson)}
        });
    }
    animJson["Clips"] = std::move(clipsJson);
    compsJson["Animator"] = std::move(animJson);
}

// =========================================================================
// Component Serializer Registry
// =========================================================================

using ComponentSerializerFn = void(*)(const GameObject* go, json& compsJson);

static const ComponentSerializerFn s_ComponentSerializers[] = {
    SerializeTransform,
    SerializeSpriteRenderer,
    SerializeTilemap,
    SerializeTerrain,
    SerializeParticleSystem,
    SerializeDirectionalLight,
    SerializePointLight,
    SerializePostProcess,
    SerializeWind,
    SerializeSquashStretch,
    SerializeRigidbody2D,
    SerializeBoxCollider2D,
    SerializeCameraComponent,
    SerializeUIPanels,
    SerializeUILabels,
    SerializeAudioSource,
    SerializeAnimator
};

json SerializeEntity(const GameObject* go) {
    json entityJson;
    entityJson["UUID"] = go->GetUUID();
    entityJson["Name"] = go->GetName();
    json& compsJson = entityJson["Components"];

    for (auto serializerFn : s_ComponentSerializers) {
        serializerFn(go, compsJson);
    }

    return entityJson;
}

// =========================================================================
// Per-Component Deserializers
// =========================================================================

void DeserializeTransform(GameObject* go, const json& tJson, const std::string&) {
    auto* transform = go->GetTransform();
    if (!transform) return;
    if (tJson.contains("Position")) transform->position = AsVec3(tJson["Position"], transform->position);
    if (tJson.contains("Rotation")) transform->rotation = AsVec3(tJson["Rotation"], transform->rotation);
    if (tJson.contains("Scale"))    transform->scale    = AsVec3(tJson["Scale"], transform->scale);
}

void DeserializeSpriteRenderer(GameObject* go, const json& srJson, const std::string& name) {
    auto* sprite = go->AddComponent<SpriteRenderer>();
    if (srJson.contains("Color")) {
        sprite->SetColor(AsVec4(srJson["Color"], glm::vec4(1.0f)));
    }
    sprite->SetBillboardMode(static_cast<SpriteRenderer::BillboardMode>(srJson.value("BillboardMode", 0)));
    sprite->SetAlphaCutoff(srJson.value("AlphaCutoff", 0.15f));
    sprite->SetCastShadows(srJson.value("CastShadows", true));
    sprite->SetReceiveLighting(srJson.value("ReceiveLighting", true));
    sprite->SetEmissive(srJson.value("Emissive", 0.0f));
    sprite->SetWindSwayWeight(srJson.value("WindSwayWeight", 0.0f));
    sprite->SetWindPhaseOffset(srJson.value("WindPhaseOffset", 0.0f));
    if (srJson.contains("TexturePath") && srJson["TexturePath"].is_string()) {
        std::string texPath = srJson["TexturePath"].get<std::string>();
        if (!texPath.empty()) {
            sprite->SetTexture(ResourceManager::Load<Texture>(texPath));
        }
    }
    if (srJson.contains("SubTexture")) {
        if (auto subTex = DeserializeSubTexture(srJson["SubTexture"])) {
            sprite->SetSubTexture(subTex);
        }
    }
    ENGINE_CORE_TRACE("SceneSerializer: Restored SpriteRenderer on '{}' (BillboardMode={}, Cutoff={:.2f}, CastShadows={})",
                      name, static_cast<int>(sprite->GetBillboardMode()), sprite->GetAlphaCutoff(), sprite->GetCastShadows());
    if (sprite->GetWindSwayWeight() > 0.001f) {
        ENGINE_CORE_TRACE("SceneSerializer: Restored SpriteRenderer wind on '{}' (Weight={:.2f}, Phase={:.2f})",
                          name, sprite->GetWindSwayWeight(), sprite->GetWindPhaseOffset());
    }
}

void DeserializeTilemap(GameObject* go, const json& tmJson, const std::string&) {
    uint32_t width = tmJson.value("Width", 10u);
    uint32_t height = tmJson.value("Height", 10u);
    auto* tilemap = go->AddComponent<Tilemap>(width, height);

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
    }
    if (!loadedFromBase64 && !splatPath.empty()) {
        terrain->LoadSplatmapFromPNG(ResourceManager::ResolvePath(splatPath));
    }

    if (trJson.contains("Layers") && trJson["Layers"].is_array()) {
        const auto& layersArr = trJson["Layers"];
        for (uint32_t i = 0; i < TerrainComponent::MaxLayers && i < layersArr.size(); ++i) {
            const auto& lJson = layersArr[i];
            terrain->SetLayerTilingScale(i, lJson.value("TilingScale", 0.45f));
            if (lJson.contains("TexturePath") && lJson["TexturePath"].is_string()) {
                const std::string texPath = lJson["TexturePath"].get<std::string>();
                if (!texPath.empty()) {
                    terrain->SetLayerTexture(i, ResourceManager::Load<Texture>(texPath));
                }
            }
        }
    }
    ENGINE_CORE_DEBUG("SceneSerializer: Restored TerrainComponent on '{}' ({:.1f}x{:.1f}m, Splatmap={}x{})",
                      name, width, depth, splatW, splatH);
}

void DeserializeParticleSystem(GameObject* go, const json& psJson, const std::string&) {
    uint32_t poolSize = psJson.value("PoolSize", 1000u);
    auto* ps = go->AddComponent<ParticleSystem>(poolSize);
    ps->AutoEmit = psJson.value("AutoEmit", false);
    ps->EmitRate = psJson.value("EmitRate", 8.0f);
    if (psJson.contains("SpawnOffset")) {
        ps->SpawnOffset = AsVec3(psJson["SpawnOffset"], ps->SpawnOffset);
    }
    ps->EmissiveParticles = psJson.value("EmissiveParticles", false);
    if (psJson.contains("TemplateProps") && psJson["TemplateProps"].is_object()) {
        const auto& tpJson = psJson["TemplateProps"];
        auto& tp = ps->TemplateProps;
        if (tpJson.contains("Velocity"))          tp.velocity = AsVec3(tpJson["Velocity"], tp.velocity);
        if (tpJson.contains("VelocityVariation")) tp.velocityVariation = AsVec3(tpJson["VelocityVariation"], tp.velocityVariation);
        if (tpJson.contains("ColorBegin"))        tp.colorBegin = AsVec4(tpJson["ColorBegin"], tp.colorBegin);
        if (tpJson.contains("ColorEnd"))          tp.colorEnd = AsVec4(tpJson["ColorEnd"], tp.colorEnd);
        tp.sizeBegin = tpJson.value("SizeBegin", tp.sizeBegin);
        tp.sizeEnd = tpJson.value("SizeEnd", tp.sizeEnd);
        tp.sizeVariation = tpJson.value("SizeVariation", tp.sizeVariation);
        tp.lifeTime = tpJson.value("LifeTime", tp.lifeTime);
    }
}

void DeserializeDirectionalLight(GameObject* go, const json& dlJson, const std::string& name) {
    if (!dlJson.is_object()) return;
    auto* dirLight = go->AddComponent<DirectionalLightComponent>();
    dirLight->ElevationDegrees = dlJson.value("Elevation", 42.0f);
    dirLight->AzimuthDegrees = dlJson.value("Azimuth", -38.0f);
    if (dlJson.contains("SunColor"))     dirLight->SunColor = AsVec3(dlJson["SunColor"], dirLight->SunColor);
    dirLight->SunIntensity = dlJson.value("SunIntensity", 1.05f);
    if (dlJson.contains("AmbientColor")) dirLight->AmbientColor = AsVec3(dlJson["AmbientColor"], dirLight->AmbientColor);
    dirLight->AmbientIntensity = dlJson.value("AmbientIntensity", 0.62f);
    dirLight->CastShadows = dlJson.value("CastShadows", true);
    dirLight->ShadowSoftness = dlJson.value("ShadowSoftness", 1.35f);
    dirLight->ShadowBias = dlJson.value("ShadowBias", 0.0018f);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored DirectionalLight on '{}' (Elev={:.1f}, Azim={:.1f}, Intensity={:.2f})",
                      name, dirLight->ElevationDegrees, dirLight->AzimuthDegrees, dirLight->SunIntensity);
}

void DeserializePointLight(GameObject* go, const json& plJson, const std::string& name) {
    if (!plJson.is_object()) return;
    auto* pointLight = go->AddComponent<PointLightComponent>();
    if (plJson.contains("Color")) pointLight->Color = AsVec3(plJson["Color"], pointLight->Color);
    pointLight->Intensity = plJson.value("Intensity", 1.75f);
    pointLight->Radius = plJson.value("Radius", 4.5f);
    pointLight->Falloff = plJson.value("Falloff", 1.8f);
    pointLight->HeightOffset = plJson.value("HeightOffset", 0.85f);
    pointLight->Flicker = plJson.value("Flicker", true);
    pointLight->FlickerSpeed = plJson.value("FlickerSpeed", 7.5f);
    pointLight->FlickerAmount = plJson.value("FlickerAmount", 0.14f);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored PointLight on '{}' (Radius={:.1f}, Intensity={:.2f}, Flicker={})",
                      name, pointLight->Radius, pointLight->Intensity, pointLight->Flicker);
}

void DeserializePostProcess(GameObject* go, const json& ppJson, const std::string& name) {
    if (!ppJson.is_object()) return;
    auto* pp = go->AddComponent<PostProcessComponent>();
    pp->Enabled = ppJson.value("Enabled", true);
    if (ppJson.contains("BackdropColor")) pp->BackdropColor = AsVec3(ppJson["BackdropColor"], pp->BackdropColor);
    pp->BloomEnabled = ppJson.value("BloomEnabled", true);
    pp->BloomThreshold = ppJson.value("BloomThreshold", 1.05f);
    pp->BloomKnee = ppJson.value("BloomKnee", 0.45f);
    pp->BloomIntensity = ppJson.value("BloomIntensity", 0.42f);
    pp->BloomScatter = ppJson.value("BloomScatter", 0.72f);
    pp->ToneMapping = static_cast<PostProcessComponent::ToneMapMode>(
        ppJson.value("ToneMapping", static_cast<int>(PostProcessComponent::ToneMapMode::StorybookNeutral))
    );
    pp->Exposure = ppJson.value("Exposure", 0.05f);
    pp->Temperature = ppJson.value("Temperature", 0.06f);
    pp->Tint = ppJson.value("Tint", 0.0f);
    pp->Contrast = ppJson.value("Contrast", 1.06f);
    pp->Saturation = ppJson.value("Saturation", 1.08f);
    if (ppJson.contains("ShadowTint"))    pp->ShadowTint = AsVec3(ppJson["ShadowTint"], pp->ShadowTint);
    if (ppJson.contains("HighlightTint")) pp->HighlightTint = AsVec3(ppJson["HighlightTint"], pp->HighlightTint);
    pp->VignetteEnabled = ppJson.value("VignetteEnabled", true);
    pp->VignetteIntensity = ppJson.value("VignetteIntensity", 0.36f);
    pp->VignetteSmoothness = ppJson.value("VignetteSmoothness", 0.62f);
    if (ppJson.contains("VignetteColor")) pp->VignetteColor = AsVec3(ppJson["VignetteColor"], pp->VignetteColor);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored PostProcess on '{}' (ToneMap={}, Bloom={}, Vignette={})",
                      name, static_cast<int>(pp->ToneMapping), pp->BloomEnabled, pp->VignetteEnabled);
}

void DeserializeWind(GameObject* go, const json& wJson, const std::string& name) {
    if (!wJson.is_object()) return;
    auto* wind = go->AddComponent<WindComponent>();
    wind->Enabled = wJson.value("Enabled", true);
    wind->DirectionDegrees = wJson.value("DirectionDegrees", 25.0f);
    wind->Strength = wJson.value("Strength", 0.16f);
    wind->Speed = wJson.value("Speed", 2.15f);
    wind->Gustiness = wJson.value("Gustiness", 0.55f);
    wind->GustFrequency = wJson.value("GustFrequency", 0.42f);
    wind->SpatialWaveScale = wJson.value("SpatialWaveScale", 0.55f);
    wind->InteractiveRustle = wJson.value("InteractiveRustle", true);
    wind->PlayerInteractionRadius = wJson.value("PlayerInteractionRadius", 1.25f);
    wind->PlayerPushStrength = wJson.value("PlayerPushStrength", 0.34f);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored WindComponent on '{}' (Dir={:.1f} deg, Strength={:.2f}, Speed={:.2f})",
                      name, wind->DirectionDegrees, wind->Strength, wind->Speed);
}

void DeserializeSquashStretch(GameObject* go, const json& ssJson, const std::string& name) {
    if (!ssJson.is_object()) return;
    auto* ss = go->AddComponent<SquashStretchComponent>();
    ss->Enabled = ssJson.value("Enabled", true);
    ss->Stiffness = ssJson.value("Stiffness", 175.0f);
    ss->Damping = ssJson.value("Damping", 14.5f);
    ss->MaxSquash = ssJson.value("MaxSquash", 0.32f);
    ss->MaxStretch = ssJson.value("MaxStretch", 0.38f);
    ss->EnableWalkHop = ssJson.value("EnableWalkHop", true);
    ss->WalkBobFrequency = ssJson.value("WalkBobFrequency", 11.5f);
    ss->WalkSquashAmount = ssJson.value("WalkSquashAmount", 0.075f);
    ss->WalkTiltDegrees = ssJson.value("WalkTiltDegrees", 4.8f);
    ss->JumpLaunchStretch = ssJson.value("JumpLaunchStretch", 4.20f);
    ss->LandingSquashImpact = ssJson.value("LandingSquashImpact", 0.85f);
    ENGINE_CORE_DEBUG("SceneSerializer: Restored SquashStretchComponent on '{}' (Stiffness={:.1f}, Damping={:.1f})",
                      name, ss->Stiffness, ss->Damping);
}

void DeserializeRigidbody2D(GameObject* go, const json& rbJson, const std::string&) {
    auto* rb2d = go->AddComponent<Rigidbody2D>();
    rb2d->Type = static_cast<Rigidbody2D::BodyType>(rbJson.value("BodyType", 1));
    rb2d->FixedRotation = rbJson.value("FixedRotation", false);
}

void DeserializeBoxCollider2D(GameObject* go, const json& bcJson, const std::string&) {
    auto* collider = go->AddComponent<BoxCollider2D>();
    if (bcJson.contains("Size"))   collider->size = AsVec2(bcJson["Size"], collider->size);
    if (bcJson.contains("Offset")) collider->offset = AsVec2(bcJson["Offset"], collider->offset);
    collider->density = bcJson.value("Density", 1.0f);
    collider->friction = bcJson.value("Friction", 0.5f);
    collider->restitution = bcJson.value("Restitution", 0.0f);
    collider->restitutionThreshold = bcJson.value("RestitutionThreshold", 0.5f);
    collider->isDynamic = bcJson.value("IsDynamic", false);
    collider->useGravity = bcJson.value("UseGravity", true);
    collider->gravity = bcJson.value("Gravity", -9.8f);
    collider->groundLevel = bcJson.value("GroundLevel", 0.0f);
}

void DeserializeCameraComponent(GameObject* go, const json& camJson, const std::string&) {
    auto* camComp = go->AddComponent<CameraComponent>();
    camComp->FOV = camJson.value("FOV", 32.0f);
    camComp->Distance = camJson.value("Distance", 14.0f);
    camComp->Pitch = camJson.value("Pitch", 40.0f);
    camComp->Yaw = camJson.value("Yaw", 0.0f);
    camComp->FixedAspectRatio = camJson.value("FixedAspectRatio", false);
    camComp->SetViewportSize(1280, 720);
    camComp->SyncFromTransform(go->GetTransform());
}

void DeserializeSingleUIPanel(GameObject* go, const json& panelJson) {
    glm::vec2 pos = AsVec2(panelJson["Position"]);
    glm::vec2 sz  = AsVec2(panelJson["Size"]);
    glm::vec4 col = AsVec4(panelJson["Color"], glm::vec4(1.0f));
    auto* panel = go->AddComponent<UIPanel>(pos, sz, col);
    if (panelJson.contains("TexturePath") && panelJson["TexturePath"].is_string()) {
        std::string texPath = panelJson["TexturePath"].get<std::string>();
        if (!texPath.empty()) {
            panel->texture = ResourceManager::Load<Texture>(texPath);
        }
    }
}

void DeserializeUIPanels(GameObject* go, const json& j, const std::string&) {
    if (j.is_array()) {
        for (const auto& panelJson : j) {
            DeserializeSingleUIPanel(go, panelJson);
        }
    } else if (j.is_object()) {
        DeserializeSingleUIPanel(go, j);
    }
}

void DeserializeSingleUILabel(GameObject* go, const json& labelJson) {
    std::string text = labelJson.value("Text", "");
    glm::vec2 pos = AsVec2(labelJson["Position"]);

    std::shared_ptr<Font> font = nullptr;
    if (labelJson.contains("Font") && labelJson["Font"].is_object()) {
        const auto& fontJson = labelJson["Font"];
        font = std::make_shared<Font>();
        if (fontJson.contains("AtlasTexturePath") && fontJson["AtlasTexturePath"].is_string()) {
            std::string atlasPath = fontJson["AtlasTexturePath"].get<std::string>();
            if (!atlasPath.empty()) {
                font->atlasTexture = ResourceManager::Load<Texture>(atlasPath);
            }
        }
        if (fontJson.contains("Characters") && fontJson["Characters"].is_array()) {
            for (const auto& chJson : fontJson["Characters"]) {
                std::string chStr = chJson.value("Char", "");
                if (chStr.empty()) continue;

                Character ch{};
                if (chJson.contains("SubTexture")) {
                    ch.subTexture = DeserializeSubTexture(chJson["SubTexture"]);
                }
                if (chJson.contains("Size")) {
                    ch.size = AsVec2(chJson["Size"]);
                }
                if (chJson.contains("Bearing")) {
                    ch.bearing = AsVec2(chJson["Bearing"]);
                }
                ch.advance = chJson.value("Advance", 0.0f);
                font->AddCharacter(chStr[0], ch);
            }
        }
    }

    auto* label = go->AddComponent<UILabel>(text, font, pos);
    label->scale = labelJson.value("Scale", 1.0f);
    if (labelJson.contains("Color")) {
        label->color = AsVec4(labelJson["Color"], label->color);
    }
}

void DeserializeUILabels(GameObject* go, const json& j, const std::string&) {
    if (j.is_array()) {
        for (const auto& labelJson : j) {
            DeserializeSingleUILabel(go, labelJson);
        }
    } else if (j.is_object()) {
        DeserializeSingleUILabel(go, j);
    }
}

void DeserializeAudioSource(GameObject* go, const json& audioJson, const std::string&) {
    auto* audioSource = go->AddComponent<AudioSource>();
    if (audioJson.contains("ClipPath") && audioJson["ClipPath"].is_string()) {
        std::string clipPath = audioJson["ClipPath"].get<std::string>();
        if (!clipPath.empty()) {
            audioSource->SetClip(ResourceManager::Load<AudioClip>(clipPath));
        }
    }
}

void DeserializeAnimator(GameObject* go, const json& animJson, const std::string&) {
    auto* animator = go->AddComponent<Animator>();
    if (animJson.contains("Clips") && animJson["Clips"].is_array()) {
        for (const auto& clipJson : animJson["Clips"]) {
            std::string clipName = clipJson.value("Name", "");
            float duration = clipJson.value("FrameDuration", 0.1f);
            bool loop = clipJson.value("Loop", true);
            auto clip = std::make_shared<AnimationClip>(clipName, duration, loop);

            if (clipJson.contains("Frames") && clipJson["Frames"].is_array()) {
                for (const auto& frameJson : clipJson["Frames"]) {
                    if (auto frameSubTex = DeserializeSubTexture(frameJson)) {
                        clip->AddFrame(frameSubTex);
                    }
                }
            }
            animator->AddClip(clip);
        }
    }
    std::string currentClip = animJson.value("CurrentClip", "");
    if (!currentClip.empty()) {
        animator->Play(currentClip);
    }
}

// =========================================================================
// Component Deserializer Registry Table
// =========================================================================

using ComponentDeserializerFn = void(*)(GameObject* go, const json& j, const std::string& name);

struct ComponentDeserializerEntry {
    std::string_view key;
    ComponentDeserializerFn deserialize;
};

static const ComponentDeserializerEntry s_ComponentDeserializers[] = {
    { "Transform",        DeserializeTransform },
    { "SpriteRenderer",   DeserializeSpriteRenderer },
    { "Tilemap",          DeserializeTilemap },
    { "Terrain",          DeserializeTerrain },
    { "ParticleSystem",   DeserializeParticleSystem },
    { "DirectionalLight", DeserializeDirectionalLight },
    { "PointLight",       DeserializePointLight },
    { "PostProcess",      DeserializePostProcess },
    { "Wind",             DeserializeWind },
    { "SquashStretch",    DeserializeSquashStretch },
    { "Rigidbody2D",      DeserializeRigidbody2D },
    { "BoxCollider2D",    DeserializeBoxCollider2D },
    { "CameraComponent",  DeserializeCameraComponent },
    { "UIPanels",         DeserializeUIPanels },
    { "UIPanel",          DeserializeUIPanels },
    { "UILabels",         DeserializeUILabels },
    { "UILabel",          DeserializeUILabels },
    { "AudioSource",      DeserializeAudioSource },
    { "Animator",         DeserializeAnimator },
};

void DeserializeEntityComponents(GameObject* go, const json& componentsJson, const std::string& name) {
    if (!componentsJson.is_object()) return;

    for (const auto& entry : s_ComponentDeserializers) {
        auto it = componentsJson.find(entry.key);
        if (it != componentsJson.end()) {
            entry.deserialize(go, *it, name);
        }
    }
}

} // namespace

// =========================================================================
// SceneSerializer Class Implementation
// =========================================================================

SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
    : m_Scene(scene) {}

std::string SceneSerializer::SerializeToString() const {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::SerializeToString failed: Scene pointer is null");
        return {};
    }

    json sceneJson;
    sceneJson["SceneName"] = m_Scene->GetName();
    sceneJson["OrthographicSize"] = m_Scene->GetOrthographicSize();
    SerializeSceneCamera(m_Scene->GetCamera(), sceneJson["Camera"]);

    json gameObjectsJson = json::array();
    for (const auto& go : m_Scene->GetGameObjects()) {
        gameObjectsJson.push_back(SerializeEntity(go.get()));
    }
    sceneJson["GameObjects"] = std::move(gameObjectsJson);

    return sceneJson.dump(4);
}

bool SceneSerializer::Serialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Serialize failed: Scene pointer is null");
        return false;
    }

    std::string resolvedPath = ResourceManager::ResolvePath(filepath);
    ENGINE_CORE_INFO("Serializing scene '{}' to '{}'", m_Scene->GetName(), resolvedPath);

    std::error_code ec;
    auto parentDir = std::filesystem::path(resolvedPath).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    // Flush any dirty TerrainComponent splatmaps to external PNG assets before writing JSON
    const std::string sceneStem = std::filesystem::path(filepath).stem().string();
    uint32_t terrainCounter = 0;
    for (const auto& go : m_Scene->GetGameObjects()) {
        if (auto* terrain = go->GetComponent<TerrainComponent>()) {
            if (terrain->GetSplatmapPath().empty()) {
                const std::string baseName = sceneStem.empty() ? "terrain" : sceneStem;
                const std::string suffix = (terrainCounter == 0) ? "_splatmap.png" : ("_splatmap_" + std::to_string(terrainCounter) + ".png");
                terrain->SetSplatmapPath("assets/diorama/" + baseName + suffix);
            }
            ++terrainCounter;

            const std::string resolvedSplatPath = ResourceManager::ResolvePath(terrain->GetSplatmapPath());
            if (terrain->HasUnsavedMemoryEdits() || !std::filesystem::exists(resolvedSplatPath, ec)) {
                terrain->SaveSplatmapToPNG(resolvedSplatPath);
            }
        }
    }

    std::string serializedData = SerializeToString();
    if (serializedData.empty()) {
        return false;
    }

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene serialization: '{}'", resolvedPath);
        return false;
    }

    file << serializedData;
    file.close();
    ENGINE_CORE_INFO("Scene serialized successfully to '{}' ({} GameObjects)", resolvedPath, m_Scene->GetGameObjects().size());
    return true;
}

bool SceneSerializer::DeserializeFromString(const std::string& jsonString) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::DeserializeFromString failed: Scene pointer is null");
        return false;
    }

    json sceneJson;
    try {
        sceneJson = json::parse(jsonString);
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("Failed to parse scene JSON string: {}", e.what());
        return false;
    }

    if (sceneJson.contains("SceneName") && sceneJson["SceneName"].is_string()) {
        m_Scene->SetName(sceneJson["SceneName"].get<std::string>());
    }
    if (sceneJson.contains("OrthographicSize") && sceneJson["OrthographicSize"].is_number()) {
        m_Scene->SetOrthographicSize(sceneJson["OrthographicSize"].get<float>());
    }
    if (sceneJson.contains("Camera") && sceneJson["Camera"].is_object()) {
        DeserializeSceneCamera(sceneJson["Camera"], m_Scene->GetCamera());
    }

    size_t loadedCount = 0;
    if (sceneJson.contains("GameObjects") && sceneJson["GameObjects"].is_array()) {
        for (const auto& entityJson : sceneJson["GameObjects"]) {
            std::string name = entityJson.value("Name", "GameObject");
            GameObject* go = entityJson.contains("UUID")
                ? m_Scene->CreateGameObjectWithUUID(entityJson["UUID"].get<uint64_t>(), name)
                : m_Scene->CreateGameObject(name);
            loadedCount++;

            if (entityJson.contains("Components")) {
                DeserializeEntityComponents(go, entityJson["Components"], name);
            }
        }
    }

    ENGINE_CORE_DEBUG("Scene deserialized from JSON ({} GameObjects loaded)", loadedCount);
    return true;
}

bool SceneSerializer::Deserialize(const std::string& filepath) {
    if (!m_Scene) {
        ENGINE_CORE_ERROR("SceneSerializer::Deserialize failed: Scene pointer is null");
        return false;
    }

    std::string resolvedPath = ResourceManager::ResolvePath(filepath);
    ENGINE_CORE_INFO("Deserializing scene from '{}'", resolvedPath);

    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("Failed to open file for scene deserialization: '{}'", resolvedPath);
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (!DeserializeFromString(content)) {
        ENGINE_CORE_ERROR("Failed to deserialize scene from '{}'", resolvedPath);
        return false;
    }

    ENGINE_CORE_INFO("Scene deserialized successfully from '{}' ({} GameObjects loaded)",
                     resolvedPath, m_Scene->GetGameObjects().size());
    return true;
}

} // namespace Engine
