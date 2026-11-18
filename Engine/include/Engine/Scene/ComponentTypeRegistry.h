#ifndef SCENE_COMPONENTTYPEREGISTRY_H
#define SCENE_COMPONENTTYPEREGISTRY_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/ComponentMask128.h"
#include "Engine/Utils/Logger.h"

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace Engine::Detail {

inline constexpr uint32_t kMaxComponentTypes = 128;

inline uint32_t AllocateComponentTypeID(std::atomic<uint32_t>& counter) noexcept {
    const uint32_t id = counter.fetch_add(1, std::memory_order_relaxed);
    if (id >= kMaxComponentTypes) [[unlikely]] {
        ENGINE_CORE_CRITICAL(
            "ComponentMask128 limit exceeded: attempted to register component type ID {} (max allowed: {}).",
            id, kMaxComponentTypes);
        if (auto logger = ::Engine::Logger::GetCoreLogger()) {
            logger->flush();
        }
        std::fprintf(stderr,
                     "[FATAL] ComponentMask128 limit exceeded: component type ID %u (max allowed: %u).\n",
                     id, kMaxComponentTypes);
        std::fflush(stderr);
        assert(id < kMaxComponentTypes && "Exceeded maximum allowed 128 unique Component types in ComponentMask128!");
        std::abort();
    }
    return id;
}

inline uint32_t GetNextComponentTypeID() noexcept {
    static std::atomic<uint32_t> s_Counter{0};
    return AllocateComponentTypeID(s_Counter);
}

template<typename T>
[[nodiscard]] inline uint16_t GetComponentTypeID() noexcept {
    static const uint16_t s_TypeID = static_cast<uint16_t>(GetNextComponentTypeID());
    return s_TypeID;
}

template<typename T>
[[nodiscard]] inline ComponentMask128 GetComponentTypeMask() noexcept {
    static const ComponentMask128 s_Mask = ComponentMask128::FromBit(GetComponentTypeID<T>());
    return s_Mask;
}

template<typename T>
concept HasComponentBaseType = requires { typename T::ComponentBaseType; };

template<typename T>
[[nodiscard]] constexpr bool IsRegisteredPolymorphicBase() noexcept {
    if constexpr (HasComponentBaseType<T>) {
        return std::is_same_v<T, typename T::ComponentBaseType>;
    }
    return false;
}

template<typename CleanT, typename BaseT>
[[nodiscard]] constexpr bool IsComponentWithBase() noexcept {
    if constexpr (std::is_same_v<CleanT, BaseT>) {
        return true;
    } else if constexpr (HasComponentBaseType<CleanT>) {
        return std::is_same_v<typename CleanT::ComponentBaseType, BaseT>;
    }
    return false;
}

template<typename T>
concept HasOverriddenFixedUpdate = requires {
    requires !std::is_same_v<decltype(&T::FixedUpdate), void(Component::*)(float)>;
};

} // namespace Engine::Detail

#endif // SCENE_COMPONENTTYPEREGISTRY_H
