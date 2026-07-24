#include <mln/test/util.hpp>

#include <mln/command_export/line_vertex_data.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = 8;
constexpr std::size_t outputStride = 88;
constexpr std::size_t patternFromOffset = 72;
constexpr std::size_t patternToOffset = 80;

struct NumericPropertyInfo {
    const char* name;
    std::size_t outputOffset;
    bool color;
};

constexpr std::array<NumericPropertyInfo, 7> numericProperties{{
    {"color", 8, true},
    {"blur", 24, false},
    {"opacity", 32, false},
    {"gap-width", 40, false},
    {"offset", 48, false},
    {"width", 56, false},
    {"floor-width", 64, false},
}};

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

void writeUShort(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    ASSERT_LE(offset + sizeof(value), data.size());
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::uint16_t readUShort(const std::vector<std::uint8_t>& data, std::size_t offset) {
    EXPECT_LE(offset + sizeof(std::uint16_t), data.size());
    std::uint16_t value = 0;
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

std::optional<LineAttributeData>& numericAttributeAt(LineVertexAttributes& attributes, std::size_t index) {
    switch (index) {
        case 0:
            return attributes.color;
        case 1:
            return attributes.blur;
        case 2:
            return attributes.opacity;
        case 3:
            return attributes.gapWidth;
        case 4:
            return attributes.offset;
        case 5:
            return attributes.width;
        case 6:
            return attributes.floorWidth;
        default:
            std::abort();
    }
}

void expectOtherDataZero(const std::vector<std::uint8_t>& output,
                         std::size_t vertex,
                         std::optional<std::size_t> populatedProperty) {
    for (std::size_t property = 0; property < numericProperties.size(); ++property) {
        if (populatedProperty && property == *populatedProperty) continue;
        const auto count = numericProperties[property].color ? 4u : 2u;
        for (std::size_t component = 0; component < count; ++component) {
            EXPECT_FLOAT_EQ(
                readFloat(output,
                          vertex * outputStride + numericProperties[property].outputOffset + component * sizeof(float)),
                0)
                << numericProperties[property].name;
        }
    }
    for (std::size_t byte = patternFromOffset; byte < outputStride; ++byte) {
        EXPECT_EQ(output[vertex * outputStride + byte], 0);
    }
}

TEST(CommandExportLineVertexData, DuplicatesEveryNumericSourceEncodingIntoItsZoomRange) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = 24;
    constexpr std::size_t paintOffset = 8;

    for (std::size_t property = 0; property < numericProperties.size(); ++property) {
        SCOPED_TRACE(numericProperties[property].name);
        std::vector<std::uint8_t> paint(3 * paintStride);
        const auto components = numericProperties[property].color ? 2u : 1u;
        for (std::size_t vertex = 0; vertex < 2; ++vertex) {
            for (std::size_t component = 0; component < components; ++component) {
                writeFloat(paint,
                           (vertex + 1) * paintStride + paintOffset + component * sizeof(float),
                           static_cast<float>(100 * (property + 1) + 10 * vertex + component + 1));
            }
        }

        LineVertexAttributes attributes;
        numericAttributeAt(attributes, property) = LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = paintOffset,
            .vertexOffset = 1,
            .stride = paintStride,
            .type = numericProperties[property].color ? gfx::AttributeDataType::Float2 : gfx::AttributeDataType::Float,
        };

        std::vector<std::uint8_t> output;
        EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
                  LineVertexDataUpdate::Changed);
        ASSERT_EQ(output.size(), 2 * outputStride);
        for (std::size_t vertex = 0; vertex < 2; ++vertex) {
            EXPECT_EQ(
                std::memcmp(output.data() + vertex * outputStride, layout.data() + vertex * layoutStride, layoutStride),
                0);
            const auto first = static_cast<float>(100 * (property + 1) + 10 * vertex + 1);
            if (numericProperties[property].color) {
                const auto second = first + 1;
                EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + numericProperties[property].outputOffset),
                                first);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + numericProperties[property].outputOffset + sizeof(float)),
                    second);
                EXPECT_FLOAT_EQ(
                    readFloat(output,
                              vertex * outputStride + numericProperties[property].outputOffset + 2 * sizeof(float)),
                    first);
                EXPECT_FLOAT_EQ(
                    readFloat(output,
                              vertex * outputStride + numericProperties[property].outputOffset + 3 * sizeof(float)),
                    second);
            } else {
                EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + numericProperties[property].outputOffset),
                                first);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + numericProperties[property].outputOffset + sizeof(float)),
                    first);
            }
            expectOtherDataZero(output, vertex, property);
        }
    }
}

