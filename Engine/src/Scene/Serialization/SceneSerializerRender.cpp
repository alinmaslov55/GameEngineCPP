#include "JsonHelpers.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>

namespace Engine::Serialization {

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
    spriteJson["DissolveAmount"] = spriteRenderer->GetDissolveAmount();
    spriteJson["DissolveEdgeWidth"] = spriteRenderer->GetDissolveEdgeWidth();
    spriteJson["FlashAmount"] = spriteRenderer->GetFlashAmount();
    spriteJson["FlashColorMode"] = spriteRenderer->GetFlashColorMode();
    spriteJson["RenderSilhouette"] = spriteRenderer->GetRenderSilhouette();
    spriteJson["SilhouetteColor"] = ToJson(spriteRenderer->GetSilhouetteColor());
    spriteJson["FlipX"] = spriteRenderer->GetFlipX();
    spriteJson["FlipY"] = spriteRenderer->GetFlipY();

    if (auto tex = spriteRenderer->GetTexture()) {
        spriteJson["TexturePath"] = ResourceManager::ToRelativePath(tex->GetFilepath());
    }
    if (auto subTex = spriteRenderer->GetSubTexture()) {
        spriteJson["SubTexture"] = SerializeSubTexture(subTex);
    }
    compsJson["SpriteRenderer"] = std::move(spriteJson);
}


void SerializeParticleSystem(const GameObject* go, json& compsJson) {
    auto* particleSystem = go->GetComponent<ParticleSystem>();
    if (!particleSystem) return;

    const auto& tp = particleSystem->TemplateProps;
    const auto& fb = particleSystem->Flipbook;
    json psJson = {
        {"PoolSize", particleSystem->GetParticles().size()},
        {"AutoEmit", particleSystem->AutoEmit},
        {"EmitRate", particleSystem->EmitRate},
        {"SpawnOffset", ToJson(particleSystem->SpawnOffset)},
        {"EmissiveParticles", particleSystem->EmissiveParticles},
        {"Orientation", static_cast<int>(particleSystem->Orientation)},
        {"GravityY", particleSystem->GravityY},
        {"LinearDrag", particleSystem->LinearDrag},
        {"CollideGroundY", particleSystem->CollideGroundY},
        {"GroundPlaneY", particleSystem->GroundPlaneY},
        {"GroundBounceRestitution", particleSystem->GroundBounceRestitution},
        {"Flipbook", {
            {"Enabled", fb.Enabled},
            {"Columns", fb.Columns},
            {"Rows", fb.Rows},
            {"TotalFrames", fb.TotalFrames},
            {"CyclesOverLifetime", fb.CyclesOverLifetime},
            {"RandomStartFrame", fb.RandomStartFrame}
        }},
        {"TemplateProps", {
            {"Velocity", ToJson(tp.velocity)},
            {"VelocityVariation", ToJson(tp.velocityVariation)},
            {"ColorBegin", ToJson(tp.colorBegin)},
            {"ColorEnd", ToJson(tp.colorEnd)},
            {"SizeBegin", tp.sizeBegin},
            {"SizeEnd", tp.sizeEnd},
            {"SizeVariation", tp.sizeVariation},
            {"LifeTime", tp.lifeTime},
            {"Rotation", tp.rotation},
            {"RandomRotation", tp.randomRotation}
        }}
    };

    if (particleSystem->ParticleTexture && !particleSystem->ParticleTexture->GetFilepath().empty()) {
        psJson["TexturePath"] = ResourceManager::ToRelativePath(particleSystem->ParticleTexture->GetFilepath());
    }

    compsJson["ParticleSystem"] = std::move(psJson);
}


void SerializeAnimator(const GameObject* go, json& compsJson) {
    auto* animator = go->GetComponent<Animator>();
    if (!animator) return;

    json animJson;
    animJson["CurrentClip"] = animator->GetCurrentClipName();
    animJson["DirectionalScheme"] = static_cast<int>(animator->GetDirectionalScheme());
    animJson["PlaybackSpeed"] = animator->GetPlaybackSpeed();
    animJson["HysteresisDegrees"] = animator->GetHysteresisDegrees();
    animJson["CurrentFlipX"] = animator->GetCurrentFlipX();
    animJson["CurrentFacingDirection"] = static_cast<int>(animator->GetCurrentFacingDirection());
    json clipsJson = json::array();

    for (const auto& [clipName, clipPtr] : animator->GetClips()) {
        if (!clipPtr) continue;
        json framesJson = json::array();
        if (!clipPtr->Frames.empty()) {
            for (const auto& af : clipPtr->Frames) {
                json fJson = SerializeSubTexture(af.SubTex);
                if (!fJson.is_null()) {
                    fJson["DurationSeconds"] = af.DurationSeconds;
                    framesJson.push_back(std::move(fJson));
                }
            }
        } else {
            for (const auto& frame : clipPtr->frames) {
                json fJson = SerializeSubTexture(frame);
                if (!fJson.is_null()) {
                    framesJson.push_back(std::move(fJson));
                }
            }
        }
        json cEntry = {
            {"Name", clipPtr->name},
            {"FrameDuration", clipPtr->frameDuration},
            {"Loop", clipPtr->loop},
            {"Frames", std::move(framesJson)}
        };
        if (!clipPtr->Events.empty()) {
            json eventsArr = json::array();
            for (const auto& ev : clipPtr->Events) {
                eventsArr.push_back({
                    {"FrameIndex", ev.FrameIndex},
                    {"EventID", ev.EventID},
                    {"SocketOffsetX", ev.SocketOffset.x},
                    {"SocketOffsetY", ev.SocketOffset.y}
                });
            }
            cEntry["Events"] = std::move(eventsArr);
        }
        clipsJson.push_back(std::move(cEntry));
    }
    animJson["Clips"] = std::move(clipsJson);
    compsJson["Animator"] = std::move(animJson);
}

