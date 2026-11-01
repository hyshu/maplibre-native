#include <mln/test/util.hpp>

#include <mln/command_export/fill_extrusion_vertex_data.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = 12;
constexpr std::size_t outputStride = 44;
constexpr std::size_t baseOffset = 12;
constexpr std::size_t heightOffset = 20;
constexpr std::size_t colorOffset = 28;

void writeFloat(std::vector<std::uint8_t>& data, std::size_t offset, float value) {
    ASSERT_LE(offset + sizeof(value), data.size());
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

float readFloat(const std::vector<std::uint8_t>& data, std::size_t offset) {
    EXPECT_LE(offset + sizeof(float), data.size());
    float value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

std::vector<std::uint8_t> makeLayout(std::size_t vertexCount) {
    std::vector<std::uint8_t> result(vertexCount * layoutStride);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::uint8_t>(i + 1);
    }
    return result;
}

TEST(CommandExportFillExtrusionVertexData, SourceBaseAndConstantHeight) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = 12;
    constexpr std::size_t paintOffset = 4;
    std::vector<std::uint8_t> paint(3 * paintStride);
    writeFloat(paint, paintStride + paintOffset, 3.5f);
    writeFloat(paint, 2 * paintStride + paintOffset, 7.25f);

    const FillExtrusionAttributeData base{
        .data = paint.data(),
        .size = paint.size(),
        .offset = paintOffset,
        .vertexOffset = 1,
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, base, std::nullopt, std::nullopt, 0, 11, output),
              FillExtrusionVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), 2 * outputStride);
    EXPECT_EQ(std::memcmp(output.data(), layout.data(), layoutStride), 0);
    EXPECT_EQ(std::memcmp(output.data() + outputStride, layout.data() + layoutStride, layoutStride), 0);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset), 3.5f);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset + sizeof(float)), 3.5f);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset), 11);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset + sizeof(float)), 11);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + baseOffset), 7.25f);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + baseOffset + sizeof(float)), 7.25f);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(readFloat(output, colorOffset + i * sizeof(float)), 0);
    }
}

TEST(CommandExportFillExtrusionVertexData, ConstantBaseAndSourceHeight) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float));
    writeFloat(paint, 0, 40);
    const FillExtrusionAttributeData height{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, std::nullopt, height, std::nullopt, 4, 0, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset), 4);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset + sizeof(float)), 4);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset), 40);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset + sizeof(float)), 40);
}

TEST(CommandExportFillExtrusionVertexData, BothSourceValuesAreIndependentlyDuplicated) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = sizeof(float) * 2;
    std::vector<std::uint8_t> paint(2 * paintStride);
    writeFloat(paint, 0, 2);
    writeFloat(paint, sizeof(float), 12);
    writeFloat(paint, paintStride, 4);
    writeFloat(paint, paintStride + sizeof(float), 14);
    const FillExtrusionAttributeData base{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float,
    };
    const FillExtrusionAttributeData height{
        .data = paint.data(),
        .size = paint.size(),
        .offset = sizeof(float),
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, base, height, std::nullopt, 0, 0, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset), 2);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset + sizeof(float)), 2);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset), 12);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset + sizeof(float)), 12);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + baseOffset), 4);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + baseOffset + sizeof(float)), 4);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + heightOffset), 14);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + heightOffset + sizeof(float)), 14);
}

TEST(CommandExportFillExtrusionVertexData, CompositeRangesArePreserved) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float) * 4);
    writeFloat(paint, 0, 1);
    writeFloat(paint, sizeof(float), 2);
    writeFloat(paint, sizeof(float) * 2, 20);
    writeFloat(paint, sizeof(float) * 3, 30);
    const FillExtrusionAttributeData base{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float) * 4,
        .type = gfx::AttributeDataType::Float2,
    };
    const FillExtrusionAttributeData height{
        .data = paint.data(),
        .size = paint.size(),
        .offset = sizeof(float) * 2,
        .stride = sizeof(float) * 4,
        .type = gfx::AttributeDataType::Float2,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, base, height, std::nullopt, 0, 0, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset), 1);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset + sizeof(float)), 2);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset), 20);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset + sizeof(float)), 30);
}

