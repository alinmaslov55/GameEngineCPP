#pragma once

#include <cstdint>
#include <cstddef>

namespace Engine {

/// @brief Type-safe, 64-bit value wrapper around Box2D runtime body identifier.
/// Eliminates raw void* casting and avoids leaking Box2D headers into Engine public API.
struct BodyHandle {
    uint64_t Value = 0;

    constexpr BodyHandle() noexcept = default;
    constexpr explicit BodyHandle(uint64_t val) noexcept : Value(val) {}
    constexpr BodyHandle(std::nullptr_t) noexcept : Value(0) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    constexpr bool operator==(const BodyHandle& other) const noexcept = default;
    constexpr bool operator==(std::nullptr_t) const noexcept { return Value == 0; }
    constexpr bool operator!=(std::nullptr_t) const noexcept { return Value != 0; }

    void Reset() noexcept { Value = 0; }
    BodyHandle& operator=(std::nullptr_t) noexcept { Value = 0; return *this; }
};

/// @brief Type-safe, 64-bit value wrapper around Box2D runtime shape identifier.
/// Eliminates raw void* casting and distinguishes shape handles from body handles at compile time.
struct ShapeHandle {
    uint64_t Value = 0;

    constexpr ShapeHandle() noexcept = default;
    constexpr explicit ShapeHandle(uint64_t val) noexcept : Value(val) {}
    constexpr ShapeHandle(std::nullptr_t) noexcept : Value(0) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    constexpr bool operator==(const ShapeHandle& other) const noexcept = default;
    constexpr bool operator==(std::nullptr_t) const noexcept { return Value == 0; }
    constexpr bool operator!=(std::nullptr_t) const noexcept { return Value != 0; }

    void Reset() noexcept { Value = 0; }
    ShapeHandle& operator=(std::nullptr_t) noexcept { Value = 0; return *this; }
};

} // namespace Engine
