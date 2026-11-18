#ifndef MATH_BILLBOARDMATH_H
#define MATH_BILLBOARDMATH_H

#include "Engine/Particles/ParticleSystem.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <span>

namespace Engine {

void BuildGroundQuadCorners(const glm::vec3& center,
                            const glm::vec2& sizeXZ,
                            float rotationYRadians,
                            std::span<glm::vec3, 4> outPositions) noexcept;

void ComputeBillboardCorners(const glm::vec3& anchorPosition,
                             const glm::vec2& size,
                             const glm::vec3& rightAxis,
                             const glm::vec3& upAxis,
                             float rotationZDegrees,
                             std::span<glm::vec3, 4> outPositions);

void ComputeParticleCorners(const glm::vec3& position,
                            const glm::vec3& velocity,
                            float size,
                            float rotationRadians,
                            ParticleOrientation orientation,
                            const glm::vec3& camRight,
                            const glm::vec3& camUp,
                            std::span<glm::vec3, 4> outPositions) noexcept;

} // namespace Engine

#endif // MATH_BILLBOARDMATH_H
