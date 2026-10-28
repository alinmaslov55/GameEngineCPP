#ifndef RENDERER_SHADER_H
#define RENDERER_SHADER_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace Engine {
class Shader {
public:
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    ~Shader();

    void Bind() const;
    void Unbind() const;

    void SetInt(const std::string& name, int value) const;
    void SetIntArray(const std::string& name, const int* values, uint32_t count) const;
    void SetFloat(const std::string& name, float value) const;
    void SetFloatArray(const std::string& name, const float* values, uint32_t count) const;
    void SetFloat2(const std::string& name, const glm::vec2& value) const;
    void SetFloat3(const std::string& name, const glm::vec3& value) const;
    void SetFloat4(const std::string& name, const glm::vec4& value) const;
    void SetMat4(const std::string& name, const glm::mat4& matrix) const;

private:
    int GetUniformLocation(const std::string& name) const;

private:
    uint32_t m_RendererID = 0;
    mutable std::unordered_map<std::string, int> m_UniformLocationCache;
};
} // namespace Engine

#endif // RENDERER_SHADER_H
