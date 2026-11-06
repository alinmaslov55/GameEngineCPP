#ifndef SCENE_COMPONENT_MASK128_H
#define SCENE_COMPONENT_MASK128_H

#include <cstdint>

namespace Engine {

struct ComponentMask128 {
    uint64_t lo = 0;
    uint64_t hi = 0;

    constexpr ComponentMask128() noexcept = default;
    constexpr ComponentMask128(uint64_t l, uint64_t h) noexcept : lo(l), hi(h) {}

    [[nodiscard]] constexpr bool Intersects(const ComponentMask128& o) const noexcept {
        return ((lo & o.lo) | (hi & o.hi)) != 0;
    }

    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return (lo | hi) == 0;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return (lo | hi) != 0;
    }

    constexpr ComponentMask128 operator&(const ComponentMask128& o) const noexcept {
        return { lo & o.lo, hi & o.hi };
    }

    constexpr ComponentMask128 operator|(const ComponentMask128& o) const noexcept {
        return { lo | o.lo, hi | o.hi };
    }

    constexpr ComponentMask128 operator^(const ComponentMask128& o) const noexcept {
        return { lo ^ o.lo, hi ^ o.hi };
    }

    constexpr ComponentMask128 operator~() const noexcept {
        return { ~lo, ~hi };
    }

    constexpr ComponentMask128& operator&=(const ComponentMask128& o) noexcept {
        lo &= o.lo;
        hi &= o.hi;
        return *this;
    }

    constexpr ComponentMask128& operator|=(const ComponentMask128& o) noexcept {
        lo |= o.lo;
        hi |= o.hi;
        return *this;
    }

    constexpr ComponentMask128& operator^=(const ComponentMask128& o) noexcept {
        lo ^= o.lo;
        hi ^= o.hi;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const ComponentMask128& o) const noexcept {
        return lo == o.lo && hi == o.hi;
    }

    [[nodiscard]] constexpr bool operator!=(const ComponentMask128& o) const noexcept {
        return !(*this == o);
    }

    [[nodiscard]] constexpr bool operator==(int zero) const noexcept {
        return (lo | hi) == 0;
    }

    [[nodiscard]] constexpr bool operator!=(int zero) const noexcept {
        return (lo | hi) != 0;
    }

    static constexpr ComponentMask128 FromBit(uint32_t bitIndex) noexcept {
        if (bitIndex < 64) {
            return { 1ULL << bitIndex, 0ULL };
        } else if (bitIndex < 128) {
            return { 0ULL, 1ULL << (bitIndex - 64) };
        }
        return { 0ULL, 0ULL };
    }
};

} // namespace Engine

#endif // SCENE_COMPONENT_MASK128_H
