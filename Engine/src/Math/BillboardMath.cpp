#include "Engine/Math/BillboardMath.h"

#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

void BuildGroundQuadCorners(const glm::vec3& center,
                            const glm::vec2& sizeXZ,
                            float rotationYRadians,
                            std::span<glm::vec3, 4> outPositions) noexcept {
    const float hx = 0.5f * sizeXZ.x;
    const float hz = 0.5f * sizeXZ.y;
    const float y = center.y;

    if (std::abs(rotationYRadians) <= 1e-4f) {
        outPositions[0] = { center.x - hx, y, center.z + hz };
        outPositions[1] = { center.x + hx, y, center.z + hz };
        outPositions[2] = { center.x + hx, y, center.z - hz };
        outPositions[3] = { center.x - hx, y, center.z - hz };
        return;
    }

    const float c = std::cos(rotationYRadians);
    const float s = std::sin(rotationYRadians);
    // CCW rotation around +Y matching glm::rotate(+Y, rotationYRadians)
    const glm::vec3 halfR{ c * hx, 0.0f, -s * hx };
    const glm::vec3 halfU{ -s * hz, 0.0f, -c * hz };

    outPositions[0] = center - halfR - halfU;
    outPositions[1] = center + halfR - halfU;
    outPositions[2] = center + halfR + halfU;
    outPositions[3] = center - halfR + halfU;
}

void ComputeBillboardCorners(const glm::vec3& anchorPosition,
                             const glm::vec2& size,
                             const glm::vec3& rightAxis,
                             const glm::vec3& upAxis,
                             float rotationZDegrees,
                             std::span<glm::vec3, 4> outPositions) {
    glm::vec3 r = rightAxis;
    glm::vec3 u = upAxis;
    if (std::abs(rotationZDegrees) > 1e-4f) {
        const float rad = glm::radians(rotationZDegrees);
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        r = rightAxis * c + upAxis * s;
        u = -rightAxis * s + upAxis * c;
    }

    const glm::vec3 center = anchorPosition + upAxis * (0.5f * size.y);
    const glm::vec3 halfR = r * (0.5f * size.x);
    const glm::vec3 halfU = u * (0.5f * size.y);

    outPositions[0] = center - halfR - halfU;
    outPositions[1] = center + halfR - halfU;
    outPositions[2] = center + halfR + halfU;
    outPositions[3] = center - halfR + halfU;
}

void ComputeParticleCorners(const glm::vec3& position,
                            const glm::vec3& velocity,
                            float size,
                            float rotationRadians,
                            ParticleOrientation orientation,
                            const glm::vec3& camRight,
                            const glm::vec3& camUp,
                            std::span<glm::vec3, 4> outPositions) noexcept {
    const float halfSize = 0.5f * size;
    glm::vec3 r = camRight;
    glm::vec3 u = camUp;
    bool applyRotation = true;

    if (orientation == ParticleOrientation::FlatGroundXZ) {
        r = { 1.0f, 0.0f, 0.0f };
        u = { 0.0f, 0.0f, -1.0f };
    } else if (orientation == ParticleOrientation::VelocityAligned) {
        const float velLenSq = glm::dot(velocity, velocity);
        if (velLenSq > 1e-6f) {
            const glm::vec3 velDir = velocity * (1.0f / std::sqrt(velLenSq));
            const glm::vec3 viewNormal = glm::cross(camRight, camUp);
            const glm::vec3 sideRaw = glm::cross(velDir, viewNormal);
            const float sideLenSq = glm::dot(sideRaw, sideRaw);
            if (sideLenSq > 1e-6f) {
                r = sideRaw * (1.0f / std::sqrt(sideLenSq));
                u = velDir;
                applyRotation = false;
            }
        }
    }

    if (applyRotation && std::abs(rotationRadians) > 1e-4f) {
        const float c = std::cos(rotationRadians);
        const float s = std::sin(rotationRadians);
        const glm::vec3 rotR = r * c + u * s;
        const glm::vec3 rotU = -r * s + u * c;
        r = rotR;
        u = rotU;
    }

    const glm::vec3 halfR = r * halfSize;
    const glm::vec3 halfU = u * halfSize;
    outPositions[0] = position - halfR - halfU;
    outPositions[1] = position + halfR - halfU;
    outPositions[2] = position + halfR + halfU;
    outPositions[3] = position - halfR + halfU;
}

} // namespace Engine
