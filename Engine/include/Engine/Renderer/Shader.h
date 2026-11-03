#ifndef RENDERER_SHADER_H
#define RENDERER_SHADER_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <unordered_map>

#include <glm/glm.hpp>

namespace Engine {

class Shader {
public:
    Shader(const std::string& filepath);
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    [[nodiscard]] bool Reload();
    [[nodiscard]] bool IsValid() const noexcept { return m_RendererID != 0; }
    [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
    [[nodiscard]] const std::string& GetFilePath() const noexcept { return m_FilePath; }
    [[nodiscard]] uint32_t GetRendererID() const noexcept { return m_RendererID; }

    void SetInt(std::string_view name, int value) const;
    void SetIntArray(std::string_view name, const int* values, uint32_t count) const;
    void SetIntArray(std::string_view name, std::span<const int> values) const;
    void SetFloat(std::string_view name, float value) const;
    void SetFloatArray(std::string_view name, const float* values, uint32_t count) const;
    void SetFloatArray(std::string_view name, std::span<const float> values) const;
    void SetFloat2(std::string_view name, const glm::vec2& value) const;
    void SetFloat3(std::string_view name, const glm::vec3& value) const;
    void SetFloat4(std::string_view name, const glm::vec4& value) const;
    void SetMat4(std::string_view name, const glm::mat4& matrix) const;

private:
    [[nodiscard]] int GetUniformLocation(std::string_view name) const;
    std::string ReadFile(const std::string& filepath);
    std::unordered_map<uint32_t, std::string> PreProcess(const std::string& source);
    bool Compile(const std::unordered_map<uint32_t, std::string>& shaderSources);

private:
    struct StringHash {
        using is_transparent = void;
        [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };

    uint32_t m_RendererID = 0;
    std::string m_FilePath;
    std::string m_Name;
    mutable std::unordered_map<std::string, int, StringHash, std::equal_to<>> m_UniformLocationCache;
};

} // namespace Engine

#endif // RENDERER_SHADER_H
