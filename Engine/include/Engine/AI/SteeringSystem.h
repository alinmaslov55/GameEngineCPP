#ifndef AI_STEERINGSYSTEM_H
#define AI_STEERINGSYSTEM_H

#include <glm/vec2.hpp>
#include <array>
#include <cstdint>
#include <span>

namespace Engine {

class Scene;
class GameObject;

struct SteeringParams {
    float MaxSpeed = 4.2f;
    float SeparationRadius = 1.15f;
    float SeparationWeight = 1.65f;
    float ObstacleWhiskerLength = 1.6f;
    float AvoidanceWeight = 2.2f;
    float TargetOrbitRadius = 0.0f; // > 0 for Wisps orbiting just outside Torch edge!
    float FireAvoidanceWeight = 3.0f;
    uint64_t ObstacleCollisionMask = 0; // 0 defaults to Environment | LowObstacle
    bool OrbitClockwise = true;
    float ArrivalRadius = 0.8f;
};

class SteeringSystem {
public:
    static constexpr size_t SlotCount = 12;

    // Context-map (12 radial interest/danger slots) steering solver:
    static glm::vec2 ComputeContextSteeringXZ(
        const Scene& scene,
        const GameObject* agent,
        const glm::vec2& targetXZ,
        std::span<const glm::vec2> neighborPositionsXZ,
        const SteeringParams& params);

    // Overload taking explicit 2D agent coordinates and radius
    static glm::vec2 ComputeContextSteeringXZ(
        const Scene& scene,
        const glm::vec2& agentPositionXZ,
        float agentRadius,
        const glm::vec2& targetXZ,
        std::span<const glm::vec2> neighborPositionsXZ,
        const SteeringParams& params,
        const GameObject* agentEntity = nullptr);

    // Diagnostic context evaluation helper (fills interest, danger, and context maps for inspection)
    static glm::vec2 EvaluateContextMap(
        const Scene& scene,
        const glm::vec2& agentPositionXZ,
        float agentRadius,
        const glm::vec2& targetXZ,
        std::span<const glm::vec2> neighborPositionsXZ,
        const SteeringParams& params,
        const GameObject* agentEntity,
        std::array<float, SlotCount>* outInterest,
        std::array<float, SlotCount>* outDanger,
        std::array<float, SlotCount>* outContext);

    // Static 12-slot unit direction vectors (30 degree increments on XZ)
    [[nodiscard]] static const std::array<glm::vec2, SlotCount>& GetSlotDirections() noexcept;
};

} // namespace Engine

#endif // AI_STEERINGSYSTEM_H
