#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"

#include <box2d/box2d.h>
#include <box2d/collision.h>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace Engine {

namespace {

struct OverlapContext {
    std::unordered_set<GameObject*> uniqueHits;
    std::vector<GameObject*>* outHits = nullptr;
};

bool Box2DOverlapCallback(b2ShapeId shapeId, void* context) {
    auto* ctx = static_cast<OverlapContext*>(context);
    if (!b2Shape_IsValid(shapeId)) return true;
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    if (!b2Body_IsValid(bodyId)) return true;
    void* userData = b2Body_GetUserData(bodyId);
    if (userData) {
        auto* go = static_cast<GameObject*>(userData);
        if (go && !go->IsPendingDestroy() && go->IsActiveInHierarchy()) {
            if (ctx->uniqueHits.insert(go).second) {
                ctx->outHits->push_back(go);
            }
        }
    }
    return true; // continue collecting overlaps
}

} // namespace

void PhysicsQueries::OverlapCircleXZ(const Scene& scene, glm::vec2 centerXZ, float radius,
                                    uint64_t maskBits, std::vector<GameObject*>& outHits) {
    outHits.clear();
    if (radius <= 0.0f) return;

    if (scene.IsRuntimeRunning()) {
        b2WorldId worldId = b2LoadWorldId(scene.GetPhysicsWorldId());
        if (b2World_IsValid(worldId)) {
            b2Vec2 center{ centerXZ.x, centerXZ.y };
            b2ShapeProxy proxy = b2MakeProxy(&center, 1, radius);
            b2QueryFilter filter = b2DefaultQueryFilter();
            filter.categoryBits = PhysicsLayer::All;
            filter.maskBits = maskBits;

            OverlapContext ctx;
            ctx.outHits = &outHits;
            b2World_OverlapShape(worldId, &proxy, filter, Box2DOverlapCallback, &ctx);
            return;
        }
    }

    // Fallback for non-runtime or editor queries
    for (const auto& go : scene.GetGameObjects()) {
        if (!go || !go->IsActiveInHierarchy()) continue;
        auto* t = go->GetTransform();
        if (!t) continue;
        glm::vec2 posXZ{ t->position.x, t->position.z };

        bool overlaps = false;
        if (auto* cc = go->GetComponent<CircleCollider2D>()) {
            if ((cc->CategoryBits & maskBits) != 0) {
                float dist = glm::distance(centerXZ, posXZ + cc->OffsetXZ);
                if (dist <= (radius + cc->Radius)) overlaps = true;
            }
        }
        if (auto* bc = go->GetComponent<BoxCollider2D>()) {
            if ((bc->CategoryBits & maskBits) != 0) {
                glm::vec2 bMin = posXZ + bc->offset - bc->size * 0.5f;
                glm::vec2 bMax = posXZ + bc->offset + bc->size * 0.5f;
                glm::vec2 closest = glm::clamp(centerXZ, bMin, bMax);
                if (glm::distance(centerXZ, closest) <= radius) overlaps = true;
            }
        }
        if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
            if ((cap->CategoryBits & maskBits) != 0) {
                glm::vec2 a = posXZ + cap->Center1;
                glm::vec2 b = posXZ + cap->Center2;
                glm::vec2 ab = b - a;
                float abLenSq = glm::dot(ab, ab);
                float tParam = (abLenSq > 1e-6f) ? glm::clamp(glm::dot(centerXZ - a, ab) / abLenSq, 0.0f, 1.0f) : 0.0f;
                glm::vec2 closest = a + tParam * ab;
                if (glm::distance(centerXZ, closest) <= (radius + cap->Radius)) overlaps = true;
            }
        }
        if (overlaps) {
            outHits.push_back(go.get());
        }
    }
}

void PhysicsQueries::OverlapArcXZ(const Scene& scene, glm::vec2 originXZ, glm::vec2 forwardXZ,
                                 float radius, float halfAngleDegrees, uint64_t maskBits,
                                 std::vector<GameObject*>& outHits) {
    outHits.clear();
    if (radius <= 0.0f) return;

    std::vector<GameObject*> candidates;
    OverlapCircleXZ(scene, originXZ, radius, maskBits, candidates);
    if (candidates.empty()) return;

    const float lenSq = glm::dot(forwardXZ, forwardXZ);
    if (lenSq < 1e-6f || halfAngleDegrees >= 180.0f) {
        outHits = std::move(candidates);
        return;
    }

    const glm::vec2 fwd = forwardXZ / std::sqrt(lenSq);
    const float cosThreshold = std::cos(glm::radians(halfAngleDegrees));

    for (auto* go : candidates) {
        if (!go) continue;
        auto* t = go->GetTransform();
        if (!t) continue;

        glm::vec2 targetXZ{ t->position.x, t->position.z };
        glm::vec2 toTarget = targetXZ - originXZ;
        float distSq = glm::dot(toTarget, toTarget);

        if (distSq < 1e-6f) {
            // Target is coincident with query origin
            outHits.push_back(go);
            continue;
        }

        glm::vec2 dir = toTarget / std::sqrt(distSq);
        float dot = glm::dot(fwd, dir);
        if (dot >= (cosThreshold - 1e-5f)) {
            outHits.push_back(go);
        }
    }
}

