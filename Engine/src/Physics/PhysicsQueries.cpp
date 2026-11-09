#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Tilemap.h"
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
        const glm::vec3 worldPos = t->GetWorldPosition();
        const glm::vec3 worldScale = t->GetWorldScale();
        const glm::vec2 posXZ{ worldPos.x, worldPos.z };
        const glm::vec2 scaleXZ{ std::abs(worldScale.x), std::abs(worldScale.z) };

        bool overlaps = false;
        if (auto* cc = go->GetComponent<CircleCollider2D>()) {
            if ((cc->CategoryBits & maskBits) != 0) {
                float effectiveRadius = cc->Radius * std::max(scaleXZ.x, scaleXZ.y);
                float dist = glm::distance(centerXZ, posXZ + cc->OffsetXZ * scaleXZ);
                if (dist <= (radius + effectiveRadius)) overlaps = true;
            }
        }
        if (auto* bc = go->GetComponent<BoxCollider2D>()) {
            if ((bc->CategoryBits & maskBits) != 0) {
                glm::vec2 bMin = posXZ + bc->offset * scaleXZ - bc->size * scaleXZ * 0.5f;
                glm::vec2 bMax = posXZ + bc->offset * scaleXZ + bc->size * scaleXZ * 0.5f;
                glm::vec2 closest = glm::clamp(centerXZ, bMin, bMax);
                if (glm::distance(centerXZ, closest) <= radius) overlaps = true;
            }
        }
        if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
            if ((cap->CategoryBits & maskBits) != 0) {
                glm::vec2 a = posXZ + cap->Center1 * scaleXZ;
                glm::vec2 b = posXZ + cap->Center2 * scaleXZ;
                float effectiveRadius = cap->Radius * std::max(scaleXZ.x, scaleXZ.y);
                glm::vec2 ab = b - a;
                float abLenSq = glm::dot(ab, ab);
                float tParam = (abLenSq > 1e-6f) ? glm::clamp(glm::dot(centerXZ - a, ab) / abLenSq, 0.0f, 1.0f) : 0.0f;
                glm::vec2 closest = a + tParam * ab;
                if (glm::distance(centerXZ, closest) <= (radius + effectiveRadius)) overlaps = true;
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

        const glm::vec3 worldPos = t->GetWorldPosition();
        glm::vec2 targetXZ{ worldPos.x, worldPos.z };
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
        const glm::vec3 worldPos = t->GetWorldPosition();
        const glm::vec3 worldScale = t->GetWorldScale();
        const glm::vec2 posXZ{ worldPos.x, worldPos.z };
        const glm::vec2 scaleXZ{ std::abs(worldScale.x), std::abs(worldScale.z) };

        // 1. CircleCollider2D
        if (auto* cc = go->GetComponent<CircleCollider2D>()) {
            if ((cc->CategoryBits & maskBits) != 0) {
                const float effectiveRadius = cc->Radius * std::max(scaleXZ.x, scaleXZ.y);
                const glm::vec2 center = posXZ + cc->OffsetXZ * scaleXZ;
                const glm::vec2 d = diff;
                const glm::vec2 f = startXZ - center;
                const float c = glm::dot(f, f) - effectiveRadius * effectiveRadius;

                if (c <= 0.0f) {
                    // Containment: Ray starts inside circle
                    if (0.0f < closestFraction) {
                        closestFraction = 0.0f;
                        outHit.Entity = go.get();
                        outHit.Fraction = 0.0f;
                        outHit.PointXZ = startXZ;
                        const float dist = glm::length(f);
                        outHit.NormalXZ = (dist > 1e-6f) ? (-f / dist) : -glm::normalize(diff);
                        foundHit = true;
                    }
                } else {
                    const float a = glm::dot(d, d);
                    const float b = 2.0f * glm::dot(f, d);
                    float discriminant = b * b - 4.0f * a * c;
                    if (discriminant >= 0.0f) {
                        discriminant = std::sqrt(discriminant);
                        const float t1 = (-b - discriminant) / (2.0f * a);
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
        }

        // 2. BoxCollider2D
        if (auto* bc = go->GetComponent<BoxCollider2D>()) {
            if ((bc->CategoryBits & maskBits) != 0) {
                const glm::vec2 bMin = posXZ + bc->offset * scaleXZ - bc->size * scaleXZ * 0.5f;
                const glm::vec2 bMax = posXZ + bc->offset * scaleXZ + bc->size * scaleXZ * 0.5f;

                if (startXZ.x >= bMin.x && startXZ.x <= bMax.x &&
                    startXZ.y >= bMin.y && startXZ.y <= bMax.y) {
                    // Containment: Ray starts inside box
                    if (0.0f < closestFraction) {
                        closestFraction = 0.0f;
                        outHit.Entity = go.get();
                        outHit.Fraction = 0.0f;
                        outHit.PointXZ = startXZ;
                        outHit.NormalXZ = -glm::normalize(diff);
                        foundHit = true;
                    }
                } else {
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
                            const float ood = 1.0f / diff[axis];
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

        // 3. CapsuleCollider2D
        if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
            if ((cap->CategoryBits & maskBits) != 0) {
                const glm::vec2 p1 = posXZ + cap->Center1 * scaleXZ;
                const glm::vec2 p2 = posXZ + cap->Center2 * scaleXZ;
                const float r = cap->Radius * std::max(scaleXZ.x, scaleXZ.y);
                const glm::vec2 seg = p2 - p1;
                const float segLenSq = glm::dot(seg, seg);

                // Check containment: distance from startXZ to segment <= r
                const float tSeg = (segLenSq > 1e-6f) ? glm::clamp(glm::dot(startXZ - p1, seg) / segLenSq, 0.0f, 1.0f) : 0.0f;
                const glm::vec2 closestOnSeg = p1 + tSeg * seg;
                if (glm::distance(startXZ, closestOnSeg) <= r) {
                    if (0.0f < closestFraction) {
                        closestFraction = 0.0f;
                        outHit.Entity = go.get();
                        outHit.Fraction = 0.0f;
                        outHit.PointXZ = startXZ;
                        outHit.NormalXZ = -glm::normalize(diff);
                        foundHit = true;
                    }
                } else {
                    float bestCapT = closestFraction;
                    glm::vec2 bestCapNormal{ 0.0f };
                    bool hitCap = false;

                    // A. Check oriented rectangular body if segment length > 0
                    if (segLenSq > 1e-6f) {
                        const float L = std::sqrt(segLenSq);
                        const glm::vec2 U = seg / L;
                        const glm::vec2 V{ -U.y, U.x };

                        // Transform ray start and diff to capsule local space (u along seg, v perp)
                        const glm::vec2 lStart{ glm::dot(startXZ - p1, U), glm::dot(startXZ - p1, V) };
                        const glm::vec2 lDiff{ glm::dot(diff, U), glm::dot(diff, V) };

                        // Box bounds: u in [0, L], v in [-r, +r]
                        float tMin = 0.0f;
                        float tMax = bestCapT;
                        glm::vec2 lHitNormal{ 0.0f };
                        bool hitBox = true;

                        const glm::vec2 boxMin{ 0.0f, -r };
                        const glm::vec2 boxMax{ L, r };

                        for (int axis = 0; axis < 2; ++axis) {
                            if (std::abs(lDiff[axis]) < 1e-6f) {
                                if (lStart[axis] < boxMin[axis] || lStart[axis] > boxMax[axis]) {
                                    hitBox = false;
                                    break;
                                }
                            } else {
                                const float ood = 1.0f / lDiff[axis];
                                float t1 = (boxMin[axis] - lStart[axis]) * ood;
                                float t2 = (boxMax[axis] - lStart[axis]) * ood;
                                glm::vec2 n1{ 0.0f }, n2{ 0.0f };
                                n1[axis] = -1.0f;
                                n2[axis] = 1.0f;

                                if (t1 > t2) {
                                    std::swap(t1, t2);
                                    std::swap(n1, n2);
                                }
                                if (t1 > tMin) {
                                    tMin = t1;
                                    lHitNormal = n1;
                                }
                                tMax = std::min(tMax, t2);
                                if (tMin > tMax) {
                                    hitBox = false;
                                    break;
                                }
                            }
                        }
                        if (hitBox && tMin >= 0.0f && tMin < bestCapT) {
                            bestCapT = tMin;
                            bestCapNormal = lHitNormal.x * U + lHitNormal.y * V;
                            hitCap = true;
                        }
                    }

                    // B. Check cap circle 1 (at p1)
                    auto testCapCircle = [&](const glm::vec2& center) {
                        const glm::vec2 f = startXZ - center;
                        const float a = glm::dot(diff, diff);
                        const float b = 2.0f * glm::dot(f, diff);
                        const float c = glm::dot(f, f) - r * r;
                        float disc = b * b - 4.0f * a * c;
                        if (disc >= 0.0f) {
                            disc = std::sqrt(disc);
                            const float t1 = (-b - disc) / (2.0f * a);
                            if (t1 >= 0.0f && t1 < bestCapT) {
                                bestCapT = t1;
                                bestCapNormal = glm::normalize((startXZ + t1 * diff) - center);
                                hitCap = true;
                            }
                        }
                    };

                    testCapCircle(p1);
                    testCapCircle(p2);

                    if (hitCap && bestCapT < closestFraction) {
                        closestFraction = bestCapT;
                        outHit.Entity = go.get();
                        outHit.Fraction = bestCapT;
                        outHit.PointXZ = startXZ + bestCapT * diff;
                        outHit.NormalXZ = bestCapNormal;
                        foundHit = true;
                    }
                }
            }
        }

        // 4. Tilemap
        if (auto* tm = go->GetComponent<Tilemap>()) {
            if ((PhysicsLayer::Environment & maskBits) != 0) {
                const uint32_t width = tm->GetWidth();
                const uint32_t height = tm->GetHeight();
                if (width > 0 && height > 0) {
                    const glm::vec2 tmMin = posXZ - glm::vec2(0.5f * scaleXZ.x, 0.5f * scaleXZ.y);
                    const glm::vec2 tmMax = posXZ + glm::vec2((static_cast<float>(width) - 0.5f) * scaleXZ.x,
                                                              (static_cast<float>(height) - 0.5f) * scaleXZ.y);

                    float tEnter = 0.0f;
                    float tExit = closestFraction;
                    bool hitsTmBounds = true;
                    for (int axis = 0; axis < 2; ++axis) {
                        if (std::abs(diff[axis]) < 1e-6f) {
                            if (startXZ[axis] < tmMin[axis] || startXZ[axis] > tmMax[axis]) {
                                hitsTmBounds = false;
                                break;
                            }
                        } else {
                            const float ood = 1.0f / diff[axis];
                            float t1 = (tmMin[axis] - startXZ[axis]) * ood;
                            float t2 = (tmMax[axis] - startXZ[axis]) * ood;
                            if (t1 > t2) std::swap(t1, t2);
                            tEnter = std::max(tEnter, t1);
                            tExit = std::min(tExit, t2);
                            if (tEnter > tExit) {
                                hitsTmBounds = false;
                                break;
                            }
                        }
                    }

                    if (hitsTmBounds) {
                        for (uint32_t gz = 0; gz < height; ++gz) {
                            for (uint32_t gx = 0; gx < width; ++gx) {
                                if (tm->GetTile(gx, gz).walkable) continue;

                                const float centerX = posXZ.x + static_cast<float>(gx) * scaleXZ.x;
                                const float centerZ = posXZ.y + static_cast<float>(gz) * scaleXZ.y;
                                const float halfW = 0.5f * scaleXZ.x;
                                const float halfD = 0.5f * scaleXZ.y;

                                const glm::vec2 bMin{ centerX - halfW, centerZ - halfD };
                                const glm::vec2 bMax{ centerX + halfW, centerZ + halfD };

                                if (startXZ.x >= bMin.x && startXZ.x <= bMax.x &&
                                    startXZ.y >= bMin.y && startXZ.y <= bMax.y) {
                                    if (0.0f < closestFraction) {
                                        closestFraction = 0.0f;
                                        outHit.Entity = go.get();
                                        outHit.Fraction = 0.0f;
                                        outHit.PointXZ = startXZ;
                                        outHit.NormalXZ = -glm::normalize(diff);
                                        foundHit = true;
                                    }
                                } else {
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
                                            const float ood = 1.0f / diff[axis];
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
                    }
                }
            }
        }
    }
    return foundHit;
}

} // namespace Engine
