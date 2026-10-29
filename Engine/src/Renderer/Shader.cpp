#include "Engine/Renderer/Shader.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <utility>

namespace Engine {
Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc) {
    ENGINE_CORE_INFO("Compiling shader program...");
    uint32_t vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vSrc = vertexSrc.c_str();
    glShaderSource(vertexShader, 1, &vSrc, nullptr);
    glCompileShader(vertexShader);

    GLint isCompiled = 0;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);
        glDeleteShader(vertexShader);
        ENGINE_CORE_ERROR("Vertex shader compilation failed: {}", infoLog.data());
        return;
    }

    uint32_t fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fSrc = fragmentSrc.c_str();
    glShaderSource(fragmentShader, 1, &fSrc, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        ENGINE_CORE_ERROR("Fragment shader compilation failed: {}", infoLog.data());
        return;
    }

    m_RendererID = glCreateProgram();
    glAttachShader(m_RendererID, vertexShader);
    glAttachShader(m_RendererID, fragmentShader);
    glLinkProgram(m_RendererID);

    GLint isLinked = 0;
    glGetProgramiv(m_RendererID, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE) {
        GLint maxLength = 0;
        glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(m_RendererID, maxLength, &maxLength, &infoLog[0]);
        glDeleteProgram(m_RendererID);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        ENGINE_CORE_ERROR("Shader link failed: {}", infoLog.data());
        return;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    ENGINE_CORE_INFO("Shader program created successfully with ID: {}", m_RendererID);
}

Shader::~Shader() {
    if (m_RendererID != 0) {
        ENGINE_CORE_DEBUG("Shader program destroyed with ID: {}", m_RendererID);
        glDeleteProgram(m_RendererID);
        m_RendererID = 0;
    }
}

Shader::Shader(Shader&& other) noexcept
    : m_RendererID(std::exchange(other.m_RendererID, 0)),
      m_UniformLocationCache(std::move(other.m_UniformLocationCache)) {
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0) {
            glDeleteProgram(m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_UniformLocationCache = std::move(other.m_UniformLocationCache);
    }
    return *this;
}

void Shader::Bind() const {
    ENGINE_CORE_TRACE("Shader program ID {} bound", m_RendererID);
    glUseProgram(m_RendererID);
}

void Shader::Unbind() const {
    ENGINE_CORE_TRACE("Shader program ID {} unbound", m_RendererID);
    glUseProgram(0);
}

int Shader::GetUniformLocation(const std::string& name) const {
    auto it = m_UniformLocationCache.find(name);
    if (it != m_UniformLocationCache.end()) {
        return it->second;
    }

    int location = glGetUniformLocation(m_RendererID, name.c_str());
    if (location == -1) {
        ENGINE_CORE_WARN("Shader ID {}: Uniform '{}' not found or unused", m_RendererID, name);
    }
    m_UniformLocationCache[name] = location;
    return location;
}

void Shader::SetInt(const std::string& name, int value) const {
    ENGINE_CORE_TRACE("Shader ID {} SetInt: {} = {}", m_RendererID, name, value);
    glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetIntArray(const std::string& name, const int* values, uint32_t count) const {
    ENGINE_CORE_TRACE("Shader ID {} SetIntArray: {} (count: {})", m_RendererID, name, count);
    glUniform1iv(GetUniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::SetFloat(const std::string& name, float value) const {
    ENGINE_CORE_TRACE("Shader ID {} SetFloat: {} = {:.3f}", m_RendererID, name, value);
    glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetFloatArray(const std::string& name, const float* values, uint32_t count) const {
    ENGINE_CORE_TRACE("Shader ID {} SetFloatArray: {} (count: {})", m_RendererID, name, count);
    glUniform1fv(GetUniformLocation(name), static_cast<GLsizei>(count), values);
}

void Shader::SetFloat2(const std::string& name, const glm::vec2& value) const {
    ENGINE_CORE_TRACE("Shader ID {} SetFloat2: {} = ({:.2f}, {:.2f})", m_RendererID, name, value.x, value.y);
    glUniform2f(GetUniformLocation(name), value.x, value.y);
}

void Shader::SetFloat3(const std::string& name, const glm::vec3& value) const {
    ENGINE_CORE_TRACE("Shader ID {} SetFloat3: {} = ({:.2f}, {:.2f}, {:.2f})", m_RendererID, name, value.x, value.y, value.z);
    glUniform3f(GetUniformLocation(name), value.x, value.y, value.z);
}

void Shader::SetFloat4(const std::string& name, const glm::vec4& value) const {
    ENGINE_CORE_TRACE("Shader ID {} SetFloat4: {} = ({:.2f}, {:.2f}, {:.2f}, {:.2f})", m_RendererID, name, value.x, value.y, value.z, value.w);
    glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w);
}

void Shader::SetMat4(const std::string& name, const glm::mat4& matrix) const {
    ENGINE_CORE_TRACE("Shader ID {} SetMat4: {}", m_RendererID, name);
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

} // namespace Engine