bool PhysicsQueries::RaycastClosestXZ(const Scene& scene, glm::vec2 startXZ, glm::vec2 endXZ,
                                      uint64_t maskBits, RaycastHitXZ& outHit) {
    outHit = RaycastHitXZ{};
    glm::vec2 diff = endXZ - startXZ;
    float maxDist = glm::length(diff);
    if (maxDist < 1e-5f) return false;

    if (scene.IsRuntimeRunning()) {
        b2WorldId worldId = b2LoadWorldId(scene.GetPhysicsWorldId());
        if (b2World_IsValid(worldId)) {
            b2Vec2 origin{ startXZ.x, startXZ.y };
            b2Vec2 translation{ diff.x, diff.y };
            b2QueryFilter filter = b2DefaultQueryFilter();
            filter.categoryBits = PhysicsLayer::All;
            filter.maskBits = maskBits;

            b2RayResult res = b2World_CastRayClosest(worldId, origin, translation, filter);
            if (res.hit && b2Shape_IsValid(res.shapeId)) {
                b2BodyId bodyId = b2Shape_GetBody(res.shapeId);
                if (b2Body_IsValid(bodyId)) {
                    void* userData = b2Body_GetUserData(bodyId);
                    if (userData) {
                        auto* go = static_cast<GameObject*>(userData);
                        if (go && !go->IsPendingDestroy() && go->IsActiveInHierarchy()) {
                            outHit.Entity = go;
                            outHit.PointXZ = { res.point.x, res.point.y };
                            outHit.NormalXZ = { res.normal.x, res.normal.y };
                            outHit.Fraction = res.fraction;
                            return true;
                        }
                    }
                }
            }
            return false;
        }
    }

    // Fallback for non-runtime or editor queries
    float closestFraction = 1.0f;
    bool foundHit = false;

    for (const auto& go : scene.GetGameObjects()) {
        if (!go || !go->IsActiveInHierarchy()) continue;
        auto* t = go->GetTransform();
        if (!t) continue;
        glm::vec2 posXZ{ t->position.x, t->position.z };

        if (auto* cc = go->GetComponent<CircleCollider2D>()) {
            if ((cc->CategoryBits & maskBits) != 0) {
                glm::vec2 center = posXZ + cc->OffsetXZ;
                glm::vec2 d = diff;
                glm::vec2 f = startXZ - center;
                float a = glm::dot(d, d);
                float b = 2.0f * glm::dot(f, d);
                float c = glm::dot(f, f) - cc->Radius * cc->Radius;
                float discriminant = b * b - 4.0f * a * c;
                if (discriminant >= 0.0f) {
                    discriminant = std::sqrt(discriminant);
                    float t1 = (-b - discriminant) / (2.0f * a);
                    if (t1 >= 0.0f && t1 < closestFraction) {
                        closestFraction = t1;
                        outHit.Entity = go.get();
                        outHit.Fraction = t1;
                        outHit.PointXZ = startXZ + t1 * diff;
                        outHit.NormalXZ = glm::normalize(outHit.PointXZ - center);
                        foundHit = true;
                    }
                }
            }
        }
        if (auto* bc = go->GetComponent<BoxCollider2D>()) {
            if ((bc->CategoryBits & maskBits) != 0) {
                glm::vec2 bMin = posXZ + bc->offset - bc->size * 0.5f;
                glm::vec2 bMax = posXZ + bc->offset + bc->size * 0.5f;

                float tMin = 0.0f;
                float tMax = closestFraction;
                glm::vec2 hitNormal{ 0.0f };

                bool hitBox = true;
                for (int axis = 0; axis < 2; ++axis) {
                    if (std::abs(diff[axis]) < 1e-6f) {
                        if (startXZ[axis] < bMin[axis] || startXZ[axis] > bMax[axis]) {
                            hitBox = false;
                            break;
                        }
                    } else {
                        float ood = 1.0f / diff[axis];
                        float t1 = (bMin[axis] - startXZ[axis]) * ood;
                        float t2 = (bMax[axis] - startXZ[axis]) * ood;
                        glm::vec2 n1{ 0.0f }, n2{ 0.0f };
                        n1[axis] = -1.0f;
                        n2[axis] = 1.0f;

                        if (t1 > t2) {
                            std::swap(t1, t2);
                            std::swap(n1, n2);
                        }
                        if (t1 > tMin) {
                            tMin = t1;
                            hitNormal = n1;
                        }
                        tMax = std::min(tMax, t2);
                        if (tMin > tMax) {
                            hitBox = false;
                            break;
                        }
                    }
                }
                if (hitBox && tMin >= 0.0f && tMin < closestFraction) {
                    closestFraction = tMin;
                    outHit.Entity = go.get();
                    outHit.Fraction = tMin;
                    outHit.PointXZ = startXZ + tMin * diff;
                    outHit.NormalXZ = hitNormal;
                    foundHit = true;
                }
            }
        }
    }
    return foundHit;
}

} // namespace Engine
