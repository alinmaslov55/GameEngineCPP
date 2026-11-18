#ifndef SCENE_LIGHTQUERYSYSTEM_H
#define SCENE_LIGHTQUERYSYSTEM_H

#include <cstdint>
#include <glm/vec2.hpp>

namespace Engine {

class GameObject;
class Scene;

/// @brief CPU illumination query result evaluated on the XZ gameplay plane.
struct IlluminationSample {
    float SafeIntensity = 0.0f;               // Sum of SafeLifeline smoothstep attenuations at worldXZ
    float HazardIntensity = 0.0f;             // Sum of DeceptiveHazard attenuations
    float PrimaryTorchNormalizedDist = 999.0f; // distXZ / TorchRadius (< 1.0 = inside torch circle)
    GameObject* DominantSafeSource = nullptr; // Highest-contributing SafeLifeline entity at worldXZ
    float NeutralIntensity = 0.0f;            // Sum of NeutralShrine attenuations

    [[nodiscard]] bool IsInSafeLight(float threshold = 0.05f) const noexcept {
        return SafeIntensity >= threshold;
    }

    [[nodiscard]] bool IsInSearingRim(float innerRatio = 0.82f) const noexcept {
        return PrimaryTorchNormalizedDist >= innerRatio && PrimaryTorchNormalizedDist <= 1.02f;
    }
};

/// @brief Optional modifiers for detailed CPU illumination queries (flicker & line-of-light occlusion).
struct IlluminationQueryOptions {
    bool CheckLineOfLight = false;           // Casts XZ ray against OccluderMaskBits via PhysicsQueries
    uint64_t OccluderMaskBits = (1ULL << 1); // Matches PhysicsLayer::Environment
    bool IncludeFlicker = false;             // Scales by EvaluateEffectiveIntensity(Time::GetTotalTime(), uuid)
};

/// @brief Dedicated CPU illumination query evaluator for safe-light, umbral vision, and line-of-light checks.
class LightQuerySystem {
public:
    [[nodiscard]] static IlluminationSample EvaluateIlluminationAtXZ(
        const Scene& scene,
        glm::vec2 worldXZ) noexcept;

    [[nodiscard]] static IlluminationSample EvaluateIlluminationDetailedXZ(
        const Scene& scene,
        glm::vec2 worldXZ,
        const IlluminationQueryOptions& options) noexcept;
};

} // namespace Engine

#endif // SCENE_LIGHTQUERYSYSTEM_H
