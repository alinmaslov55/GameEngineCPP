#ifndef MATH_FRUSTUM_H
#define MATH_FRUSTUM_H

#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

struct Frustum {
    // 6 inward-facing planes (Left, Right, Bottom, Top, Near, Far): (nx, ny, nz, d)
    std::array<glm::vec4, 6> Planes{};

    [[nodiscard]] static Frustum FromViewProjection(const glm::mat4& vp) noexcept {
        Frustum frustum;
        const glm::vec4 row0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        const glm::vec4 row1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        const glm::vec4 row2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        const glm::vec4 row3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

        frustum.Planes[0] = NormalizePlane(row3 + row0); // Left
        frustum.Planes[1] = NormalizePlane(row3 - row0); // Right
        frustum.Planes[2] = NormalizePlane(row3 + row1); // Bottom
        frustum.Planes[3] = NormalizePlane(row3 - row1); // Top
        frustum.Planes[4] = NormalizePlane(row3 + row2); // Near
        frustum.Planes[5] = NormalizePlane(row3 - row2); // Far
        return frustum;
    }

    [[nodiscard]] bool IntersectsSphere(const glm::vec3& center, float radius) const noexcept {
        for (const auto& plane : Planes) {
            const float dist = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
            if (dist < -radius) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IntersectsAABB(const glm::vec3& minBounds, const glm::vec3& maxBounds) const noexcept {
        for (const auto& plane : Planes) {
            const glm::vec3 pVertex{
                (plane.x >= 0.0f) ? maxBounds.x : minBounds.x,
                (plane.y >= 0.0f) ? maxBounds.y : minBounds.y,
                (plane.z >= 0.0f) ? maxBounds.z : minBounds.z
            };
            if (plane.x * pVertex.x + plane.y * pVertex.y + plane.z * pVertex.z + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] static glm::vec4 NormalizePlane(const glm::vec4& plane) noexcept {
        const float len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        return (len > 1e-6f) ? (plane / len) : plane;
    }
};

} // namespace Engine

#endif // MATH_FRUSTUM_H
