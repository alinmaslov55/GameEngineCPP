#ifndef PHYSICS_COLLISION2DINFO_H
#define PHYSICS_COLLISION2DINFO_H

#include <glm/glm.hpp>

namespace Engine {

class GameObject;

struct Collision2DInfo {
    GameObject* Other = nullptr;
    glm::vec2 ContactPointXZ{ 0.0f, 0.0f };
    glm::vec2 NormalXZ{ 0.0f, 0.0f };       // Points from this entity -> Other
    float ApproachSpeed = 0.0f;             // Relative approach velocity in m/s (from b2ContactHitEvent)
    float EffectiveImpulse = 0.0f;          // Reduced mass * ApproachSpeed for collateral damage
};

} // namespace Engine

#endif // PHYSICS_COLLISION2DINFO_H
