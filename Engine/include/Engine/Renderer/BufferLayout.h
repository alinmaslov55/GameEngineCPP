#ifndef RENDERER_BUFFERLAYOUT_H
#define RENDERER_BUFFERLAYOUT_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace Engine {

enum class ShaderDataType {
    None = 0,
    Float, Float2, Float3, Float4,
    Mat3, Mat4,
    Int, Int2, Int3, Int4,
    Bool
};

[[nodiscard]] constexpr inline uint32_t ShaderDataTypeSize(ShaderDataType type) noexcept {
    switch (type) {
        case ShaderDataType::Float:  return 4;
        case ShaderDataType::Float2: return 4 * 2;
        case ShaderDataType::Float3: return 4 * 3;
        case ShaderDataType::Float4: return 4 * 4;
        case ShaderDataType::Mat3:   return 4 * 3 * 3;
        case ShaderDataType::Mat4:   return 4 * 4 * 4;
        case ShaderDataType::Int:    return 4;
        case ShaderDataType::Int2:   return 4 * 2;
        case ShaderDataType::Int3:   return 4 * 3;
        case ShaderDataType::Int4:   return 4 * 4;
        case ShaderDataType::Bool:   return 1;
        case ShaderDataType::None:   break;
    }
    return 0;
}

struct BufferElement {
    std::string Name;
    ShaderDataType Type;
    uint32_t Size;
    size_t Offset;
    bool Normalized;

    BufferElement() = default;

    BufferElement(ShaderDataType type, const std::string& name, bool normalized = false)
        : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized) {}

    [[nodiscard]] constexpr uint32_t GetComponentCount() const noexcept {
        switch (Type) {
            case ShaderDataType::Float:  return 1;
            case ShaderDataType::Float2: return 2;
            case ShaderDataType::Float3: return 3;
            case ShaderDataType::Float4: return 4;
            case ShaderDataType::Mat3:   return 3;
            case ShaderDataType::Mat4:   return 4;
            case ShaderDataType::Int:    return 1;
            case ShaderDataType::Int2:   return 2;
            case ShaderDataType::Int3:   return 3;
            case ShaderDataType::Int4:   return 4;
            case ShaderDataType::Bool:   return 1;
            case ShaderDataType::None:   break;
        }
        return 0;
    }
};

class BufferLayout {
public:
    BufferLayout() = default;

    BufferLayout(std::initializer_list<BufferElement> elements)
        : m_Elements(elements) {
        CalculateOffsetsAndStride();
    }

    [[nodiscard]] uint32_t GetStride() const noexcept { return m_Stride; }
    [[nodiscard]] const std::vector<BufferElement>& GetElements() const noexcept { return m_Elements; }

    [[nodiscard]] std::vector<BufferElement>::iterator begin() noexcept { return m_Elements.begin(); }
    [[nodiscard]] std::vector<BufferElement>::iterator end() noexcept { return m_Elements.end(); }
    [[nodiscard]] std::vector<BufferElement>::const_iterator begin() const noexcept { return m_Elements.begin(); }
    [[nodiscard]] std::vector<BufferElement>::const_iterator end() const noexcept { return m_Elements.end(); }

private:
    void CalculateOffsetsAndStride() {
        size_t offset = 0;
        m_Stride = 0;
        for (auto& element : m_Elements) {
            element.Offset = offset;
            offset += element.Size;
            m_Stride += element.Size;
        }
    }

private:
    std::vector<BufferElement> m_Elements;
    uint32_t m_Stride = 0;
};

} // namespace Engine

#endif // RENDERER_BUFFERLAYOUT_H