void DeserializeSpriteRenderer(GameObject* go, const json& srJson, [[maybe_unused]] const std::string& name) {
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
    sprite->SetDissolveAmount(srJson.value("DissolveAmount", 0.0f));
    sprite->SetDissolveEdgeWidth(srJson.value("DissolveEdgeWidth", 0.08f));
    sprite->SetFlashAmount(srJson.value("FlashAmount", 0.0f));
    sprite->SetFlashColorMode(srJson.value("FlashColorMode", 0.0f));
    sprite->SetRenderSilhouette(srJson.value("RenderSilhouette", false));
    sprite->SetFlipX(srJson.value("FlipX", false));
    sprite->SetFlipY(srJson.value("FlipY", false));
    if (srJson.contains("SilhouetteColor")) {
        sprite->SetSilhouetteColor(AsVec4(srJson["SilhouetteColor"], glm::vec4(0.2f, 0.4f, 0.9f, 0.28f)));
    }
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


void DeserializeParticleSystem(GameObject* go, const json& psJson, const std::string&) {
    uint32_t poolSize = psJson.value("PoolSize", 1000u);
    auto* ps = go->AddComponent<ParticleSystem>(poolSize);
    ps->AutoEmit = psJson.value("AutoEmit", false);
    ps->EmitRate = psJson.value("EmitRate", 8.0f);
    if (psJson.contains("SpawnOffset")) {
        ps->SpawnOffset = AsVec3(psJson["SpawnOffset"], ps->SpawnOffset);
    }
    ps->EmissiveParticles = psJson.value("EmissiveParticles", false);

    const int rawOrientation = psJson.value("Orientation", static_cast<int>(ParticleOrientation::CameraBillboard));
    ps->Orientation = static_cast<ParticleOrientation>(std::clamp(rawOrientation, 0, 2));
    ps->GravityY = psJson.value("GravityY", 0.0f);
    ps->LinearDrag = std::max(0.0f, psJson.value("LinearDrag", 0.0f));
    ps->CollideGroundY = psJson.value("CollideGroundY", false);
    ps->GroundPlaneY = psJson.value("GroundPlaneY", 0.02f);
    ps->GroundBounceRestitution = std::clamp(psJson.value("GroundBounceRestitution", 0.4f), 0.0f, 1.0f);

    if (psJson.contains("TexturePath") && psJson["TexturePath"].is_string()) {
        const std::string texPath = psJson["TexturePath"].get<std::string>();
        if (!texPath.empty()) {
            ps->ParticleTexture = ResourceManager::Load<Texture>(texPath);
        }
    }

    if (psJson.contains("Flipbook") && psJson["Flipbook"].is_object()) {
        const auto& fbJson = psJson["Flipbook"];
        ps->Flipbook.Enabled = fbJson.value("Enabled", false);
        ps->Flipbook.Columns = std::max(1u, fbJson.value("Columns", 1u));
        ps->Flipbook.Rows = std::max(1u, fbJson.value("Rows", 1u));
        const uint32_t maxGrid = ps->Flipbook.Columns * ps->Flipbook.Rows;
        ps->Flipbook.TotalFrames = std::clamp(fbJson.value("TotalFrames", 1u), 1u, maxGrid);
        ps->Flipbook.CyclesOverLifetime = std::max(0.0f, fbJson.value("CyclesOverLifetime", 1.0f));
        ps->Flipbook.RandomStartFrame = fbJson.value("RandomStartFrame", false);
    }

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
        tp.rotation = tpJson.value("Rotation", tp.rotation);
        tp.randomRotation = tpJson.value("RandomRotation", tp.randomRotation);
    }
}


void DeserializeAnimator(GameObject* go, const json& animJson, const std::string&) {
    auto* animator = go->AddComponent<Animator>();
    animator->SetDirectionalScheme(static_cast<DirectionalScheme>(animJson.value("DirectionalScheme", 2)));
    animator->SetPlaybackSpeed(animJson.value("PlaybackSpeed", 1.0f));
    animator->SetHysteresisDegrees(animJson.value("HysteresisDegrees", 7.0f));
    animator->SetCurrentFlipX(animJson.value("CurrentFlipX", false));
    animator->SetCurrentFacingDirection(static_cast<FacingDirection>(animJson.value("CurrentFacingDirection", 0)));

    if (animJson.contains("Clips") && animJson["Clips"].is_array()) {
        for (const auto& clipJson : animJson["Clips"]) {
            std::string clipName = clipJson.value("Name", "");
            float duration = clipJson.value("FrameDuration", 0.1f);
            bool loop = clipJson.value("Loop", true);
            auto clip = std::make_shared<AnimationClip>(clipName, duration, loop);

            if (clipJson.contains("Frames") && clipJson["Frames"].is_array()) {
                for (const auto& frameJson : clipJson["Frames"]) {
                    if (auto frameSubTex = DeserializeSubTexture(frameJson)) {
                        float frameDur = frameJson.value("DurationSeconds", -1.0f);
                        clip->AddFrame(frameSubTex, frameDur);
                    }
                }
            }

            if (clipJson.contains("Events") && clipJson["Events"].is_array()) {
                for (const auto& evJson : clipJson["Events"]) {
                    AnimationEvent ev;
                    ev.FrameIndex = evJson.value("FrameIndex", 0u);
                    ev.EventID = evJson.value("EventID", "");
                    ev.SocketOffset.x = evJson.value("SocketOffsetX", 0.0f);
                    ev.SocketOffset.y = evJson.value("SocketOffsetY", 0.0f);
                    clip->AddEvent(ev);
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

} // namespace Engine::Serialization