TEST(CommandExportLineVertexData, PreservesEveryNumericCompositeZoomRange) {
    const auto layout = makeLayout(1);

    for (std::size_t property = 0; property < numericProperties.size(); ++property) {
        SCOPED_TRACE(numericProperties[property].name);
        const auto components = numericProperties[property].color ? 4u : 2u;
        std::vector<std::uint8_t> paint(components * sizeof(float));
        for (std::size_t component = 0; component < components; ++component) {
            writeFloat(paint, component * sizeof(float), static_cast<float>(10 * property + component + 1));
        }

        LineVertexAttributes attributes;
        numericAttributeAt(attributes, property) = LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .stride = paint.size(),
            .type = numericProperties[property].color ? gfx::AttributeDataType::Float4 : gfx::AttributeDataType::Float2,
        };

        std::vector<std::uint8_t> output;
        EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
                  LineVertexDataUpdate::Changed);
        ASSERT_EQ(output.size(), outputStride);
        for (std::size_t component = 0; component < components; ++component) {
            EXPECT_FLOAT_EQ(readFloat(output, numericProperties[property].outputOffset + component * sizeof(float)),
                            static_cast<float>(10 * property + component + 1));
        }
        expectOtherDataZero(output, 0, property);
    }
}

TEST(CommandExportLineVertexData, PreservesShaderFacingPatternSlotOrder) {
    const auto layout = makeLayout(2);
    constexpr std::size_t patternStride = 12;
    constexpr std::size_t patternOffset = 2;
    std::vector<std::uint8_t> from(2 * patternStride);
    std::vector<std::uint8_t> to(2 * patternStride);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        for (std::size_t component = 0; component < 4; ++component) {
            writeUShort(from,
                        vertex * patternStride + patternOffset + component * sizeof(std::uint16_t),
                        static_cast<std::uint16_t>(100 + vertex * 10 + component));
            writeUShort(to,
                        vertex * patternStride + patternOffset + component * sizeof(std::uint16_t),
                        static_cast<std::uint16_t>(200 + vertex * 10 + component));
        }
    }

    LineVertexAttributes attributes;
    attributes.patternFrom = LineAttributeData{
        .data = from.data(),
        .size = from.size(),
        .offset = patternOffset,
        .stride = patternStride,
        .type = gfx::AttributeDataType::UShort4,
    };
    attributes.patternTo = LineAttributeData{
        .data = to.data(),
        .size = to.size(),
        .offset = patternOffset,
        .stride = patternStride,
        .type = gfx::AttributeDataType::UShort4,
    };

    std::vector<std::uint8_t> output;
    ASSERT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
              LineVertexDataUpdate::Changed);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        for (std::size_t component = 0; component < 4; ++component) {
            EXPECT_EQ(readUShort(output, vertex * outputStride + patternFromOffset + component * sizeof(std::uint16_t)),
                      100 + vertex * 10 + component);
            EXPECT_EQ(readUShort(output, vertex * outputStride + patternToOffset + component * sizeof(std::uint16_t)),
                      200 + vertex * 10 + component);
        }
        for (const auto& property : numericProperties) {
            const auto components = property.color ? 4u : 2u;
            for (std::size_t component = 0; component < components; ++component) {
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + property.outputOffset + component * sizeof(float)), 0);
            }
        }
    }
}

