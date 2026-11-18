#ifndef PHYSICS_COLLIDER2D_H
#define PHYSICS_COLLIDER2D_H

#include "Engine/Scene/Component.h"
#include "Engine/Physics/Collision2DInfo.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Physics/PhysicsHandles.h"

namespace Engine {

class Transform;

/// @brief Abstract base class for all 2D physics colliders.
/// Implements Template Method Pattern for shape creation and unified filter/lifecycle management.
class Collider2D : public Component {
public:
    using ComponentBaseType = Collider2D;

    Collider2D() = default;
    Collider2D(bool isSensor, uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex)
        : IsSensor(isSensor), CategoryBits(categoryBits), MaskBits(maskBits), GroupIndex(groupIndex) {}
    ~Collider2D() override = default;

    // Physical material properties for Box2D
    float Density = 1.0f;
    float Friction = 0.5f;
    float Restitution = 0.0f;          // Bounciness on the XZ plane
    float RestitutionThreshold = 0.5f; // Velocity required to bounce
    bool IsSensor = false;             // Trigger volume without physical contact response
    bool EnableHitEvents = true;       // Enables b2ContactHitEvent generation for impact damage

    // 64-bit Collision Filtering
    uint64_t CategoryBits = PhysicsLayer::Default;
    uint64_t MaskBits = PhysicsLayer::All;
    int32_t GroupIndex = 0;

    void SetFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex = 0);
    void SetCategoryBits(uint64_t categoryBits);
    void SetMaskBits(uint64_t maskBits);
    void SetGroupIndex(int32_t groupIndex);

    // Type-safe Box2D runtime shape handle
    ShapeHandle RuntimeShape;

    // Type-safe Box2D runtime body handle (attached dynamic Rigidbody2D or standalone static body)
    BodyHandle RuntimeBody;

    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnPoolAcquire() override;
    void OnPoolRelease() override;

    // Template Method: Skeleton algorithm for shape creation and geometry/material synchronization
    void CreateOrUpdateShape(BodyHandle body, const Transform& transform, GameObject* go);

    // Template Method Hook: Pure virtual geometry construction implemented by derived colliders
    virtual void BuildShapeDefAndCreate(BodyHandle body, const Transform& transform, GameObject* go) = 0;
};

} // namespace Engine

#endif // PHYSICS_COLLIDER2D_H
