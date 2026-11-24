#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/Tilemap.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Utils/Logger.h"

#include <box2d/box2d.h>
#include <box2d/collision.h>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace Engine {

namespace {

struct OverlapContext {
    std::vector<GameObject*>* outHits = nullptr;
    std::unordered_set<GameObject*>* overflowSet = nullptr;
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
            auto* hits = ctx->outHits;
            constexpr size_t kLinearDedupThreshold = 32;
            if (!ctx->overflowSet) {
                if (std::find(hits->begin(), hits->end(), go) != hits->end()) {
                    return true;
                }
                if (hits->size() >= kLinearDedupThreshold) {
                    thread_local std::unordered_set<GameObject*> t_OverflowSet;
                    t_OverflowSet.clear();
                    t_OverflowSet.insert(hits->begin(), hits->end());
                    ctx->overflowSet = &t_OverflowSet;
                    if (!ctx->overflowSet->insert(go).second) {
                        return true;
                    }
                }
                hits->push_back(go);
            } else if (ctx->overflowSet->insert(go).second) {
                hits->push_back(go);
            }
        }
    }
    return true; // continue collecting overlaps
}

struct RaycastContext {
    RaycastHitXZ* outHit = nullptr;
    float closestFraction = 1.0f;
    bool found = false;
};

float Box2DRaycastCallback(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
    if (!b2Shape_IsValid(shapeId) || b2Shape_IsSensor(shapeId)) {
        return -1.0f;
    }
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    if (!b2Body_IsValid(bodyId)) {
        return -1.0f;
    }
    auto* go = static_cast<GameObject*>(b2Body_GetUserData(bodyId));
    if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) {
        return -1.0f;
    }

    auto* ctx = static_cast<RaycastContext*>(context);
    if (fraction < ctx->closestFraction) {
        ctx->closestFraction = fraction;
        ctx->outHit->Entity = go;
        ctx->outHit->PointXZ = { point.x, point.y };
        ctx->outHit->NormalXZ = { normal.x, normal.y };
        ctx->outHit->Fraction = fraction;
        ctx->found = true;
    }
    return fraction;
}

struct RayOriginOverlapContext {
    b2Vec2 origin{ 0.0f, 0.0f };
    GameObject* hitEntity = nullptr;
};

bool Box2DRayOriginOverlapCallback(b2ShapeId shapeId, void* context) {
    if (!b2Shape_IsValid(shapeId) || b2Shape_IsSensor(shapeId)) {
        return true;
    }
    if (!b2Shape_TestPoint(shapeId, static_cast<RayOriginOverlapContext*>(context)->origin)) {
        return true;
    }
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    if (!b2Body_IsValid(bodyId)) {
        return true;
    }
    auto* go = static_cast<GameObject*>(b2Body_GetUserData(bodyId));
    if (go && !go->IsPendingDestroy() && go->IsActiveInHierarchy()) {
        static_cast<RayOriginOverlapContext*>(context)->hitEntity = go;
        return false; // stop at first containing solid shape
    }
    return true;
}

[[nodiscard]] inline glm::vec2 RotateXZ(glm::vec2 v, float cosY, float sinY) noexcept {
    return { v.x * cosY + v.y * sinY, -v.x * sinY + v.y * cosY };
}

[[nodiscard]] inline glm::vec2 InverseRotateXZ(glm::vec2 v, float cosY, float sinY) noexcept {
    return { v.x * cosY - v.y * sinY, v.x * sinY + v.y * cosY };
}

} // namespace