TEST(CommandExportFillExtrusionVertexData, SourceColorIsDuplicatedWithConstantBaseAndHeight) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = sizeof(float) * 3;
    constexpr std::size_t paintOffset = sizeof(float);
    std::vector<std::uint8_t> paint(3 * paintStride);
    writeFloat(paint, paintStride + paintOffset, 101);
    writeFloat(paint, paintStride + paintOffset + sizeof(float), 102);
    writeFloat(paint, 2 * paintStride + paintOffset, 201);
    writeFloat(paint, 2 * paintStride + paintOffset + sizeof(float), 202);
    const FillExtrusionAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .offset = paintOffset,
        .vertexOffset = 1,
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float2,
    };

    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, std::nullopt, std::nullopt, color, 3, 12, output),
              FillExtrusionVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), 2 * outputStride);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        const auto offset = vertex * outputStride;
        const auto first = vertex == 0 ? 101.0f : 201.0f;
        const auto second = vertex == 0 ? 102.0f : 202.0f;
        EXPECT_FLOAT_EQ(readFloat(output, offset + baseOffset), 3);
        EXPECT_FLOAT_EQ(readFloat(output, offset + baseOffset + sizeof(float)), 3);
        EXPECT_FLOAT_EQ(readFloat(output, offset + heightOffset), 12);
        EXPECT_FLOAT_EQ(readFloat(output, offset + heightOffset + sizeof(float)), 12);
        EXPECT_FLOAT_EQ(readFloat(output, offset + colorOffset), first);
        EXPECT_FLOAT_EQ(readFloat(output, offset + colorOffset + sizeof(float)), second);
        EXPECT_FLOAT_EQ(readFloat(output, offset + colorOffset + sizeof(float) * 2), first);
        EXPECT_FLOAT_EQ(readFloat(output, offset + colorOffset + sizeof(float) * 3), second);
    }

    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, std::nullopt, std::nullopt, color, 3, 12, output),
              FillExtrusionVertexDataUpdate::Unchanged);
    writeFloat(paint, paintStride + paintOffset, 301);
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, std::nullopt, std::nullopt, color, 3, 12, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, colorOffset), 301);
}

TEST(CommandExportFillExtrusionVertexData, CompositeColorRangeIsPreserved) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float) * 4);
    for (std::size_t i = 0; i < 4; ++i) {
        writeFloat(paint, i * sizeof(float), static_cast<float>(i + 1));
    }
    const FillExtrusionAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, std::nullopt, std::nullopt, color, 4, 20, output),
              FillExtrusionVertexDataUpdate::Changed);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(readFloat(output, colorOffset + i * sizeof(float)), static_cast<float>(i + 1));
    }
}

TEST(CommandExportFillExtrusionVertexData, ReportsChangesOnlyWhenBytesChange) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float));
    writeFloat(paint, 0, 5);
    const FillExtrusionAttributeData base{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, base, std::nullopt, std::nullopt, 0, 10, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, base, std::nullopt, std::nullopt, 0, 10, output),
              FillExtrusionVertexDataUpdate::Unchanged);
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, base, std::nullopt, std::nullopt, 0, 20, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, heightOffset), 20);

    writeFloat(paint, 0, 8);
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, base, std::nullopt, std::nullopt, 0, 20, output),
              FillExtrusionVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, baseOffset), 8);
}

TEST(CommandExportFillExtrusionVertexData, RejectsMalformedInputWithoutChangingOutput) {
    const auto layout = makeLayout(2);
    const std::vector<std::uint8_t> tooShort(sizeof(float));
    const FillExtrusionAttributeData outOfBounds{
        .data = tooShort.data(),
        .size = tooShort.size(),
        .stride = sizeof(float),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output{1, 2, 3};
    const auto original = output;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, outOfBounds, std::nullopt, std::nullopt, 0, 10, output),
              FillExtrusionVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    auto invalidType = outOfBounds;
    invalidType.size = layout.size();
    invalidType.type = gfx::AttributeDataType::UInt;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, invalidType, std::nullopt, std::nullopt, 0, 10, output),
              FillExtrusionVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    std::vector<std::uint8_t> colorPaint(sizeof(float) * 2);
    FillExtrusionAttributeData invalidColor{
        .data = colorPaint.data(),
        .size = colorPaint.size(),
        .stride = colorPaint.size(),
        .type = gfx::AttributeDataType::Float2,
    };
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, std::nullopt, std::nullopt, invalidColor, 0, 10, output),
              FillExtrusionVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    invalidColor.type = gfx::AttributeDataType::Float;
    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 1, std::nullopt, std::nullopt, invalidColor, 0, 10, output),
              FillExtrusionVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    EXPECT_EQ(updateFillExtrusionVertexData(
                  std::span<const std::uint8_t>(layout), 2, std::nullopt, std::nullopt, std::nullopt, 0, 10, output),
              FillExtrusionVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);
}

} // namespace
} // namespace detail
} // namespace command_export
} // namespace mln