TEST(CommandExportLineVertexData, ReadsMapLibrePhysicalBinderOrderThroughAttributeOffsets) {
    const auto layout = makeLayout(2);
    // LinePaintProperties::DataDrivenProperties allocates the shared source
    // buffer in this order: blur, color, floor-width, gap-width, offset,
    // opacity, pattern, width. Shader-facing IDs use a different order.
    constexpr std::size_t stride = 40;
    constexpr std::size_t blur = 0;
    constexpr std::size_t color = 4;
    constexpr std::size_t floorWidth = 12;
    constexpr std::size_t gapWidth = 16;
    constexpr std::size_t offset = 20;
    constexpr std::size_t opacity = 24;
    constexpr std::size_t pattern = 28;
    constexpr std::size_t width = 36;
    std::vector<std::uint8_t> paint(3 * stride);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        const auto base = (vertex + 1) * stride;
        writeFloat(paint, base + blur, 11 + vertex);
        writeFloat(paint, base + color, 21 + vertex);
        writeFloat(paint, base + color + sizeof(float), 31 + vertex);
        writeFloat(paint, base + floorWidth, 41 + vertex);
        writeFloat(paint, base + gapWidth, 51 + vertex);
        writeFloat(paint, base + offset, 61 + vertex);
        writeFloat(paint, base + opacity, 71 + vertex);
        for (std::size_t component = 0; component < 4; ++component) {
            writeUShort(paint,
                        base + pattern + component * sizeof(std::uint16_t),
                        static_cast<std::uint16_t>(80 + vertex * 10 + component));
        }
        writeFloat(paint, base + width, 91 + vertex);
    }

    const auto scalarAttribute = [&](std::size_t attributeOffset) {
        return LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = attributeOffset,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::Float,
        };
    };
    const auto patternAttribute = [&] {
        return LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = pattern,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::UShort4,
        };
    };
    const LineVertexAttributes attributes{
        .color =
            LineAttributeData{
                .data = paint.data(),
                .size = paint.size(),
                .offset = color,
                .vertexOffset = 1,
                .stride = stride,
                .type = gfx::AttributeDataType::Float2,
            },
        .blur = scalarAttribute(blur),
        .opacity = scalarAttribute(opacity),
        .gapWidth = scalarAttribute(gapWidth),
        .offset = scalarAttribute(offset),
        .width = scalarAttribute(width),
        .floorWidth = scalarAttribute(floorWidth),
        // The current interleaved binder exposes both fixed pattern IDs from
        // the same physical atlas-rectangle slot.
        .patternFrom = patternAttribute(),
        .patternTo = patternAttribute(),
    };

    std::vector<std::uint8_t> output;
    ASSERT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
              LineVertexDataUpdate::Changed);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 8), 21 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 24), 11 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 32), 71 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 40), 51 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 48), 61 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 56), 91 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + 64), 41 + vertex);
        for (std::size_t component = 0; component < 4; ++component) {
            const auto expected = static_cast<std::uint16_t>(80 + vertex * 10 + component);
            EXPECT_EQ(readUShort(output, vertex * outputStride + patternFromOffset + component * sizeof(std::uint16_t)),
                      expected);
            EXPECT_EQ(readUShort(output, vertex * outputStride + patternToOffset + component * sizeof(std::uint16_t)),
                      expected);
        }
    }
}

