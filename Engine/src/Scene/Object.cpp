#include "Engine/Scene/Object.h"
#include <random>

namespace Engine {

static uint64_t GenerateUUID() {
    static std::random_device s_RandomDevice;
    static std::mt19937_64 s_Engine(s_RandomDevice());
    static std::uniform_int_distribution<uint64_t> s_UniformDistribution(1, UINT64_MAX);
    return s_UniformDistribution(s_Engine);
}

Object::Object(const std::string& name)
    : m_UUID(GenerateUUID()), m_Name(name) {}

Object::Object(uint64_t uuid, const std::string& name)
    : m_UUID(uuid != 0 ? uuid : GenerateUUID()), m_Name(name) {}

} // namespace Engine

