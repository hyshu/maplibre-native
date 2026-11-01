#include <mln/test/util.hpp>

#include <mln/command_export/fill_vertex_data.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = 4;
constexpr std::size_t outputStride = 28;
constexpr std::size_t colorOffset = 4;
constexpr std::size_t opacityOffset = 20;
constexpr std::size_t outlineLayoutStride = 8;
constexpr std::size_t outlineOutputStride = 32;
constexpr std::size_t outlineColorOffset = 8;
constexpr std::size_t outlineOpacityOffset = 24;

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

TEST(CommandExportFillVertexData, SourceValuesAreDuplicatedFromInterleavedAttributes) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = sizeof(float) * 4;
    constexpr std::size_t colorPaintOffset = sizeof(float);
    constexpr std::size_t opacityPaintOffset = sizeof(float) * 3;
    std::vector<std::uint8_t> paint(3 * paintStride);

    writeFloat(paint, paintStride + colorPaintOffset, 101);
    writeFloat(paint, paintStride + colorPaintOffset + sizeof(float), 102);
    writeFloat(paint, paintStride + opacityPaintOffset, 0.25f);
    writeFloat(paint, 2 * paintStride + colorPaintOffset, 201);
    writeFloat(paint, 2 * paintStride + colorPaintOffset + sizeof(float), 202);
    writeFloat(paint, 2 * paintStride + opacityPaintOffset, 0.75f);

    const FillAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .offset = colorPaintOffset,
        .vertexOffset = 1,
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float2,
    };
    const FillAttributeData opacity{
        .data = paint.data(),
        .size = paint.size(),
        .offset = opacityPaintOffset,
        .vertexOffset = 1,
        .stride = paintStride,
        .type = gfx::AttributeDataType::Float,
    };

    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 2, color, opacity, output),
              FillVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), 2 * outputStride);
    EXPECT_EQ(std::memcmp(output.data(), layout.data(), layoutStride), 0);
    EXPECT_EQ(std::memcmp(output.data() + outputStride, layout.data() + layoutStride, layoutStride), 0);

    EXPECT_FLOAT_EQ(readFloat(output, colorOffset), 101);
    EXPECT_FLOAT_EQ(readFloat(output, colorOffset + sizeof(float)), 102);
    EXPECT_FLOAT_EQ(readFloat(output, colorOffset + sizeof(float) * 2), 101);
    EXPECT_FLOAT_EQ(readFloat(output, colorOffset + sizeof(float) * 3), 102);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset), 0.25f);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset + sizeof(float)), 0.25f);

    EXPECT_FLOAT_EQ(readFloat(output, outputStride + colorOffset), 201);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + colorOffset + sizeof(float)), 202);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + colorOffset + sizeof(float) * 2), 201);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + colorOffset + sizeof(float) * 3), 202);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + opacityOffset), 0.75f);
    EXPECT_FLOAT_EQ(readFloat(output, outputStride + opacityOffset + sizeof(float)), 0.75f);
}

TEST(CommandExportFillVertexData, CompositeRangesArePreserved) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float) * 6);
    for (std::size_t i = 0; i < 6; ++i) {
        writeFloat(paint, i * sizeof(float), static_cast<float>(i + 1));
    }
    const FillAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    const FillAttributeData opacity{
        .data = paint.data(),
        .size = paint.size(),
        .offset = sizeof(float) * 4,
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float2,
    };

    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, color, opacity, output),
              FillVertexDataUpdate::Changed);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(readFloat(output, colorOffset + i * sizeof(float)), static_cast<float>(i + 1));
    }
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset), 5);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset + sizeof(float)), 6);
}