TEST(CommandExportLineVertexData, ReadsMapLibreCompositeBinderOrderThroughAttributeOffsets) {
    const auto layout = makeLayout(1);
    // Composite numeric attributes double their components but retain the
    // DataDrivenProperties order used by the interleaved shared buffer.
    constexpr std::size_t stride = 72;
    constexpr std::size_t blur = 0;
    constexpr std::size_t color = 8;
    constexpr std::size_t floorWidth = 24;
    constexpr std::size_t gapWidth = 32;
    constexpr std::size_t offset = 40;
    constexpr std::size_t opacity = 48;
    constexpr std::size_t pattern = 56;
    constexpr std::size_t width = 64;
    std::vector<std::uint8_t> paint(2 * stride);
    const auto base = stride;

    const auto writeRange = [&](std::size_t attributeOffset, float first) {
        writeFloat(paint, base + attributeOffset, first);
        writeFloat(paint, base + attributeOffset + sizeof(float), first + 1);
    };
    writeRange(blur, 11);
    for (std::size_t component = 0; component < 4; ++component) {
        writeFloat(paint, base + color + component * sizeof(float), 21 + component);
    }
    writeRange(floorWidth, 31);
    writeRange(gapWidth, 41);
    writeRange(offset, 51);
    writeRange(opacity, 61);
    for (std::size_t component = 0; component < 4; ++component) {
        writeUShort(
            paint, base + pattern + component * sizeof(std::uint16_t), static_cast<std::uint16_t>(71 + component));
    }
    writeRange(width, 81);

    const auto scalarAttribute = [&](std::size_t attributeOffset) {
        return LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = attributeOffset,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::Float2,
        };
    };
    const auto patternAttribute = [&] {
        return LineAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = pattern,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::UShort4,
        };
    };
    const LineVertexAttributes attributes{
        .color =
            LineAttributeData{
                .data = paint.data(),
                .size = paint.size(),
                .offset = color,
                .vertexOffset = 1,
                .stride = stride,
                .type = gfx::AttributeDataType::Float4,
            },
        .blur = scalarAttribute(blur),
        .opacity = scalarAttribute(opacity),
        .gapWidth = scalarAttribute(gapWidth),
        .offset = scalarAttribute(offset),
        .width = scalarAttribute(width),
        .floorWidth = scalarAttribute(floorWidth),
        .patternFrom = patternAttribute(),
        .patternTo = patternAttribute(),
    };

    std::vector<std::uint8_t> output;
    ASSERT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Changed);
    for (std::size_t component = 0; component < 4; ++component) {
        EXPECT_FLOAT_EQ(readFloat(output, 8 + component * sizeof(float)), 21 + component);
    }
    for (std::size_t property = 1; property < numericProperties.size(); ++property) {
        constexpr std::array<float, 6> expectedFirst{{11, 61, 41, 51, 81, 31}};
        EXPECT_FLOAT_EQ(readFloat(output, numericProperties[property].outputOffset), expectedFirst[property - 1]);
        EXPECT_FLOAT_EQ(readFloat(output, numericProperties[property].outputOffset + sizeof(float)),
                        expectedFirst[property - 1] + 1);
    }
    for (std::size_t component = 0; component < 4; ++component) {
        EXPECT_EQ(readUShort(output, patternFromOffset + component * sizeof(std::uint16_t)), 71 + component);
        EXPECT_EQ(readUShort(output, patternToOffset + component * sizeof(std::uint16_t)), 71 + component);
    }
}

TEST(CommandExportLineVertexData, ReportsFeatureStateChangesOnlyWhenNormalizedBytesChange) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float));
    writeFloat(paint, 0, 4);
    LineVertexAttributes attributes;
    attributes.width = LineAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;

    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Changed);
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Unchanged);
    writeFloat(paint, 0, 9);
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, 56), 9);
    EXPECT_FLOAT_EQ(readFloat(output, 60), 9);
}

TEST(CommandExportLineVertexData, RejectsMalformedInputWithoutChangingOutput) {
    const auto layout = makeLayout(2);
    std::vector<std::uint8_t> paint(sizeof(float) * 4);
    LineVertexAttributes attributes;
    attributes.color = LineAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    std::vector<std::uint8_t> output{1, 2, 3};
    const auto original = output;

    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->type = gfx::AttributeDataType::UInt4;
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->type = gfx::AttributeDataType::Float2;
    attributes.color->stride = sizeof(float);
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->stride = sizeof(float) * 2;
    attributes.color->vertexOffset = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    LineVertexAttributes invalidScalar;
    invalidScalar.opacity = LineAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, invalidScalar, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    const auto shortLayout = std::span<const std::uint8_t>(layout).first(layoutStride - 1);
    LineVertexAttributes validScalar;
    validScalar.blur = LineAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float),
        .type = gfx::AttributeDataType::Float,
    };
    EXPECT_EQ(updateLineVertexData(shortLayout, 1, validScalar, output), LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    std::vector<std::uint8_t> pattern(sizeof(std::uint16_t) * 4);
    LineVertexAttributes incompletePattern;
    incompletePattern.patternFrom = LineAttributeData{
        .data = pattern.data(),
        .size = pattern.size(),
        .stride = pattern.size(),
        .type = gfx::AttributeDataType::UShort4,
    };
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, incompletePattern, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    incompletePattern.patternTo = *incompletePattern.patternFrom;
    incompletePattern.patternTo->type = gfx::AttributeDataType::UShort8;
    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, incompletePattern, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    EXPECT_EQ(updateLineVertexData(std::span<const std::uint8_t>(layout), 1, LineVertexAttributes{}, output),
              LineVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);
}

} // namespace
} // namespace detail
} // namespace command_export
} // namespace mln
