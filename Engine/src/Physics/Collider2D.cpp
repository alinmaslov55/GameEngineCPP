#include "Engine/Physics/Collider2D.h"
#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Utils/Logger.h"
#include "PhysicsInternalHelpers.h"

#include <cmath>

namespace Engine {

using namespace PhysicsDetail;

void Collider2D::OnEnable() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnEnable");
    auto* go = GetGameObject();
    auto* scene = go ? go->GetScene() : nullptr;

    if (RuntimeBody.IsValid()) {
        b2BodyId bodyId = ToB2(RuntimeBody);
        if (b2Body_IsValid(bodyId)) {
            if (!RuntimeShape.IsValid() && go) {
                if (auto* transform = go->GetTransform()) {
                    CreateOrUpdateShape(RuntimeBody, *transform, go);
                    if (auto* rb2d = go->GetComponent<Rigidbody2D>();
                        rb2d && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                        rb2d->RefreshMassFromShapes();
                    }
                    if (scene) {
                        scene->InvalidateSpatialGrid();
                    }
                }
            }
            if (go) {
                for (auto* sibling : go->GetComponents<Collider2D>()) {
                    if (sibling != this && !sibling->IsEnabled() && sibling->RuntimeShape.IsValid()) {
                        b2ShapeId s = ToB2(sibling->RuntimeShape);
                        if (b2Shape_IsValid(s)) {
                            b2DestroyShape(s, true);
                        }
                        sibling->RuntimeShape.Reset();
                    }
                }
                auto* rb2d = go->GetComponent<Rigidbody2D>();
                if (go->IsActiveInHierarchy() && (!rb2d || rb2d->IsEnabled())) {
                    if (!b2Body_IsEnabled(bodyId)) {
                        b2Body_Enable(bodyId);
                    }
                }
            }
            return;
        }
        RuntimeBody.Reset();
        RuntimeShape.Reset();
    }

    if (!RuntimeShape.IsValid() && go && scene && scene->IsRuntimeRunning()) {
        scene->MarkPhysicsDirty(go);
    }
}

void Collider2D::OnDisable() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnDisable");
    auto* go = GetGameObject();
    auto* scene = go ? go->GetScene() : nullptr;

    // Box2D v3 has no b2Shape_Disable(): when this Collider2D itself is disabled or removed
    // (as opposed to whole-GO SetActive(false) where IsEnabled() remains true for pooling),
    // destroy only this Collider2D's RuntimeShape so sibling colliders and shared Rigidbody2D remain active.
    if (!IsEnabled() && RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2DestroyShape(shapeId, true);
        }
        RuntimeShape.Reset();

        if (go) {
            if (auto* rb2d = go->GetComponent<Rigidbody2D>();
                rb2d && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                rb2d->RefreshMassFromShapes();
            }
            if (scene) {
                scene->InvalidateSpatialGrid();
            }
        }
    }

    if (RuntimeBody.IsValid()) {
        bool keepBodyEnabled = false;
        if (go && go->IsActiveInHierarchy() && !go->IsPendingDestroy()) {
            if (const auto* rb2d = go->GetComponent<Rigidbody2D>();
                rb2d && rb2d->IsEnabled() && !rb2d->IsPendingRemoval() && rb2d->RuntimeBody == RuntimeBody) {
                keepBodyEnabled = true;
            }
            if (!keepBodyEnabled) {
                for (const auto* other : go->GetComponents<Collider2D>()) {
                    if (other != this && other->IsEnabled() && !other->IsPendingRemoval() &&
                        other->RuntimeBody == RuntimeBody) {
                        keepBodyEnabled = true;
                        break;
                    }
                }
            }
        }
        if (!keepBodyEnabled) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId) && b2Body_IsEnabled(bodyId)) {
                b2Body_Disable(bodyId);
            }
        }
    }
}

void Collider2D::OnDestroy() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnDestroy");
    if (auto* go = GetGameObject()) {
        if (auto* scene = go->GetScene()) {
            scene->GetPhysicsWorld().UnregisterCollider(go, this);
            if (go->IsAlive()) {
                scene->InvalidateSpatialGrid();
            }
            return;
        }
    }
    RuntimeShape.Reset();
    RuntimeBody.Reset();
}

void Collider2D::OnPoolAcquire() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnPoolAcquire");
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
    if (RuntimeBody.IsValid()) {
        auto* go = GetGameObject();
        if (go && !go->GetComponent<Rigidbody2D>()) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId)) {
                b2Body_Enable(bodyId);
                if (auto* transform = go->GetTransform()) {
                    glm::vec3 worldPos = transform->GetWorldPosition();
                    float angleRad = -glm::radians(transform->GetWorldRotation().y);
                    b2Body_SetTransform(bodyId, b2Vec2{ worldPos.x, worldPos.z }, b2MakeRot(angleRad));
                }
            }
        }
    }
}

