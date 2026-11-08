#ifndef PHYSICS_PHYSICSQUERIES_H
#define PHYSICS_PHYSICSQUERIES_H

#include "Engine/Physics/PhysicsLayer.h"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

class Scene;
class GameObject;

struct RaycastHitXZ {
    GameObject* Entity = nullptr;
    glm::vec2 PointXZ{ 0.0f, 0.0f };
    glm::vec2 NormalXZ{ 0.0f, 0.0f };
    float Fraction = 1.0f;
};

class PhysicsQueries {
public:
    static void OverlapCircleXZ(const Scene& scene, glm::vec2 centerXZ, float radius,
                                uint64_t maskBits, std::vector<GameObject*>& outHits);

    // Sector/cone filter on top of OverlapCircleXZ for Maul / Halberd / Sickle directional swings:
    static void OverlapArcXZ(const Scene& scene, glm::vec2 originXZ, glm::vec2 forwardXZ,
                             float radius, float halfAngleDegrees, uint64_t maskBits,
                             std::vector<GameObject*>& outHits);

    static bool RaycastClosestXZ(const Scene& scene, glm::vec2 startXZ, glm::vec2 endXZ,
                                 uint64_t maskBits, RaycastHitXZ& outHit);
};

} // namespace Engine

#endif // PHYSICS_PHYSICSQUERIES_H
