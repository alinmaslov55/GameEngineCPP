#include "Panels/Inspector/InspectorPanel.h"

#include <Engine/Physics/PhysicsComponents.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <functional>

namespace Sandbox {

namespace {

void DrawCollisionFilterUI(uint64_t& categoryBits, uint64_t& maskBits, int32_t& groupIndex,
                          const std::function<void(uint64_t, uint64_t, int32_t)>& onChange) {
    const char* layerNames[] = {
        "Default", "Environment", "LowObstacle", "Player", "Enemy",
        "Companion", "PlayerHitbox", "EnemyHitbox", "Projectile", "Pickup"
    };
    const uint64_t layerBits[] = {
        Engine::PhysicsLayer::Default,
        Engine::PhysicsLayer::Environment,
        Engine::PhysicsLayer::LowObstacle,
        Engine::PhysicsLayer::Player,
        Engine::PhysicsLayer::Enemy,
        Engine::PhysicsLayer::Companion,
        Engine::PhysicsLayer::PlayerHitbox,
        Engine::PhysicsLayer::EnemyHitbox,
        Engine::PhysicsLayer::Projectile,
        Engine::PhysicsLayer::Pickup
    };

    int currentIdx = 0; // Default
    for (int i = 0; i < 10; ++i) {
        if (categoryBits == layerBits[i]) {
            currentIdx = i;
            break;
        }
    }

    if (ImGui::BeginCombo("Category Layer", layerNames[currentIdx])) {
        for (int i = 0; i < 10; ++i) {
            const bool isSelected = (currentIdx == i);
            if (ImGui::Selectable(layerNames[i], isSelected)) {
                categoryBits = layerBits[i];
                if (onChange) onChange(categoryBits, maskBits, groupIndex);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::TreeNode("Collision Mask Layers")) {
        for (int i = 0; i < 10; ++i) {
            bool enabled = (maskBits & layerBits[i]) != 0;
            if (ImGui::Checkbox(layerNames[i], &enabled)) {
                if (enabled) maskBits |= layerBits[i];
                else maskBits &= ~layerBits[i];
                if (onChange) onChange(categoryBits, maskBits, groupIndex);
            }
        }
        ImGui::TreePop();
    }
}

} // namespace

void InspectorPanel::DrawRigidbody2D(Engine::Rigidbody2D& rb2d, Engine::GameObject* entity) {
    const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
    const int bodyTypeIdx = std::clamp(static_cast<int>(rb2d.Type), 0, IM_ARRAYSIZE(bodyTypeStrings) - 1);
    const char* currentBodyTypeString = bodyTypeStrings[bodyTypeIdx];

    if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
        for (int i = 0; i < 3; ++i) {
            const bool isSelected = (currentBodyTypeString == bodyTypeStrings[i]);
            if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                rb2d.Type = static_cast<Engine::Rigidbody2D::BodyType>(i);
                if (entity && entity->GetScene()) entity->GetScene()->MarkPhysicsDirty(entity);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Fixed Rotation (Lock Yaw)", &rb2d.FixedRotation)) {
        if (entity && entity->GetScene()) entity->GetScene()->MarkPhysicsDirty(entity);
    }
    ImGui::Checkbox("Use Terrain Friction", &rb2d.UseTerrainFriction);
    if (ImGui::DragFloat("Linear Damping", &rb2d.LinearDamping, 0.1f, 0.0f, 50.0f, "%.2f")) {
        rb2d.SetLinearDampingRuntime(rb2d.LinearDamping);
    }
    if (ImGui::DragFloat("Angular Damping", &rb2d.AngularDamping, 0.1f, 0.0f, 50.0f, "%.2f")) {
        rb2d.SetAngularDampingRuntime(rb2d.AngularDamping);
    }
    if (ImGui::DragFloat("Mass Multiplier", &rb2d.MassMultiplier, 0.05f, 0.01f, 100.0f, "%.2f")) {
        rb2d.SetMassMultiplier(rb2d.MassMultiplier);
    }
    if (ImGui::Checkbox("Continuous Collision (Bullet CCD)", &rb2d.IsBullet)) {
        rb2d.SetBullet(rb2d.IsBullet);
    }
    ImGui::TextDisabled("Effective Mass: %.2f kg", rb2d.GetMass());
    glm::vec2 vel = rb2d.GetVelocityXZ();
    ImGui::TextDisabled("Velocity XZ: (%.2f, %.2f) m/s", vel.x, vel.y);

    ImGui::Spacing();
    ImGui::Text("2.5D Vertical Elevation (Y Axis)");
    ImGui::Checkbox("Use Gravity Y", &rb2d.UseGravityY);
    if (rb2d.UseGravityY) {
        ImGui::DragFloat("Gravity Y", &rb2d.GravityY, 0.2f, -100.0f, 0.0f);
        ImGui::DragFloat("Gravity Scale Y", &rb2d.GravityScaleY, 0.05f, 0.0f, 10.0f);
    }
    ImGui::DragFloat("Ground Level Y", &rb2d.GroundLevelY, 0.05f);
    ImGui::SliderFloat("Restitution Y", &rb2d.GroundRestitutionY, 0.0f, 1.0f);
    ImGui::TextDisabled("Vertical Vel: %.2f m/s (%s)", rb2d.VerticalVelocity, rb2d.IsAirborne() ? "Airborne" : "Grounded");

    ImGui::Spacing();
    ImGui::Text("Airborne Pass-Through (PHY-04)");
    ImGui::Checkbox("Enable Airborne Pass-Through", &rb2d.EnableAirbornePassThrough);
    if (rb2d.EnableAirbornePassThrough) {
        ImGui::DragFloat("Clearance Height", &rb2d.AirborneClearanceHeight, 0.05f, 0.0f, 10.0f, "%.2f m");
        ImGui::TextDisabled("Vault Status: %s", rb2d.IsAirbornePassThroughActive() ? "ACTIVE (Mid-Air Vault)" : "INACTIVE (Ground)");
    }

    ImGui::Spacing();
    ImGui::Text("Chronos Stasis (PHY-05)");
    bool stasis = rb2d.IsStasisFrozen();
    if (ImGui::Checkbox("Stasis Frozen", &stasis)) {
        rb2d.SetStasisFrozen(stasis);
    }
    if (rb2d.IsStasisFrozen()) {
        const glm::vec2& queued = rb2d.GetQueuedStasisImpulseXZ();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "STASIS ACTIVE");
        ImGui::TextDisabled("Queued Impulse: (%.2f, %.2f) N*s", queued.x, queued.y);
    }
}

void InspectorPanel::DrawBoxCollider2D(Engine::BoxCollider2D& boxCollider, Engine::GameObject* entity) {
    bool dirty = false;
    dirty |= ImGui::DragFloat2("Size (Width X, Depth Z)", &boxCollider.size.x, 0.05f, 0.01f, 100.0f);
    dirty |= ImGui::DragFloat2("Offset (X, Z)", &boxCollider.offset.x, 0.05f);
    dirty |= ImGui::DragFloat("Density", &boxCollider.Density, 0.01f, 0.0f, 100.0f);
    dirty |= ImGui::DragFloat("Friction", &boxCollider.Friction, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::DragFloat("Restitution (XZ)", &boxCollider.Restitution, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::Checkbox("Is Sensor", &boxCollider.IsSensor);
    dirty |= ImGui::Checkbox("Enable Hit Events", &boxCollider.EnableHitEvents);
    if (dirty && entity && entity->GetScene()) {
        entity->GetScene()->MarkPhysicsDirty(entity);
    }
    DrawCollisionFilterUI(boxCollider.CategoryBits, boxCollider.MaskBits, boxCollider.GroupIndex,
        [&boxCollider](uint64_t c, uint64_t m, int32_t g) { boxCollider.SetFilter(c, m, g); });
}

void InspectorPanel::DrawCircleCollider2D(Engine::CircleCollider2D& circleCollider, Engine::GameObject* entity) {
    bool dirty = false;
    dirty |= ImGui::DragFloat("Radius", &circleCollider.Radius, 0.05f, 0.01f, 50.0f);
    dirty |= ImGui::DragFloat2("Offset (X, Z)", &circleCollider.OffsetXZ.x, 0.05f);
    dirty |= ImGui::DragFloat("Density", &circleCollider.Density, 0.01f, 0.0f, 100.0f);
    dirty |= ImGui::DragFloat("Friction", &circleCollider.Friction, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::DragFloat("Restitution", &circleCollider.Restitution, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::Checkbox("Is Sensor", &circleCollider.IsSensor);
    dirty |= ImGui::Checkbox("Enable Hit Events", &circleCollider.EnableHitEvents);
    if (dirty && entity && entity->GetScene()) {
        entity->GetScene()->MarkPhysicsDirty(entity);
    }
    DrawCollisionFilterUI(circleCollider.CategoryBits, circleCollider.MaskBits, circleCollider.GroupIndex,
        [&circleCollider](uint64_t c, uint64_t m, int32_t g) { circleCollider.SetFilter(c, m, g); });
}

void InspectorPanel::DrawCapsuleCollider2D(Engine::CapsuleCollider2D& capsuleCollider, Engine::GameObject* entity) {
    bool dirty = false;
    dirty |= ImGui::DragFloat2("Center 1 (X, Z)", &capsuleCollider.Center1.x, 0.05f);
    dirty |= ImGui::DragFloat2("Center 2 (X, Z)", &capsuleCollider.Center2.x, 0.05f);
    dirty |= ImGui::DragFloat("Radius", &capsuleCollider.Radius, 0.05f, 0.01f, 50.0f);
    dirty |= ImGui::DragFloat("Density", &capsuleCollider.Density, 0.01f, 0.0f, 100.0f);
    dirty |= ImGui::DragFloat("Friction", &capsuleCollider.Friction, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::DragFloat("Restitution", &capsuleCollider.Restitution, 0.01f, 0.0f, 1.0f);
    dirty |= ImGui::Checkbox("Is Sensor", &capsuleCollider.IsSensor);
    dirty |= ImGui::Checkbox("Enable Hit Events", &capsuleCollider.EnableHitEvents);
    if (dirty && entity && entity->GetScene()) {
        entity->GetScene()->MarkPhysicsDirty(entity);
    }
    DrawCollisionFilterUI(capsuleCollider.CategoryBits, capsuleCollider.MaskBits, capsuleCollider.GroupIndex,
        [&capsuleCollider](uint64_t c, uint64_t m, int32_t g) { capsuleCollider.SetFilter(c, m, g); });
}


} // namespace Sandbox
