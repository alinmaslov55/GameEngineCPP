#ifndef RENDERER_SHADER_H
#define RENDERER_SHADER_H
#include <memory>
#include <string>

namespace Engine {
class Shader {
public:
    Shader(const std::string &vertexSrc, const std::string &fragmentSrc);
    ~Shader();

    void Bind() const;
    void Unbind() const;

private:
    uint32_t m_RendererID;
};
} // namespace Engine

#endif // RENDERER_SHADER_H