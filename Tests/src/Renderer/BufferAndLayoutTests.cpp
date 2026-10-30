#include <gtest/gtest.h>
#include "Engine/Renderer/Buffer.h"

namespace {

TEST(BufferLayoutTest, DataTypeSizeCalculations) {
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Float), 4u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Float2), 8u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Float3), 12u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Float4), 16u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Mat3), 36u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Mat4), 64u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Int), 4u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Int2), 8u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Int3), 12u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Int4), 16u);
    EXPECT_EQ(Engine::ShaderDataTypeSize(Engine::ShaderDataType::Bool), 1u);
}

TEST(BufferLayoutTest, ElementOffsetAndStrideCalculation) {
    Engine::BufferLayout layout = {
        { Engine::ShaderDataType::Float3, "a_Position" },
        { Engine::ShaderDataType::Float4, "a_Color" },
        { Engine::ShaderDataType::Float2, "a_TexCoord" },
        { Engine::ShaderDataType::Float,  "a_TexIndex" }
    };

    const auto& elements = layout.GetElements();
    ASSERT_EQ(elements.size(), 4u);

    // a_Position (Float3 = 12 bytes, offset 0)
    EXPECT_EQ(elements[0].Offset, 0u);
    EXPECT_EQ(elements[0].Size, 12u);
    EXPECT_EQ(elements[0].GetComponentCount(), 3u);

    // a_Color (Float4 = 16 bytes, offset 12)
    EXPECT_EQ(elements[1].Offset, 12u);
    EXPECT_EQ(elements[1].Size, 16u);
    EXPECT_EQ(elements[1].GetComponentCount(), 4u);

    // a_TexCoord (Float2 = 8 bytes, offset 28)
    EXPECT_EQ(elements[2].Offset, 28u);
    EXPECT_EQ(elements[2].Size, 8u);
    EXPECT_EQ(elements[2].GetComponentCount(), 2u);

    // a_TexIndex (Float = 4 bytes, offset 36)
    EXPECT_EQ(elements[3].Offset, 36u);
    EXPECT_EQ(elements[3].Size, 4u);
    EXPECT_EQ(elements[3].GetComponentCount(), 1u);

    // Total stride: 12 + 16 + 8 + 4 = 40 bytes
    EXPECT_EQ(layout.GetStride(), 40u);
}

} // namespace

