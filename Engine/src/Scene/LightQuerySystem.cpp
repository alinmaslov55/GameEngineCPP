#include "Engine/Scene/LightQuerySystem.h"
#include "Engine/Core/Time.h"
#include "Engine/Physics/PhysicsQueries.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {

namespace {

[[nodiscard]] bool IsSelfOrAncestor(const GameObject* lightGo, const GameObject* hitGo) noexcept {
    if (!lightGo || !hitGo) return false;
    if (lightGo == hitGo) return true;
    const Transform* cur = lightGo->GetTransform();
    while (cur) {
        if (cur->GetGameObject() == hitGo) {
            return true;
        }
        cur = cur->GetParent();
    }
    return false;
}

} // namespace

IlluminationSample LightQuerySystem::EvaluateIlluminationAtXZ(const Scene& scene,
                                                              glm::vec2 worldXZ) noexcept {
    return EvaluateIlluminationDetailedXZ(scene, worldXZ, IlluminationQueryOptions{});
}

IlluminationSample LightQuerySystem::EvaluateIlluminationDetailedXZ(
    const Scene& scene,
    glm::vec2 worldXZ,
    const IlluminationQueryOptions& options) noexcept {
    IlluminationSample result{};
    static const auto kPointLightMask = Detail::GetComponentTypeMask<PointLightComponent>();

    bool hasPrimaryHeroTorch = false;
    bool bestPrimaryCastShadows = false;
    float bestPrimaryNormDist = 999.0f;
    float bestPrimaryDistSq = std::numeric_limits<float>::max();
    float bestSafeContribution = 0.0f;

    auto evaluateEntity = [&](GameObject* go) noexcept {
        if (!go || go->IsPendingDestroy() || !go->IsActiveInHierarchy()) {
            return;
        }
        if (!(go->GetComponentMask() & kPointLightMask)) {
            return;
        }
        const auto* pl = go->GetComponent<PointLightComponent>();
        if (!pl || !pl->IsEnabled() || pl->Radius <= 0.0f) {
            return;
        }

        const float baseIntensity = options.IncludeFlicker
            ? pl->EvaluateEffectiveIntensity(Time::GetTotalTime(), static_cast<uint32_t>(go->GetUUID()))
            : std::max(0.0f, pl->Intensity);
        if (baseIntensity <= 0.0f) {
            return;
        }

        const Transform* transform = go->GetTransform();
        const glm::vec3 worldPos = transform ? transform->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec2 lightXZ{ worldPos.x, worldPos.z };
        const glm::vec2 delta = worldXZ - lightXZ;
        const float distSqXZ = glm::dot(delta, delta);
        const float distXZ = std::sqrt(distSqXZ);
        const float safeRadius = std::max(0.05f, pl->Radius);
        const float normDist = distXZ / safeRadius;

        const float atten = PointLightComponent::EvaluateSmoothstepAttenuation(distXZ, pl->Radius, pl->Falloff);
        if (atten <= 0.0f) {
            return;
        }

        if (options.CheckLineOfLight && distXZ > 1e-4f) {
            RaycastHitXZ hit{};
            // Cast from query point toward light origin so intervening walls block before hitting the light owner or its ancestors
            if (PhysicsQueries::RaycastClosestXZ(scene, worldXZ, lightXZ, options.OccluderMaskBits, hit) &&
                hit.Entity && !IsSelfOrAncestor(go, hit.Entity)) {
                return;
            }
        }

        if (pl->Semantic == LightSemantic::SafeLifeline) {
            const bool isHeroTorch = (pl->Priority == LightPriority::Critical_HeroTorch);
            bool takeAsPrimary = false;
            if (isHeroTorch != hasPrimaryHeroTorch) {
                takeAsPrimary = isHeroTorch;
            } else if (isHeroTorch && pl->CastShadows != bestPrimaryCastShadows) {
                takeAsPrimary = pl->CastShadows;
            } else if (std::abs(normDist - bestPrimaryNormDist) > 1e-5f) {
                takeAsPrimary = normDist < bestPrimaryNormDist;
            } else if (distSqXZ < bestPrimaryDistSq) {
                takeAsPrimary = true;
            }

            if (takeAsPrimary) {
                hasPrimaryHeroTorch = isHeroTorch;
                bestPrimaryCastShadows = pl->CastShadows;
                bestPrimaryNormDist = normDist;
                bestPrimaryDistSq = distSqXZ;
                result.PrimaryTorchNormalizedDist = normDist;
            }
        }

        const float contribution = baseIntensity * atten;
        switch (pl->Semantic) {
            case LightSemantic::SafeLifeline:
                result.SafeIntensity += contribution;
                if (contribution > bestSafeContribution) {
                    bestSafeContribution = contribution;
                    result.DominantSafeSource = go;
                }
                break;
            case LightSemantic::NeutralShrine:
                result.NeutralIntensity += contribution;
                break;
            case LightSemantic::DeceptiveHazard:
                result.HazardIntensity += contribution;
                break;
        }
    };

    for (GameObject* go : scene.GetPointLightEntities()) {
        evaluateEntity(go);
    }

    return result;
}

} // namespace Engine
