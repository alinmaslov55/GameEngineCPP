#include "Engine/Terrain/ProceduralPropFactory.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Prefab.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <utility>

namespace Engine {

namespace {

static const PropTemplateSpec kPropSpecs[ProceduralPropFactory::kPropKindCount] = {
    // 0: ConiferPine
    { "proc_conifer_pine", "Procedural Pine", EntityTag::HarvestableTree, EntityLayer::Environment,
      "assets/diorama/pine_tree.png", { 2.0f, 2.75f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
      0.38f, 0.0f, true, true, { 0.55f, 0.45f }, PhysicsLayer::Environment, false, 0.0f, 0.0f, {} },
    // 1: BerryShrub
    { "proc_berry_shrub", "Procedural Shrub", EntityTag::Foliage, EntityLayer::Default,
      "assets/diorama/berry_bush.png", { 1.05f, 0.95f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
      0.68f, 0.0f, true, false, {}, 0ULL, false, 0.0f, 0.0f, {} },
    // 2: TallGrassCluster
    { "proc_tall_grass", "Procedural Grass", EntityTag::Foliage, EntityLayer::Default,
      "assets/diorama/tall_grass_tuft.png", { 0.68f, 0.76f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
      0.90f, 0.0f, false, false, {}, 0ULL, false, 0.0f, 0.0f, {} },
    // 3: DeadSnag
    { "proc_dead_snag", "Dead Snag", EntityTag::HarvestableTree, EntityLayer::Environment,
      "assets/diorama/pine_tree.png", { 1.45f, 2.15f, 1.0f }, { 0.44f, 0.36f, 0.34f, 1.0f },
      0.12f, 0.0f, true, true, { 0.48f, 0.40f }, PhysicsLayer::Environment, false, 0.0f, 0.0f, {} },
    // 4: BogBubbleEmitter
    { "proc_bog_bubble", "Bog Bubble", EntityTag::Untagged, EntityLayer::Default,
      "", { 0.65f, 0.65f, 1.0f }, { 0.38f, 0.78f, 0.36f, 0.85f },
      0.20f, 0.35f, false, false, {}, 0ULL, false, 0.0f, 0.0f, {} },
    // 5: BrokenFence
    { "proc_broken_fence", "Broken Fence", EntityTag::DestructibleProp, EntityLayer::LowObstacle,
      "assets/diorama/cult_banner.png", { 1.05f, 0.85f, 1.0f }, { 0.72f, 0.64f, 0.56f, 1.0f },
      0.0f, 0.0f, true, true, { 0.95f, 0.35f }, PhysicsLayer::LowObstacle, false, 0.0f, 0.0f, {} },
    // 6: BonePile
    { "proc_bone_pile", "Bone Pile", EntityTag::Untagged, EntityLayer::Default,
      "", { 0.70f, 0.48f, 1.0f }, { 0.88f, 0.86f, 0.80f, 1.0f },
      0.0f, 0.0f, false, false, {}, 0ULL, false, 0.0f, 0.0f, {} },
    // 7: POI_DeadBrazier (dormant until ignited)
    { "proc_poi_dead_brazier", "Dead Brazier POI", EntityTag::SafeZoneBrazier, EntityLayer::Environment,
      "assets/diorama/ritual_brazier.png", { 0.95f, 1.35f, 1.0f }, { 0.72f, 0.72f, 0.76f, 1.0f },
      0.0f, 0.05f, true, true, { 0.55f, 0.45f }, PhysicsLayer::Environment, true, 1.8f, 0.25f, { 0.85f, 0.35f, 0.16f } },
    // 8: POI_UmbralShrine
    { "proc_poi_umbral_shrine", "Umbral Shrine POI", EntityTag::Shrine, EntityLayer::Environment,
      "assets/diorama/cult_shrine.png", { 1.75f, 2.35f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
      0.0f, 0.28f, true, true, { 1.15f, 0.65f }, PhysicsLayer::Environment, true, 3.2f, 0.95f, { 0.95f, 0.22f, 0.18f } }
};

} // namespace

const PropTemplateSpec& ProceduralPropFactory::GetPropSpec(ProceduralPropKind kind) noexcept {
    const size_t idx = std::min(static_cast<size_t>(kind), kPropKindCount - 1u);
    return kPropSpecs[idx];
}

std::shared_ptr<Prefab> ProceduralPropFactory::BuildBuiltinPropPrefab(ProceduralPropKind kind) {
    const auto& spec = GetPropSpec(kind);

    nlohmann::json spriteJson = {
        { "Color", { spec.Color.r, spec.Color.g, spec.Color.b, spec.Color.a } },
        { "AlphaCutoff", 0.18f },
        { "CastShadows", spec.CastShadows },
        { "ReceiveLighting", true },
        { "Emissive", spec.Emissive },
        { "WindSwayWeight", spec.WindSway }
    };
    if (spec.TexturePath && spec.TexturePath[0] != '\0') {
        std::error_code ec;
        const std::string resolved = ResourceManager::ResolvePath(spec.TexturePath);
        if (std::filesystem::exists(resolved, ec)) {
            spriteJson["TexturePath"] = spec.TexturePath;
        }
    }

    nlohmann::json compsJson = {
        { "Transform", {
            { "Position", { 0.0f, 0.0f, 0.0f } },
            { "Rotation", { 0.0f, 0.0f, 0.0f } },
            { "Scale", { spec.BaseScale.x, spec.BaseScale.y, spec.BaseScale.z } }
        } },
        { "SpriteRenderer", std::move(spriteJson) }
    };

    if (spec.HasCollider) {
        compsJson["BoxCollider2D"] = {
            { "Size", { spec.ColliderSize.x, spec.ColliderSize.y } },
            { "CategoryBits", spec.ColliderCategory },
            { "MaskBits", PhysicsLayer::All }
        };
    }

    if (spec.HasLight) {
        compsJson["PointLight"] = {
            { "Color", { spec.LightColor.r, spec.LightColor.g, spec.LightColor.b } },
            { "Intensity", spec.LightIntensity },
            { "Radius", spec.LightRadius },
            { "Falloff", 1.9f },
            { "HeightOffset", 1.0f },
            { "Semantic", static_cast<int>(LightSemantic::NeutralShrine) }
        };
    }

    nlohmann::json entityJson = {
        { "UUID", 1 },
        { "Name", spec.EntityName },
        { "Tag", EntityTagToString(spec.Tag) },
        { "LayerMask", spec.Layer },
        { "ParentUUID", 0 },
        { "Components", std::move(compsJson) }
    };

    return std::make_shared<Prefab>(std::move(entityJson));
}

std::shared_ptr<Prefab> ProceduralPropFactory::BuildTarFirewallPrefab() {
    nlohmann::json compsJson = {
        { "Transform", {
            { "Position", { 0.0f, 0.45f, 0.0f } },
            { "Rotation", { 0.0f, 0.0f, 0.0f } },
            { "Scale", { 1.0f, 1.0f, 1.0f } }
        } },
        { "SpriteRenderer", {
            { "Color", { 1.0f, 0.45f, 0.10f, 0.95f } },
            { "BillboardMode", 1 }, // CylindricalY
            { "AlphaCutoff", 0.05f },
            { "CastShadows", false },
            { "ReceiveLighting", false },
            { "Emissive", 2.5f }
        } },
        { "ParticleSystem", {
            { "PoolSize", 64u },
            { "AutoEmit", true },
            { "EmitRate", 16.0f },
            { "EmissiveParticles", true },
            { "TemplateProps", {
                { "LifeTime", 0.45f },
                { "ColorBegin", { 1.0f, 0.65f, 0.2f, 0.9f } },
                { "ColorEnd", { 0.85f, 0.15f, 0.05f, 0.0f } },
                { "SizeBegin", 0.25f },
                { "SizeEnd", 0.05f },
                { "Velocity", { 0.0f, 0.85f, 0.0f } },
                { "VelocityVariation", { 0.2f, 0.3f, 0.2f } }
            } }
        } },
        { "PointLight", {
            { "Color", { 1.0f, 0.48f, 0.12f } },
            { "Intensity", 1.6f },
            { "Radius", 2.4f },
            { "Falloff", 1.2f },
            { "Flicker", true },
            { "FlickerSpeed", 14.0f },
            { "FlickerAmount", 0.30f },
            { "Semantic", static_cast<int>(LightSemantic::SafeLifeline) }
        } },
        { "CircleCollider2D", {
            { "Radius", 0.35f },
            { "IsSensor", true },
            { "CategoryBits", static_cast<uint64_t>(PhysicsLayer::EnemyHitbox) },
            { "MaskBits", static_cast<uint64_t>(PhysicsLayer::Player | PhysicsLayer::Enemy) }
        } }
    };

    nlohmann::json entityJson = {
        { "UUID", 1 },
        { "Name", "TarFirewall" },
        { "Tag", "Untagged" },
        { "LayerMask", PhysicsLayer::Default },
        { "ParentUUID", 0 },
        { "Components", std::move(compsJson) }
    };

    return std::make_shared<Prefab>(std::move(entityJson));
}

} // namespace Engine
