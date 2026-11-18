#include "JsonHelpers.h"
#include "Engine/Audio/AudioSource.h"
#include "Engine/Core/ResourceManager.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Renderer/Texture.h"
#include "Engine/Scene/Components/CameraComponent.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Components/PostProcessComponent.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>

namespace Engine::Serialization {

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
        {"PortraitHorizontalFOV", sceneCam.GetPortraitHorizontalFOVDegrees()},
        {"FOVMode", static_cast<int>(sceneCam.GetFOVAdaptationMode())},
        {"Distance", sceneCam.GetDistance()},
        {"Pitch", sceneCam.GetPitchDegrees()},
        {"Yaw", sceneCam.GetYawDegrees()},
        {"FocalPoint", ToJson(focal)}
    };
}

void DeserializeSceneCamera(const json& camJson, PerspectiveCamera& sceneCam) {
    sceneCam.SetFOVDegrees(camJson.value("FOV", 32.0f));
    sceneCam.SetPortraitHorizontalFOVDegrees(camJson.value("PortraitHorizontalFOV", 22.0f));
    const int rawMode = camJson.value("FOVMode", static_cast<int>(FOVAdaptationMode::LandscapeVert_PortraitHorz));
    sceneCam.SetFOVAdaptationMode(rawMode == static_cast<int>(FOVAdaptationMode::FixedVertical)
                                      ? FOVAdaptationMode::FixedVertical
                                      : FOVAdaptationMode::LandscapeVert_PortraitHorz);
    sceneCam.SetDistance(camJson.value("Distance", 15.0f));
    sceneCam.SetPitchDegrees(camJson.value("Pitch", 40.0f));
    sceneCam.SetYawDegrees(camJson.value("Yaw", 0.0f));
    if (camJson.contains("FocalPoint")) {
        sceneCam.SetFocalPoint(AsVec3(camJson["FocalPoint"], sceneCam.GetFocalPoint()));
    }
    ENGINE_CORE_DEBUG("SceneSerializer: Restored Camera (FOV={:.1f}, hFOV={:.1f}, Mode={}, Dist={:.1f}, Pitch={:.1f}, Yaw={:.1f})",
                      sceneCam.GetFOVDegrees(), sceneCam.GetPortraitHorizontalFOVDegrees(),
                      static_cast<int>(sceneCam.GetFOVAdaptationMode()),
                      sceneCam.GetDistance(), sceneCam.GetPitchDegrees(), sceneCam.GetYawDegrees());
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
        {"Priority", static_cast<int>(pointLight->Priority)},
        {"Semantic", static_cast<int>(pointLight->Semantic)},
        {"CastShadows", pointLight->CastShadows},
        {"ShadowSoftness", pointLight->ShadowSoftness},
        {"ShadowBias", pointLight->ShadowBias},
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


void SerializeCameraComponent(const GameObject* go, json& compsJson) {
    auto* cameraComp = go->GetComponent<CameraComponent>();
    if (!cameraComp) return;

    compsJson["CameraComponent"] = {
        {"FOV", cameraComp->FOV},
        {"PortraitHorizontalFOV", cameraComp->PortraitHorizontalFOV},
        {"FOVMode", static_cast<int>(cameraComp->FOVMode)},
        {"Distance", cameraComp->Distance},
        {"Pitch", cameraComp->Pitch},
        {"Yaw", cameraComp->Yaw},
        {"FixedAspectRatio", cameraComp->FixedAspectRatio},
        {"SmoothFollow", cameraComp->SmoothFollow},
        {"FollowSmoothTime", cameraComp->FollowSmoothTime},
        {"AimLookAheadDistance", cameraComp->AimLookAheadDistance},
        {"AimLookAheadSmoothTime", cameraComp->AimLookAheadSmoothTime},
        {"DynamicZoomOutMax", cameraComp->DynamicZoomOutMax},
        {"DynamicZoomSmoothTime", cameraComp->DynamicZoomSmoothTime},
        {"TraumaDecay", cameraComp->TraumaDecay},
        {"MaxShakeAmplitude", cameraComp->MaxShakeAmplitude},
        {"ShakeFrequency", cameraComp->ShakeFrequency},
        {"KickDecayRate", cameraComp->KickDecayRate}
    };
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


void DeserializeTransform(GameObject* go, const json& tJson, const std::string&) {
    auto* transform = go->GetTransform();
    if (!transform) return;
    if (tJson.contains("Position")) transform->position = AsVec3(tJson["Position"], transform->position);
    if (tJson.contains("Rotation")) transform->rotation = AsVec3(tJson["Rotation"], transform->rotation);
    if (tJson.contains("Scale"))    transform->scale    = AsVec3(tJson["Scale"], transform->scale);
    transform->SnapshotPrevious();
    transform->SetDirty();
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
    const int rawPrio = std::clamp(plJson.value("Priority", static_cast<int>(LightPriority::Normal_World)),
                                   static_cast<int>(LightPriority::Low_VFX),
                                   static_cast<int>(LightPriority::Critical_HeroTorch));
    pointLight->Priority = static_cast<LightPriority>(rawPrio);
    const int rawSemantic = std::clamp(plJson.value("Semantic", static_cast<int>(LightSemantic::NeutralShrine)),
                                       static_cast<int>(LightSemantic::SafeLifeline),
                                       static_cast<int>(LightSemantic::DeceptiveHazard));
    pointLight->Semantic = static_cast<LightSemantic>(rawSemantic);
    pointLight->CastShadows = plJson.value("CastShadows", false);
    pointLight->ShadowSoftness = plJson.value("ShadowSoftness", 1.6f);
    pointLight->ShadowBias = plJson.value("ShadowBias", 0.0025f);
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


void DeserializeCameraComponent(GameObject* go, const json& camJson, const std::string&) {
    auto* camComp = go->AddComponent<CameraComponent>();
    camComp->FOV = camJson.value("FOV", 32.0f);
    camComp->PortraitHorizontalFOV = camJson.value("PortraitHorizontalFOV", 22.0f);
    const int rawMode = camJson.value("FOVMode", static_cast<int>(FOVAdaptationMode::LandscapeVert_PortraitHorz));
    camComp->FOVMode = (rawMode == static_cast<int>(FOVAdaptationMode::FixedVertical))
                           ? FOVAdaptationMode::FixedVertical
                           : FOVAdaptationMode::LandscapeVert_PortraitHorz;
    camComp->Distance = camJson.value("Distance", 14.0f);
    camComp->Pitch = camJson.value("Pitch", 40.0f);
    camComp->Yaw = camJson.value("Yaw", 0.0f);
    camComp->FixedAspectRatio = camJson.value("FixedAspectRatio", false);
    camComp->SmoothFollow = camJson.value("SmoothFollow", true);
    camComp->FollowSmoothTime = camJson.value("FollowSmoothTime", 0.11f);
    camComp->AimLookAheadDistance = camJson.value("AimLookAheadDistance", 1.15f);
    camComp->AimLookAheadSmoothTime = camJson.value("AimLookAheadSmoothTime", 0.18f);
    camComp->DynamicZoomOutMax = camJson.value("DynamicZoomOutMax", 1.8f);
    camComp->DynamicZoomSmoothTime = camJson.value("DynamicZoomSmoothTime", 0.25f);
    camComp->TraumaDecay = camJson.value("TraumaDecay", 1.4f);
    camComp->MaxShakeAmplitude = camJson.value("MaxShakeAmplitude", 0.65f);
    camComp->ShakeFrequency = camJson.value("ShakeFrequency", 28.0f);
    camComp->KickDecayRate = camJson.value("KickDecayRate", 8.0f);
    camComp->SetViewportSize(1280, 720);
    camComp->SyncFromTransform(go->GetTransform());
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


} // namespace Engine::Serialization