TEST(CommandExportFillVertexData, TriangulatedOutlinePreservesLineLayoutAndPaintRanges) {
    std::vector<std::uint8_t> layout(outlineLayoutStride);
    for (std::size_t i = 0; i < layout.size(); ++i) {
        layout[i] = static_cast<std::uint8_t>(31 + i);
    }

    std::vector<std::uint8_t> sourcePaint(sizeof(float) * 3);
    writeFloat(sourcePaint, 0, 11);
    writeFloat(sourcePaint, sizeof(float), 12);
    writeFloat(sourcePaint, sizeof(float) * 2, 0.4f);
    const FillAttributeData sourceOutlineColor{
        .data = sourcePaint.data(),
        .size = sourcePaint.size(),
        .stride = sourcePaint.size(),
        .type = gfx::AttributeDataType::Float2,
    };
    const FillAttributeData sourceOpacity{
        .data = sourcePaint.data(),
        .size = sourcePaint.size(),
        .offset = sizeof(float) * 2,
        .stride = sourcePaint.size(),
        .type = gfx::AttributeDataType::Float,
    };

    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillOutlineTriangulatedVertexData(
                  std::span<const std::uint8_t>(layout), 1, sourceOutlineColor, sourceOpacity, output),
              FillVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), outlineOutputStride);
    EXPECT_EQ(std::memcmp(output.data(), layout.data(), outlineLayoutStride), 0);
    EXPECT_FLOAT_EQ(readFloat(output, outlineColorOffset), 11);
    EXPECT_FLOAT_EQ(readFloat(output, outlineColorOffset + sizeof(float)), 12);
    EXPECT_FLOAT_EQ(readFloat(output, outlineColorOffset + sizeof(float) * 2), 11);
    EXPECT_FLOAT_EQ(readFloat(output, outlineColorOffset + sizeof(float) * 3), 12);
    EXPECT_FLOAT_EQ(readFloat(output, outlineOpacityOffset), 0.4f);
    EXPECT_FLOAT_EQ(readFloat(output, outlineOpacityOffset + sizeof(float)), 0.4f);
    EXPECT_EQ(updateFillOutlineTriangulatedVertexData(
                  std::span<const std::uint8_t>(layout), 1, sourceOutlineColor, sourceOpacity, output),
              FillVertexDataUpdate::Unchanged);

    std::vector<std::uint8_t> compositePaint(sizeof(float) * 6);
    for (std::size_t i = 0; i < 6; ++i) {
        writeFloat(compositePaint, i * sizeof(float), static_cast<float>(21 + i));
    }
    const FillAttributeData compositeOutlineColor{
        .data = compositePaint.data(),
        .size = compositePaint.size(),
        .stride = compositePaint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    const FillAttributeData compositeOpacity{
        .data = compositePaint.data(),
        .size = compositePaint.size(),
        .offset = sizeof(float) * 4,
        .stride = compositePaint.size(),
        .type = gfx::AttributeDataType::Float2,
    };

    EXPECT_EQ(updateFillOutlineTriangulatedVertexData(
                  std::span<const std::uint8_t>(layout), 1, compositeOutlineColor, compositeOpacity, output),
              FillVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), outlineOutputStride);
    EXPECT_EQ(std::memcmp(output.data(), layout.data(), outlineLayoutStride), 0);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(readFloat(output, outlineColorOffset + i * sizeof(float)), static_cast<float>(21 + i));
    }
    EXPECT_FLOAT_EQ(readFloat(output, outlineOpacityOffset), 25);
    EXPECT_FLOAT_EQ(readFloat(output, outlineOpacityOffset + sizeof(float)), 26);
}

TEST(CommandExportFillVertexData, TriangulatedOutlineRejectsMalformedLayoutWithoutChangingOutput) {
    std::vector<std::uint8_t> shortLayout(outlineLayoutStride - 1);
    std::vector<std::uint8_t> paint(sizeof(float) * 2);
    const FillAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float2,
    };
    std::vector<std::uint8_t> output{7, 8, 9};
    const auto original = output;

    EXPECT_EQ(updateFillOutlineTriangulatedVertexData(
                  std::span<const std::uint8_t>(shortLayout), 1, color, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    std::vector<std::uint8_t> layout(outlineLayoutStride);
    EXPECT_EQ(updateFillOutlineTriangulatedVertexData(std::span<const std::uint8_t>(layout),
                                                      std::numeric_limits<std::size_t>::max(),
                                                      color,
                                                      std::nullopt,
                                                      output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);
}

TEST(CommandExportFillVertexData, MissingPropertyRangeIsZeroed) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> colorPaint(sizeof(float) * 2);
    writeFloat(colorPaint, 0, 9);
    writeFloat(colorPaint, sizeof(float), 10);
    const FillAttributeData color{
        .data = colorPaint.data(),
        .size = colorPaint.size(),
        .stride = colorPaint.size(),
        .type = gfx::AttributeDataType::Float2,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, color, std::nullopt, output),
              FillVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset), 0);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset + sizeof(float)), 0);

    std::vector<std::uint8_t> opacityPaint(sizeof(float));
    writeFloat(opacityPaint, 0, 0.5f);
    const FillAttributeData opacity{
        .data = opacityPaint.data(),
        .size = opacityPaint.size(),
        .stride = opacityPaint.size(),
        .type = gfx::AttributeDataType::Float,
    };
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, std::nullopt, opacity, output),
              FillVertexDataUpdate::Changed);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(readFloat(output, colorOffset + i * sizeof(float)), 0);
    }
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset), 0.5f);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset + sizeof(float)), 0.5f);
}

TEST(CommandExportFillVertexData, ReportsChangesOnlyWhenNormalizedBytesChange) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float));
    writeFloat(paint, 0, 0.4f);
    const FillAttributeData opacity{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, std::nullopt, opacity, output),
              FillVertexDataUpdate::Changed);
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, std::nullopt, opacity, output),
              FillVertexDataUpdate::Unchanged);
    writeFloat(paint, 0, 0.6f);
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, std::nullopt, opacity, output),
              FillVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, opacityOffset), 0.6f);
}

TEST(CommandExportFillVertexData, RejectsMalformedInputWithoutChangingOutput) {
    const auto layout = makeLayout(2);
    std::vector<std::uint8_t> paint(sizeof(float) * 2);
    FillAttributeData color{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float) * 2,
        .type = gfx::AttributeDataType::Float2,
    };
    std::vector<std::uint8_t> output{1, 2, 3};
    const auto original = output;

    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 2, color, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    color.size = layout.size();
    color.type = gfx::AttributeDataType::UInt;
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 2, color, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    color.type = gfx::AttributeDataType::Float2;
    color.stride = sizeof(float);
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, color, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    color.stride = sizeof(float) * 2;
    color.vertexOffset = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, color, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    EXPECT_EQ(updateFillVertexData(std::span<const std::uint8_t>(layout), 1, std::nullopt, std::nullopt, output),
              FillVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);
}

} // namespace
} // namespace detail
} // namespace command_export
} // namespace mln