void PhysicsQueries::OverlapCircleXZ(const Scene& scene, glm::vec2 centerXZ, float radius,
                                    uint64_t maskBits, std::vector<GameObject*>& outHits) {
    outHits.clear();
    if (radius <= 0.0f) {
        ENGINE_CORE_WARN("PhysicsQueries::OverlapCircleXZ ignored non-positive radius {}", radius);
        return;
    }

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
            ENGINE_CORE_TRACE("PhysicsQueries::OverlapCircleXZ (Box2D) at ({:.2f}, {:.2f}) r={:.2f} -> {} hits",
                              centerXZ.x, centerXZ.y, radius, outHits.size());
            return;
        }
    }

    // Fallback for non-runtime or editor queries
    for (const auto& go : scene.GetGameObjects()) {
        if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) continue;
        auto* t = go->GetTransform();
        if (!t) continue;
        const glm::vec3 worldPos = t->GetWorldPosition();
        const glm::vec3 worldScale = t->GetWorldScale();
        const glm::vec2 posXZ{ worldPos.x, worldPos.z };
        const glm::vec2 scaleXZ{ std::abs(worldScale.x), std::abs(worldScale.z) };
        const float yawRad = glm::radians(t->GetWorldRotation().y);
        const float cosY = std::cos(yawRad);
        const float sinY = std::sin(yawRad);

        bool overlaps = false;
        for (const auto* cc : go->GetComponents<CircleCollider2D>()) {
            if (!cc->IsEnabled() || (cc->CategoryBits & maskBits) == 0) continue;
            const float effectiveRadius = cc->Radius * std::max(scaleXZ.x, scaleXZ.y);
            const glm::vec2 center = posXZ + RotateXZ(cc->OffsetXZ * scaleXZ, cosY, sinY);
            if (glm::distance(centerXZ, center) <= (radius + effectiveRadius)) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            const glm::vec2 localCenter = InverseRotateXZ(centerXZ - posXZ, cosY, sinY);
            for (const auto* bc : go->GetComponents<BoxCollider2D>()) {
                if (!bc->IsEnabled() || (bc->CategoryBits & maskBits) == 0) continue;
                const glm::vec2 bMin = bc->offset * scaleXZ - bc->size * scaleXZ * 0.5f;
                const glm::vec2 bMax = bc->offset * scaleXZ + bc->size * scaleXZ * 0.5f;
                const glm::vec2 closest = glm::clamp(localCenter, bMin, bMax);
                if (glm::distance(localCenter, closest) <= radius) {
                    overlaps = true;
                    break;
                }
            }
        }
        if (!overlaps) {
            for (const auto* cap : go->GetComponents<CapsuleCollider2D>()) {
                if (!cap->IsEnabled() || (cap->CategoryBits & maskBits) == 0) continue;
                const glm::vec2 a = posXZ + RotateXZ(cap->Center1 * scaleXZ, cosY, sinY);
                const glm::vec2 b = posXZ + RotateXZ(cap->Center2 * scaleXZ, cosY, sinY);
                const float effectiveRadius = cap->Radius * std::max(scaleXZ.x, scaleXZ.y);
                const glm::vec2 ab = b - a;
                const float abLenSq = glm::dot(ab, ab);
                const float tParam = (abLenSq > 1e-6f) ? glm::clamp(glm::dot(centerXZ - a, ab) / abLenSq, 0.0f, 1.0f) : 0.0f;
                const glm::vec2 closest = a + tParam * ab;
                if (glm::distance(centerXZ, closest) <= (radius + effectiveRadius)) {
                    overlaps = true;
                    break;
                }
            }
        }
        if (!overlaps) {
            if (const auto* tm = go->GetComponent<Tilemap>(); tm && tm->IsEnabled() && (PhysicsLayer::Environment & maskBits) != 0) {
                const uint32_t width = tm->GetWidth();
                const uint32_t height = tm->GetHeight();
                if (width > 0 && height > 0) {
                    const float safeScaleX = std::max(0.001f, scaleXZ.x);
                    const float safeScaleZ = std::max(0.001f, scaleXZ.y);
                    const float halfW = 0.5f * safeScaleX;
                    const float halfD = 0.5f * safeScaleZ;
                    for (uint32_t gz = 0; gz < height && !overlaps; ++gz) {
                        for (uint32_t gx = 0; gx < width; ++gx) {
                            if (tm->GetTile(gx, gz).walkable) continue;
                            const glm::vec2 tileCenter{ posXZ.x + static_cast<float>(gx) * safeScaleX,
                                                        posXZ.y + static_cast<float>(gz) * safeScaleZ };
                            const glm::vec2 bMin = tileCenter - glm::vec2(halfW, halfD);
                            const glm::vec2 bMax = tileCenter + glm::vec2(halfW, halfD);
                            const glm::vec2 closest = glm::clamp(centerXZ, bMin, bMax);
                            if (glm::distance(centerXZ, closest) <= radius) {
                                overlaps = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (overlaps) {
            outHits.push_back(go.get());
        }
    }
    ENGINE_CORE_TRACE("PhysicsQueries::OverlapCircleXZ (fallback) at ({:.2f}, {:.2f}) r={:.2f} -> {} hits",
                      centerXZ.x, centerXZ.y, radius, outHits.size());
}

void PhysicsQueries::OverlapArcXZ(const Scene& scene, glm::vec2 originXZ, glm::vec2 forwardXZ,
                                 float radius, float halfAngleDegrees, uint64_t maskBits,
                                 std::vector<GameObject*>& outHits) {
    outHits.clear();
    if (radius <= 0.0f || halfAngleDegrees <= 0.0f) {
        ENGINE_CORE_WARN("PhysicsQueries::OverlapArcXZ ignored non-positive radius {} or halfAngle {}", radius, halfAngleDegrees);
        return;
    }

    OverlapCircleXZ(scene, originXZ, radius, maskBits, outHits);
    if (outHits.empty()) return;

    const float lenSq = glm::dot(forwardXZ, forwardXZ);
    if (lenSq < 1e-6f || halfAngleDegrees >= 180.0f) {
        return;
    }

    const glm::vec2 fwd = forwardXZ / std::sqrt(lenSq);
    const float cosThreshold = std::cos(glm::radians(halfAngleDegrees));

    std::erase_if(outHits, [&](GameObject* go) {
        if (!go) return true;
        auto* t = go->GetTransform();
        if (!t) return true;

        const glm::vec3 worldPos = t->GetWorldPosition();
        const glm::vec2 targetXZ{ worldPos.x, worldPos.z };
        const glm::vec2 toTarget = targetXZ - originXZ;
        const float distSq = glm::dot(toTarget, toTarget);

        if (distSq < 1e-6f) {
            return false; // Target is coincident with query origin -> keep
        }

        const glm::vec2 dir = toTarget / std::sqrt(distSq);
        const float dot = glm::dot(fwd, dir);
        return dot < (cosThreshold - 1e-5f);
    });
    ENGINE_CORE_TRACE("PhysicsQueries::OverlapArcXZ at ({:.2f}, {:.2f}) r={:.2f} halfAngle={:.1f} -> {} hits",
                      originXZ.x, originXZ.y, radius, halfAngleDegrees, outHits.size());
}

bool PhysicsQueries::RaycastClosestXZ(const Scene& scene, glm::vec2 startXZ, glm::vec2 endXZ,
                                      uint64_t maskBits, RaycastHitXZ& outHit) {
    outHit = RaycastHitXZ{};
    glm::vec2 diff = endXZ - startXZ;
    float maxDist = glm::length(diff);
    if (maxDist < 1e-5f) {
        ENGINE_CORE_TRACE("PhysicsQueries::RaycastClosestXZ skipped degenerate zero-length ray");
        return false;
    }

    if (scene.IsRuntimeRunning()) {
        b2WorldId worldId = b2LoadWorldId(scene.GetPhysicsWorldId());
        if (b2World_IsValid(worldId)) {
            b2Vec2 origin{ startXZ.x, startXZ.y };
            b2Vec2 translation{ diff.x, diff.y };
            b2QueryFilter filter = b2DefaultQueryFilter();
            filter.categoryBits = PhysicsLayer::All;
            filter.maskBits = maskBits;

            // Check initial containment at startXZ (ignoring sensors and pending-destroy entities)
            RayOriginOverlapContext originCtx;
            originCtx.origin = origin;
            b2ShapeProxy originProxy = b2MakeProxy(&origin, 1, 1e-4f);
            b2World_OverlapShape(worldId, &originProxy, filter, Box2DRayOriginOverlapCallback, &originCtx);
            if (originCtx.hitEntity) {
                outHit.Entity = originCtx.hitEntity;
                outHit.PointXZ = startXZ;
                outHit.NormalXZ = -glm::normalize(diff);
                outHit.Fraction = 0.0f;
                return true;
            }

            RaycastContext rayCtx;
            rayCtx.outHit = &outHit;
            b2World_CastRay(worldId, origin, translation, filter, Box2DRaycastCallback, &rayCtx);
            if (rayCtx.found && outHit.Entity) {
                ENGINE_CORE_TRACE("PhysicsQueries::RaycastClosestXZ (Box2D) hit '{}' at ({:.2f}, {:.2f}) t={:.3f}",
                                  outHit.Entity->GetName(), outHit.PointXZ.x, outHit.PointXZ.y, outHit.Fraction);
                return true;
            }
            return false;
        }
    }

    // Fallback for non-runtime or editor queries
    float closestFraction = 1.0f;
    bool foundHit = false;

    for (const auto& go : scene.GetGameObjects()) {
        if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) continue;
        auto* t = go->GetTransform();
        if (!t) continue;
        const glm::vec3 worldPos = t->GetWorldPosition();
        const glm::vec3 worldScale = t->GetWorldScale();
        const glm::vec2 posXZ{ worldPos.x, worldPos.z };
        const glm::vec2 scaleXZ{ std::abs(worldScale.x), std::abs(worldScale.z) };
        const float yawRad = glm::radians(t->GetWorldRotation().y);
        const float cosY = std::cos(yawRad);
        const float sinY = std::sin(yawRad);

        // 1. CircleCollider2D
        for (const auto* cc : go->GetComponents<CircleCollider2D>()) {
            if (!cc->IsEnabled() || cc->IsSensor || (cc->CategoryBits & maskBits) == 0) continue;
            const float effectiveRadius = cc->Radius * std::max(scaleXZ.x, scaleXZ.y);
            const glm::vec2 center = posXZ + RotateXZ(cc->OffsetXZ * scaleXZ, cosY, sinY);
            const glm::vec2 d = diff;
            const glm::vec2 f = startXZ - center;
            const float c = glm::dot(f, f) - effectiveRadius * effectiveRadius;

            if (c <= 0.0f) {
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

        // 2. BoxCollider2D
        for (const auto* bc : go->GetComponents<BoxCollider2D>()) {
            if (!bc->IsEnabled() || bc->IsSensor || (bc->CategoryBits & maskBits) == 0) continue;
            // Transform ray into box's local XZ frame (inverse of right-handed +yawRad around Y)
            const glm::vec2 localStart = InverseRotateXZ(startXZ - posXZ, cosY, sinY);
            const glm::vec2 localDiff = InverseRotateXZ(diff, cosY, sinY);

            const glm::vec2 bMin = bc->offset * scaleXZ - bc->size * scaleXZ * 0.5f;
            const glm::vec2 bMax = bc->offset * scaleXZ + bc->size * scaleXZ * 0.5f;

            if (localStart.x >= bMin.x && localStart.x <= bMax.x &&
                localStart.y >= bMin.y && localStart.y <= bMax.y) {
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
                glm::vec2 hitNormalLocal{ 0.0f };

                bool hitBox = true;
                for (int axis = 0; axis < 2; ++axis) {
                    if (std::abs(localDiff[axis]) < 1e-6f) {
                        if (localStart[axis] < bMin[axis] || localStart[axis] > bMax[axis]) {
                            hitBox = false;
                            break;
                        }
                    } else {
                        const float ood = 1.0f / localDiff[axis];
                        float t1 = (bMin[axis] - localStart[axis]) * ood;
                        float t2 = (bMax[axis] - localStart[axis]) * ood;
                        glm::vec2 n1{ 0.0f }, n2{ 0.0f };
                        n1[axis] = -1.0f;
                        n2[axis] = 1.0f;

                        if (t1 > t2) {
                            std::swap(t1, t2);
                            std::swap(n1, n2);
                        }
                        if (t1 > tMin) {
                            tMin = t1;
                            hitNormalLocal = n1;
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
                    outHit.NormalXZ = RotateXZ(hitNormalLocal, cosY, sinY);
                    foundHit = true;
                }
            }
        }

        // 3. CapsuleCollider2D
        for (const auto* cap : go->GetComponents<CapsuleCollider2D>()) {
            if (!cap->IsEnabled() || cap->IsSensor || (cap->CategoryBits & maskBits) == 0) continue;
            const glm::vec2 p1 = posXZ + RotateXZ(cap->Center1 * scaleXZ, cosY, sinY);
            const glm::vec2 p2 = posXZ + RotateXZ(cap->Center2 * scaleXZ, cosY, sinY);
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

                    const glm::vec2 lStart{ glm::dot(startXZ - p1, U), glm::dot(startXZ - p1, V) };
                    const glm::vec2 lDiff{ glm::dot(diff, U), glm::dot(diff, V) };

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

                // B. Check cap circles
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

        // 4. Tilemap
        if (auto* tm = go->GetComponent<Tilemap>(); tm && tm->IsEnabled()) {
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
    if (foundHit && outHit.Entity) {
        ENGINE_CORE_TRACE("PhysicsQueries::RaycastClosestXZ (fallback) hit '{}' at ({:.2f}, {:.2f}) t={:.3f}",
                          outHit.Entity->GetName(), outHit.PointXZ.x, outHit.PointXZ.y, outHit.Fraction);
    }
    return foundHit;
}

} // namespace Engine
