#ifndef MATH_NOISEGENERATOR_H
#define MATH_NOISEGENERATOR_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Engine {

/// @brief Immutable, thread-safe 2D Simplex / fBm / domain-warp noise evaluator
class NoiseGenerator {
public:
    explicit NoiseGenerator(uint64_t seed = 42ULL) noexcept;

    [[nodiscard]] static constexpr uint64_t SplitMix64(uint64_t x) noexcept {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    [[nodiscard]] float Simplex2D(float x, float z) const noexcept;
    [[nodiscard]] float FBm2D(float x, float z, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
    [[nodiscard]] glm::vec2 DomainWarp2D(float x, float z, float amplitude = 6.0f) const noexcept;
    [[nodiscard]] uint64_t GetSeed() const noexcept { return m_Seed; }

private:
    uint64_t m_Seed = 42ULL;
    std::array<uint8_t, 512> m_Perm{};
};

} // namespace Engine

#endif // MATH_NOISEGENERATOR_H
