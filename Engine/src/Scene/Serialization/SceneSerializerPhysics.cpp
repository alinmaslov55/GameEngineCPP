#include "JsonHelpers.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>

namespace Engine::Serialization {

void SerializeRigidbody2D(const GameObject* go, json& compsJson) {
    auto* rb2d = go->GetComponent<Rigidbody2D>();
    if (!rb2d) return;

    glm::vec2 linearVel = rb2d->InitialLinearVelocityXZ;
    float verticalVel = rb2d->VerticalVelocity;
    if (rb2d->IsStasisFrozen()) {
        linearVel = rb2d->GetStasisCachedVelocityXZ();
        verticalVel = rb2d->GetStasisCachedVerticalVelocity();
    } else if (rb2d->RuntimeBody.IsValid()) {
        linearVel = rb2d->GetVelocityXZ();
    }

    compsJson["Rigidbody2D"] = {
        {"BodyType", static_cast<int>(rb2d->Type)},
        {"FixedRotation", rb2d->FixedRotation},
        {"LinearDamping", rb2d->LinearDamping},
        {"UseTerrainFriction", rb2d->UseTerrainFriction},
        {"AngularDamping", rb2d->AngularDamping},
        {"GravityScaleY", rb2d->GravityScaleY},
        {"MassMultiplier", rb2d->MassMultiplier},
        {"IsBullet", rb2d->IsBullet},
        {"UseGravityY", rb2d->UseGravityY},
        {"GravityY", rb2d->GravityY},
        {"GroundLevelY", rb2d->GroundLevelY},
        {"GroundRestitutionY", rb2d->GroundRestitutionY},
        {"AirborneClearanceHeight", rb2d->AirborneClearanceHeight},
        {"EnableAirbornePassThrough", rb2d->EnableAirbornePassThrough},
        {"AirbornePassThroughMask", rb2d->AirbornePassThroughMask},
        {"InitialLinearVelocityXZ", ToJson(linearVel)},
        {"VerticalVelocity", verticalVel},
        {"IsStasisFrozen", rb2d->IsStasisFrozen()}
    };
}

void SerializeBoxCollider2D(const GameObject* go, json& compsJson) {
    auto* collider = go->GetComponent<BoxCollider2D>();
    if (!collider) return;

    compsJson["BoxCollider2D"] = {
        {"Size", ToJson(collider->size)},
        {"Offset", ToJson(collider->offset)},
        {"Density", collider->Density},
        {"Friction", collider->Friction},
        {"Restitution", collider->Restitution},
        {"RestitutionThreshold", collider->RestitutionThreshold},
        {"IsSensor", collider->IsSensor},
        {"EnableHitEvents", collider->EnableHitEvents},
        {"CategoryBits", collider->CategoryBits},
        {"MaskBits", collider->MaskBits},
        {"GroupIndex", collider->GroupIndex}
    };
}

void SerializeCircleCollider2D(const GameObject* go, json& compsJson) {
    auto* collider = go->GetComponent<CircleCollider2D>();
    if (!collider) return;

    compsJson["CircleCollider2D"] = {
        {"Radius", collider->Radius},
        {"OffsetXZ", ToJson(collider->OffsetXZ)},
        {"Density", collider->Density},
        {"Friction", collider->Friction},
        {"Restitution", collider->Restitution},
        {"IsSensor", collider->IsSensor},
        {"EnableHitEvents", collider->EnableHitEvents},
        {"CategoryBits", collider->CategoryBits},
        {"MaskBits", collider->MaskBits},
        {"GroupIndex", collider->GroupIndex}
    };
}

void SerializeCapsuleCollider2D(const GameObject* go, json& compsJson) {
    auto* collider = go->GetComponent<CapsuleCollider2D>();
    if (!collider) return;

    compsJson["CapsuleCollider2D"] = {
        {"Center1", ToJson(collider->Center1)},
        {"Center2", ToJson(collider->Center2)},
        {"Radius", collider->Radius},
        {"Density", collider->Density},
        {"Friction", collider->Friction},
        {"Restitution", collider->Restitution},
        {"IsSensor", collider->IsSensor},
        {"EnableHitEvents", collider->EnableHitEvents},
        {"CategoryBits", collider->CategoryBits},
        {"MaskBits", collider->MaskBits},
        {"GroupIndex", collider->GroupIndex}
    };
}


void DeserializeRigidbody2D(GameObject* go, const json& rbJson, const std::string&) {
    auto* rb2d = go->AddComponent<Rigidbody2D>();
    rb2d->Type = static_cast<Rigidbody2D::BodyType>(rbJson.value("BodyType", 1));
    rb2d->FixedRotation = rbJson.value("FixedRotation", true);
    rb2d->LinearDamping = rbJson.value("LinearDamping", 4.5f);
    rb2d->UseTerrainFriction = rbJson.value("UseTerrainFriction", false);
    rb2d->AngularDamping = rbJson.value("AngularDamping", 6.0f);
    rb2d->GravityScaleY = rbJson.value("GravityScaleY", 1.0f);
    rb2d->MassMultiplier = rbJson.value("MassMultiplier", 1.0f);
    rb2d->IsBullet = rbJson.value("IsBullet", false);
    rb2d->UseGravityY = rbJson.value("UseGravityY", true);
    rb2d->GravityY = rbJson.value("GravityY", -22.0f);
    rb2d->GroundLevelY = rbJson.value("GroundLevelY", 0.0f);
    rb2d->GroundRestitutionY = rbJson.value("GroundRestitutionY", 0.0f);
    rb2d->AirborneClearanceHeight = rbJson.value("AirborneClearanceHeight", 0.75f);
    rb2d->EnableAirbornePassThrough = rbJson.value("EnableAirbornePassThrough", true);
    rb2d->AirbornePassThroughMask = rbJson.value("AirbornePassThroughMask", PhysicsLayer::DefaultAirborneExcluded);
    if (rbJson.contains("InitialLinearVelocityXZ")) {
        rb2d->InitialLinearVelocityXZ = AsVec2(rbJson["InitialLinearVelocityXZ"], rb2d->InitialLinearVelocityXZ);
    }
    rb2d->VerticalVelocity = rbJson.value("VerticalVelocity", 0.0f);
    if (rbJson.value("IsStasisFrozen", false)) {
        rb2d->SetStasisFrozen(true);
    }
}

void DeserializeBoxCollider2D(GameObject* go, const json& bcJson, const std::string&) {
    auto* collider = go->AddComponent<BoxCollider2D>();
    if (bcJson.contains("Size"))   collider->size = AsVec2(bcJson["Size"], collider->size);
    if (bcJson.contains("Offset")) collider->offset = AsVec2(bcJson["Offset"], collider->offset);
    collider->Density = bcJson.value("Density", 1.0f);
    collider->Friction = bcJson.value("Friction", 0.5f);
    collider->Restitution = bcJson.value("Restitution", 0.0f);
    collider->RestitutionThreshold = bcJson.value("RestitutionThreshold", 0.5f);
    collider->IsSensor = bcJson.value("IsSensor", false);
    collider->EnableHitEvents = bcJson.value("EnableHitEvents", true);
    collider->CategoryBits = bcJson.value("CategoryBits", PhysicsLayer::Default);
    collider->MaskBits = bcJson.value("MaskBits", PhysicsLayer::All);
    collider->GroupIndex = bcJson.value("GroupIndex", 0);
}

void DeserializeCircleCollider2D(GameObject* go, const json& ccJson, const std::string&) {
    auto* collider = go->AddComponent<CircleCollider2D>();
    collider->Radius = ccJson.value("Radius", 0.45f);
    if (ccJson.contains("OffsetXZ")) collider->OffsetXZ = AsVec2(ccJson["OffsetXZ"], collider->OffsetXZ);
    collider->Density = ccJson.value("Density", 1.0f);
    collider->Friction = ccJson.value("Friction", 0.5f);
    collider->Restitution = ccJson.value("Restitution", 0.15f);
    collider->IsSensor = ccJson.value("IsSensor", false);
    collider->EnableHitEvents = ccJson.value("EnableHitEvents", true);
    collider->CategoryBits = ccJson.value("CategoryBits", PhysicsLayer::Default);
    collider->MaskBits = ccJson.value("MaskBits", PhysicsLayer::All);
    collider->GroupIndex = ccJson.value("GroupIndex", 0);
}

void DeserializeCapsuleCollider2D(GameObject* go, const json& capJson, const std::string&) {
    auto* collider = go->AddComponent<CapsuleCollider2D>();
    if (capJson.contains("Center1")) collider->Center1 = AsVec2(capJson["Center1"], collider->Center1);
    if (capJson.contains("Center2")) collider->Center2 = AsVec2(capJson["Center2"], collider->Center2);
    collider->Radius = capJson.value("Radius", 0.35f);
    collider->Density = capJson.value("Density", 1.0f);
    collider->Friction = capJson.value("Friction", 0.5f);
    collider->Restitution = capJson.value("Restitution", 0.15f);
    collider->IsSensor = capJson.value("IsSensor", false);
    collider->EnableHitEvents = capJson.value("EnableHitEvents", true);
    collider->CategoryBits = capJson.value("CategoryBits", PhysicsLayer::Default);
    collider->MaskBits = capJson.value("MaskBits", PhysicsLayer::All);
    collider->GroupIndex = capJson.value("GroupIndex", 0);
}


} // namespace Engine::Serialization
