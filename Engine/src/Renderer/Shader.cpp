#include "Engine/Renderer/Shader.h"
#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <utility>

namespace Engine {

std::string Shader::ReadFile(const std::string& filepath) {
    std::string result;
    std::ifstream in(filepath, std::ios::in | std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        std::streamoff size = in.tellg();
        if (size > 0) {
            result.resize(static_cast<size_t>(size));
            in.seekg(0, std::ios::beg);
            in.read(&result[0], size);
        } else {
            ENGINE_CORE_ERROR("Could not read from file '{}'", filepath);
        }
    } else {
        ENGINE_CORE_ERROR("Could not open shader file '{}'", filepath);
    }
    return result;
}

std::unordered_map<uint32_t, std::string> Shader::PreProcess(const std::string& source) {
    std::unordered_map<uint32_t, std::string> shaderSources;

    const char* typeToken = "#type";
    size_t typeTokenLength = strlen(typeToken);
    size_t pos = source.find(typeToken, 0);

    while (pos != std::string::npos) {
        size_t eol = source.find_first_of("\r\n", pos);
        if (eol == std::string::npos) {
            ENGINE_CORE_ERROR("Shader Syntax error: missing newline after #type in '{}'", m_FilePath);
            break;
        }

        size_t begin = pos + typeTokenLength + 1;
        std::string type = source.substr(begin, eol - begin);
        type.erase(0, type.find_first_not_of(" \t"));
        type.erase(type.find_last_not_of(" \t\r\n") + 1);

        uint32_t glType = 0;
        if (type == "vertex") {
            glType = GL_VERTEX_SHADER;
        } else if (type == "fragment" || type == "pixel") {
            glType = GL_FRAGMENT_SHADER;
        } else {
            ENGINE_CORE_ERROR("Unknown shader type '{}' in '{}'", type, m_FilePath);
        }

        size_t nextLinePos = source.find_first_not_of("\r\n", eol);
        if (nextLinePos == std::string::npos) {
            ENGINE_CORE_ERROR("Shader Syntax error: no code following #type in '{}'", m_FilePath);
            break;
        }

        pos = source.find(typeToken, nextLinePos);
        if (glType != 0) {
            shaderSources[glType] = (pos == std::string::npos)
                ? source.substr(nextLinePos)
                : source.substr(nextLinePos, pos - nextLinePos);
        }
    }

    return shaderSources;
}

bool Shader::Compile(const std::unordered_map<uint32_t, std::string>& shaderSources) {
    if (shaderSources.empty()) {
        ENGINE_CORE_ERROR("Shader::Compile failed: No valid shader stages provided");
        return false;
    }

    uint32_t program = glCreateProgram();
    std::vector<uint32_t> glShaderIDs;
    glShaderIDs.reserve(shaderSources.size());

    for (const auto& [stage, sourceStr] : shaderSources) {
        uint32_t shader = glCreateShader(stage);
        const char* sourceCStr = sourceStr.c_str();
        glShaderSource(shader, 1, &sourceCStr, nullptr);
        glCompileShader(shader);

        GLint isCompiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
        if (isCompiled == GL_FALSE) {
            GLint maxLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
            std::vector<GLchar> infoLog(maxLength);
            glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);
            glDeleteShader(shader);

            for (uint32_t id : glShaderIDs) {
                glDetachShader(program, id);
                glDeleteShader(id);
            }
            glDeleteProgram(program);

            ENGINE_CORE_ERROR("Shader compilation failed ({}, stage 0x{:X}): {}",
                              m_Name.empty() ? "unnamed" : m_Name, stage, infoLog.data());
            return false;
        }

        glAttachShader(program, shader);
        glShaderIDs.push_back(shader);
    }

    glLinkProgram(program);

    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE) {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

        for (uint32_t id : glShaderIDs) {
            glDetachShader(program, id);
            glDeleteShader(id);
        }
        glDeleteProgram(program);

        ENGINE_CORE_ERROR("Shader link failed ({}): {}",
                          m_Name.empty() ? "unnamed" : m_Name, infoLog.data());
        return false;
    }

    for (uint32_t id : glShaderIDs) {
        glDetachShader(program, id);
        glDeleteShader(id);
    }

    if (m_RendererID != 0) {
        glDeleteProgram(m_RendererID);
    }
    m_RendererID = program;
    m_UniformLocationCache.clear();

    ENGINE_CORE_INFO("Shader program '{}' created successfully with ID: {}",
                     m_Name.empty() ? "unnamed" : m_Name, m_RendererID);
    return true;
}

Shader::Shader(const std::string& filepath)
    : m_FilePath(filepath) {
    m_Name = std::filesystem::path(filepath).stem().string();
    std::string source = ReadFile(filepath);
    if (!source.empty()) {
        auto shaderSources = PreProcess(source);
        Compile(shaderSources);
    }
}

Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc)
    : m_Name("InMemoryShader") {
    std::unordered_map<uint32_t, std::string> sources;
    sources[GL_VERTEX_SHADER] = vertexSrc;
    sources[GL_FRAGMENT_SHADER] = fragmentSrc;
    Compile(sources);
}

Shader::Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
    : m_Name(name) {
    std::unordered_map<uint32_t, std::string> sources;
    sources[GL_VERTEX_SHADER] = vertexSrc;
    sources[GL_FRAGMENT_SHADER] = fragmentSrc;
    Compile(sources);
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
      m_FilePath(std::move(other.m_FilePath)),
      m_Name(std::move(other.m_Name)),
      m_UniformLocationCache(std::move(other.m_UniformLocationCache)) {
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_RendererID != 0) {
            glDeleteProgram(m_RendererID);
        }
        m_RendererID = std::exchange(other.m_RendererID, 0);
        m_FilePath = std::move(other.m_FilePath);
        m_Name = std::move(other.m_Name);
        m_UniformLocationCache = std::move(other.m_UniformLocationCache);
    }
    return *this;
}

bool Shader::Reload() {
    if (m_FilePath.empty()) {
        ENGINE_CORE_WARN("Shader '{}' cannot be reloaded: no file path specified", m_Name);
        return false;
    }
    ENGINE_CORE_INFO("Reloading shader '{}' from '{}'...", m_Name, m_FilePath);
    std::string source = ReadFile(m_FilePath);
    if (source.empty()) {
        ENGINE_CORE_ERROR("Failed to reload shader '{}': file is empty or missing", m_Name);
        return false;
    }
    auto shaderSources = PreProcess(source);
    return Compile(shaderSources);
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