void Collider2D::OnPoolRelease() {
    AssertPhysicsOwnerThread(this, "Collider2D::OnPoolRelease");
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = MaskBits;
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
    if (RuntimeBody.IsValid()) {
        auto* go = GetGameObject();
        if (go && !go->GetComponent<Rigidbody2D>()) {
            b2BodyId bodyId = ToB2(RuntimeBody);
            if (b2Body_IsValid(bodyId)) {
                b2Body_SetAwake(bodyId, false);
                b2Body_Disable(bodyId);
            }
        }
    }
}

void Collider2D::SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex) {
    AssertPhysicsOwnerThread(this, "Collider2D::SetFilter");
    CategoryBits = categoryBits;
    MaskBits = maskBits;
    GroupIndex = groupIndex;
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            b2Filter f;
            f.categoryBits = CategoryBits;
            f.maskBits = ResolveEffectiveMaskBits(GetGameObject(), MaskBits);
            f.groupIndex = GroupIndex;
            b2Shape_SetFilter(shapeId, f);
        }
    }
}

void Collider2D::SetCategoryBits(uint64_t categoryBits) {
    SetFilter(categoryBits, MaskBits, GroupIndex);
}

void Collider2D::SetMaskBits(uint64_t maskBits) {
    SetFilter(CategoryBits, maskBits, GroupIndex);
}

void Collider2D::SetGroupIndex(int32_t groupIndex) {
    SetFilter(CategoryBits, MaskBits, groupIndex);
}

void Collider2D::CreateOrUpdateShape(BodyHandle body, const Transform& transform, GameObject* go) {
    AssertPhysicsOwnerThread(this, "Collider2D::CreateOrUpdateShape");
    if (!body.IsValid()) {
        ENGINE_CORE_WARN("Collider2D::CreateOrUpdateShape: Invalid BodyHandle on '{}'", go ? go->GetName() : "<unowned>");
        return;
    }
    b2BodyId bodyId = ToB2(body);
    if (!b2Body_IsValid(bodyId)) {
        ENGINE_CORE_WARN("Collider2D::CreateOrUpdateShape: Invalid b2BodyId on '{}'", go ? go->GetName() : "<unowned>");
        return;
    }

    // Check if sensor state changed on existing shape
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            if (b2Shape_IsSensor(shapeId) != IsSensor) {
                ENGINE_CORE_DEBUG("Collider2D '{}': Recreating shape due to IsSensor toggle ({})",
                                  go ? go->GetName() : "<unowned>", IsSensor);
                b2DestroyShape(shapeId, true);
                RuntimeShape.Reset();
            }
        } else {
            RuntimeShape.Reset();
        }
    }

    // Call subclass hook to build or update shape geometry
    BuildShapeDefAndCreate(body, transform, go);

    // Synchronize material properties, density, filter, and hit events on shape
    if (RuntimeShape.IsValid()) {
        b2ShapeId shapeId = ToB2(RuntimeShape);
        if (b2Shape_IsValid(shapeId)) {
            const float safeDensity = (std::isfinite(Density) && Density >= 0.0f) ? Density : 0.0f;
            const float safeFriction = (std::isfinite(Friction) && Friction >= 0.0f) ? Friction : 0.0f;
            const float safeRestitution = (std::isfinite(Restitution) && Restitution >= 0.0f) ? Restitution : 0.0f;
            if (std::abs(b2Shape_GetDensity(shapeId) - safeDensity) > 1e-4f) {
                b2Shape_SetDensity(shapeId, safeDensity, true);
            }
            if (std::abs(b2Shape_GetFriction(shapeId) - safeFriction) > 1e-4f) {
                b2Shape_SetFriction(shapeId, safeFriction);
            }
            if (std::abs(b2Shape_GetRestitution(shapeId) - safeRestitution) > 1e-4f) {
                b2Shape_SetRestitution(shapeId, safeRestitution);
            }
            if (b2Shape_AreHitEventsEnabled(shapeId) != EnableHitEvents) {
                b2Shape_EnableHitEvents(shapeId, EnableHitEvents);
            }
            if (CategoryBits == PhysicsLayer::Default && go && go->GetLayer() != EntityLayer::Default) {
                CategoryBits = PhysicsLayer::FromEntityLayer(go->GetLayer());
            }
            b2Filter effectiveFilter;
            effectiveFilter.categoryBits = CategoryBits;
            effectiveFilter.maskBits = ResolveEffectiveMaskBits(go, MaskBits);
            effectiveFilter.groupIndex = GroupIndex;
            const b2Filter currentFilter = b2Shape_GetFilter(shapeId);
            if (currentFilter.categoryBits != effectiveFilter.categoryBits ||
                currentFilter.maskBits != effectiveFilter.maskBits ||
                currentFilter.groupIndex != effectiveFilter.groupIndex) {
                b2Shape_SetFilter(shapeId, effectiveFilter);
            }
        }
    }
}

} // namespace Engine
