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
        {"Enabled", rb2d->IsEnabled()},
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

namespace {
json SerializeSingleBoxCollider2D(const BoxCollider2D* collider) {
    return {
        {"Enabled", collider->IsEnabled()},
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

json SerializeSingleCircleCollider2D(const CircleCollider2D* collider) {
    return {
        {"Enabled", collider->IsEnabled()},
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

json SerializeSingleCapsuleCollider2D(const CapsuleCollider2D* collider) {
    return {
        {"Enabled", collider->IsEnabled()},
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
} // namespace

void SerializeBoxCollider2D(const GameObject* go, json& compsJson) {
    auto colliders = go->GetComponents<BoxCollider2D>();
    if (colliders.empty()) return;

    if (colliders.size() == 1) {
        compsJson["BoxCollider2D"] = SerializeSingleBoxCollider2D(colliders[0]);
    } else {
        json arr = json::array();
        for (const auto* c : colliders) {
            arr.push_back(SerializeSingleBoxCollider2D(c));
        }
        compsJson["BoxCollider2D"] = std::move(arr);
    }
}

void SerializeCircleCollider2D(const GameObject* go, json& compsJson) {
    auto colliders = go->GetComponents<CircleCollider2D>();
    if (colliders.empty()) return;

    if (colliders.size() == 1) {
        compsJson["CircleCollider2D"] = SerializeSingleCircleCollider2D(colliders[0]);
    } else {
        json arr = json::array();
        for (const auto* c : colliders) {
            arr.push_back(SerializeSingleCircleCollider2D(c));
        }
        compsJson["CircleCollider2D"] = std::move(arr);
    }
}

void SerializeCapsuleCollider2D(const GameObject* go, json& compsJson) {
    auto colliders = go->GetComponents<CapsuleCollider2D>();
    if (colliders.empty()) return;

    if (colliders.size() == 1) {
        compsJson["CapsuleCollider2D"] = SerializeSingleCapsuleCollider2D(colliders[0]);
    } else {
        json arr = json::array();
        for (const auto* c : colliders) {
            arr.push_back(SerializeSingleCapsuleCollider2D(c));
        }
        compsJson["CapsuleCollider2D"] = std::move(arr);
    }
}


void DeserializeRigidbody2D(GameObject* go, const json& rbJson, const std::string&) {
    if (!rbJson.is_object()) return;
    auto* rb2d = go->AddComponent<Rigidbody2D>();
    rb2d->SetEnabled(rbJson.value("Enabled", true));
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

namespace {
void DeserializeSingleBoxCollider2D(GameObject* go, const json& bcJson) {
    if (!bcJson.is_object()) return;
    auto* collider = go->AddComponent<BoxCollider2D>();
    collider->SetEnabled(bcJson.value("Enabled", true));
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

void DeserializeSingleCircleCollider2D(GameObject* go, const json& ccJson) {
    if (!ccJson.is_object()) return;
    auto* collider = go->AddComponent<CircleCollider2D>();
    collider->SetEnabled(ccJson.value("Enabled", true));
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

void DeserializeSingleCapsuleCollider2D(GameObject* go, const json& capJson) {
    if (!capJson.is_object()) return;
    auto* collider = go->AddComponent<CapsuleCollider2D>();
    collider->SetEnabled(capJson.value("Enabled", true));
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
} // namespace

void DeserializeBoxCollider2D(GameObject* go, const json& bcJson, const std::string&) {
    if (bcJson.is_array()) {
        for (const auto& entry : bcJson) {
            DeserializeSingleBoxCollider2D(go, entry);
        }
    } else {
        DeserializeSingleBoxCollider2D(go, bcJson);
    }
}

void DeserializeCircleCollider2D(GameObject* go, const json& ccJson, const std::string&) {
    if (ccJson.is_array()) {
        for (const auto& entry : ccJson) {
            DeserializeSingleCircleCollider2D(go, entry);
        }
    } else {
        DeserializeSingleCircleCollider2D(go, ccJson);
    }
}

void DeserializeCapsuleCollider2D(GameObject* go, const json& capJson, const std::string&) {
    if (capJson.is_array()) {
        for (const auto& entry : capJson) {
            DeserializeSingleCapsuleCollider2D(go, entry);
        }
    } else {
        DeserializeSingleCapsuleCollider2D(go, capJson);
    }
}


} // namespace Engine::Serialization
