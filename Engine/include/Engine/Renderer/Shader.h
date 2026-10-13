#ifndef RENDERER_SHADER_H
#define RENDERER_SHADER_H
#include <memory>
#include <string>

#include <glm/vec4.hpp>

namespace Engine {
class Shader {
public:
    Shader(const std::string &vertexSrc, const std::string &fragmentSrc);
    ~Shader();

    void Bind() const;
    void Unbind() const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat4(const std::string& name, const glm::vec4& value) const;
private:
    uint32_t m_RendererID;
};
} // namespace Engine

#endif // RENDERER_SHADER_H