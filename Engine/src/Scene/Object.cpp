#include "Engine/Scene/Object.h"
#include <random>

namespace Engine {

static uint64_t GenerateUUID() {
    thread_local std::mt19937_64 s_Engine([]() {
        std::random_device rd;
        return rd();
    }());
    thread_local std::uniform_int_distribution<uint64_t> s_UniformDistribution(1, UINT64_MAX);
    return s_UniformDistribution(s_Engine);
}

Object::Object(std::string_view name)
    : m_UUID(GenerateUUID()), m_Name(name) {}

Object::Object(uint64_t uuid, std::string_view name)
    : m_UUID(uuid != 0 ? uuid : GenerateUUID()), m_Name(name) {}

} // namespace Engine
