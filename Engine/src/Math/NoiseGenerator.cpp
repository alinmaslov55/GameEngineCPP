#include "Engine/Math/NoiseGenerator.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace Engine {

NoiseGenerator::NoiseGenerator(uint64_t seed) noexcept
    : m_Seed(seed) {
    std::array<uint8_t, 256> base{};
    std::iota(base.begin(), base.end(), static_cast<uint8_t>(0));

    uint64_t state = SplitMix64(seed);
    for (int i = 255; i > 0; --i) {
        state = SplitMix64(state);
        const int j = static_cast<int>(state % static_cast<uint64_t>(i + 1));
        std::swap(base[static_cast<size_t>(i)], base[static_cast<size_t>(j)]);
    }

    for (size_t i = 0; i < 256u; ++i) {
        m_Perm[i] = base[i];
        m_Perm[i + 256u] = base[i];
    }
}

float NoiseGenerator::Simplex2D(float x, float z) const noexcept {
    static constexpr float kF2 = 0.366025403784f; // 0.5 * (sqrt(3) - 1)
    static constexpr float kG2 = 0.211324865405f; // (3 - sqrt(3)) / 6
    static constexpr float kGrad2[8][2] = {
        {  1.0f,  1.0f }, { -1.0f,  1.0f }, {  1.0f, -1.0f }, { -1.0f, -1.0f },
        {  1.0f,  0.0f }, { -1.0f,  0.0f }, {  0.0f,  1.0f }, {  0.0f, -1.0f }
    };

    const float s = (x + z) * kF2;
    const int32_t i = static_cast<int32_t>(std::floor(x + s));
    const int32_t j = static_cast<int32_t>(std::floor(z + s));
    const float t = static_cast<float>(i + j) * kG2;
    const float x0 = x - (static_cast<float>(i) - t);
    const float z0 = z - (static_cast<float>(j) - t);

    const int32_t i1 = (x0 > z0) ? 1 : 0;
    const int32_t j1 = (x0 > z0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + kG2;
    const float z1 = z0 - static_cast<float>(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2;
    const float z2 = z0 - 1.0f + 2.0f * kG2;

    const uint8_t ii = static_cast<uint8_t>(i);
    const uint8_t jj = static_cast<uint8_t>(j);

    auto cornerContrib = [&](float cx, float cz, uint8_t permIdx) noexcept -> float {
        float dist = 0.5f - cx * cx - cz * cz;
        if (dist <= 0.0f) {
            return 0.0f;
        }
        dist *= dist;
        const auto& g = kGrad2[m_Perm[permIdx] & 7u];
        return dist * dist * (g[0] * cx + g[1] * cz);
    };

    const uint8_t gi0 = m_Perm[static_cast<size_t>(ii) + m_Perm[jj]];
    const uint8_t gi1 = m_Perm[static_cast<size_t>(static_cast<uint8_t>(ii + i1)) + m_Perm[static_cast<uint8_t>(jj + j1)]];
    const uint8_t gi2 = m_Perm[static_cast<size_t>(static_cast<uint8_t>(ii + 1u)) + m_Perm[static_cast<uint8_t>(jj + 1u)]];

    const float n0 = cornerContrib(x0, z0, gi0);
    const float n1 = cornerContrib(x1, z1, gi1);
    const float n2 = cornerContrib(x2, z2, gi2);

    return std::clamp(70.0f * (n0 + n1 + n2), -1.0f, 1.0f);
}

float NoiseGenerator::FBm2D(float x, float z, int octaves, float lacunarity, float gain) const noexcept {
    const int safeOctaves = std::clamp(octaves, 1, 12);
    float sum = 0.0f;
    float amp = 1.0f;
    float ampSum = 0.0f;
    float freq = 1.0f;

    for (int o = 0; o < safeOctaves; ++o) {
        sum += amp * Simplex2D(x * freq, z * freq);
        ampSum += amp;
        freq *= lacunarity;
        amp *= gain;
    }

    return (ampSum > 1e-6f) ? std::clamp(sum / ampSum, -1.0f, 1.0f) : 0.0f;
}

glm::vec2 NoiseGenerator::DomainWarp2D(float x, float z, float amplitude) const noexcept {
    const float dx = FBm2D(x * 0.025f + 17.3f, z * 0.025f - 29.1f, 3) * amplitude;
    const float dz = FBm2D(x * 0.025f - 53.7f, z * 0.025f + 41.9f, 3) * amplitude;
    return { x + dx, z + dz };
}

} // namespace Engine
