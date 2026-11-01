#include <mln/test/util.hpp>

#include <mln/command_export/circle_vertex_data.hpp>

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

constexpr std::size_t layoutStride = 4;
constexpr std::size_t outputStride = 76;

struct PropertyInfo {
    const char* name;
    std::size_t outputOffset;
    bool color;
};

constexpr std::array<PropertyInfo, 7> properties{{
    {"color", 4, true},
    {"radius", 20, false},
    {"blur", 28, false},
    {"opacity", 36, false},
    {"stroke-color", 44, true},
    {"stroke-width", 60, false},
    {"stroke-opacity", 68, false},
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

std::vector<std::uint8_t> makeLayout(std::size_t vertexCount) {
    std::vector<std::uint8_t> result(vertexCount * layoutStride);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::uint8_t>(i + 1);
    }
    return result;
}

std::optional<CircleAttributeData>& attributeAt(CircleVertexAttributes& attributes, std::size_t index) {
    switch (index) {
        case 0:
            return attributes.color;
        case 1:
            return attributes.radius;
        case 2:
            return attributes.blur;
        case 3:
            return attributes.opacity;
        case 4:
            return attributes.strokeColor;
        case 5:
            return attributes.strokeWidth;
        case 6:
            return attributes.strokeOpacity;
        default:
            std::abort();
    }
}

void expectOtherRangesZero(const std::vector<std::uint8_t>& output, std::size_t vertex, std::size_t populatedProperty) {
    for (std::size_t property = 0; property < properties.size(); ++property) {
        if (property == populatedProperty) continue;
        const auto count = properties[property].color ? 4u : 2u;
        for (std::size_t component = 0; component < count; ++component) {
            EXPECT_FLOAT_EQ(
                readFloat(output,
                          vertex * outputStride + properties[property].outputOffset + component * sizeof(float)),
                0)
                << properties[property].name;
        }
    }
}

TEST(CommandExportCircleVertexData, DuplicatesEverySourceEncodingIntoItsZoomRange) {
    const auto layout = makeLayout(2);
    constexpr std::size_t paintStride = 24;
    constexpr std::size_t paintOffset = 8;

    for (std::size_t property = 0; property < properties.size(); ++property) {
        SCOPED_TRACE(properties[property].name);
        std::vector<std::uint8_t> paint(3 * paintStride);
        const auto components = properties[property].color ? 2u : 1u;
        for (std::size_t vertex = 0; vertex < 2; ++vertex) {
            for (std::size_t component = 0; component < components; ++component) {
                writeFloat(paint,
                           (vertex + 1) * paintStride + paintOffset + component * sizeof(float),
                           static_cast<float>(100 * (property + 1) + 10 * vertex + component + 1));
            }
        }

        CircleVertexAttributes attributes;
        attributeAt(attributes, property) = CircleAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = paintOffset,
            .vertexOffset = 1,
            .stride = paintStride,
            .type = properties[property].color ? gfx::AttributeDataType::Float2 : gfx::AttributeDataType::Float,
        };

        std::vector<std::uint8_t> output;
        EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
                  CircleVertexDataUpdate::Changed);
        ASSERT_EQ(output.size(), 2 * outputStride);
        for (std::size_t vertex = 0; vertex < 2; ++vertex) {
            EXPECT_EQ(
                std::memcmp(output.data() + vertex * outputStride, layout.data() + vertex * layoutStride, layoutStride),
                0);
            const auto first = static_cast<float>(100 * (property + 1) + 10 * vertex + 1);
            if (properties[property].color) {
                const auto second = first + 1;
                EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[property].outputOffset), first);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + properties[property].outputOffset + sizeof(float)),
                    second);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + properties[property].outputOffset + 2 * sizeof(float)),
                    first);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + properties[property].outputOffset + 3 * sizeof(float)),
                    second);
            } else {
                EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[property].outputOffset), first);
                EXPECT_FLOAT_EQ(
                    readFloat(output, vertex * outputStride + properties[property].outputOffset + sizeof(float)),
                    first);
            }
            expectOtherRangesZero(output, vertex, property);
        }
    }
}

TEST(CommandExportCircleVertexData, PreservesEveryCompositeZoomRange) {
    const auto layout = makeLayout(1);

    for (std::size_t property = 0; property < properties.size(); ++property) {
        SCOPED_TRACE(properties[property].name);
        const auto components = properties[property].color ? 4u : 2u;
        std::vector<std::uint8_t> paint(components * sizeof(float));
        for (std::size_t component = 0; component < components; ++component) {
            writeFloat(paint, component * sizeof(float), static_cast<float>(10 * property + component + 1));
        }

        CircleVertexAttributes attributes;
        attributeAt(attributes, property) = CircleAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .stride = paint.size(),
            .type = properties[property].color ? gfx::AttributeDataType::Float4 : gfx::AttributeDataType::Float2,
        };

        std::vector<std::uint8_t> output;
        EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
                  CircleVertexDataUpdate::Changed);
        ASSERT_EQ(output.size(), outputStride);
        for (std::size_t component = 0; component < components; ++component) {
            EXPECT_FLOAT_EQ(readFloat(output, properties[property].outputOffset + component * sizeof(float)),
                            static_cast<float>(10 * property + component + 1));
        }
        expectOtherRangesZero(output, 0, property);
    }
}

TEST(CommandExportCircleVertexData, ReadsMapLibrePhysicalBinderOrderThroughAttributeOffsets) {
    const auto layout = makeLayout(2);
    // CirclePaintProperties::DataDrivenProperties allocates the shared source
    // buffer in this order, which intentionally differs from shader IDs:
    // blur, color, opacity, radius, stroke-color, stroke-opacity, stroke-width.
    constexpr std::size_t stride = 36;
    constexpr std::size_t blur = 0;
    constexpr std::size_t color = 4;
    constexpr std::size_t opacity = 12;
    constexpr std::size_t radius = 16;
    constexpr std::size_t strokeColor = 20;
    constexpr std::size_t strokeOpacity = 28;
    constexpr std::size_t strokeWidth = 32;
    std::vector<std::uint8_t> paint(3 * stride);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        const auto base = (vertex + 1) * stride;
        writeFloat(paint, base + blur, static_cast<float>(11 + vertex));
        writeFloat(paint, base + color, static_cast<float>(21 + vertex));
        writeFloat(paint, base + color + sizeof(float), static_cast<float>(31 + vertex));
        writeFloat(paint, base + opacity, static_cast<float>(41 + vertex));
        writeFloat(paint, base + radius, static_cast<float>(51 + vertex));
        writeFloat(paint, base + strokeColor, static_cast<float>(61 + vertex));
        writeFloat(paint, base + strokeColor + sizeof(float), static_cast<float>(71 + vertex));
        writeFloat(paint, base + strokeOpacity, static_cast<float>(81 + vertex));
        writeFloat(paint, base + strokeWidth, static_cast<float>(91 + vertex));
    }

    const auto scalarAttribute = [&](std::size_t offset) {
        return CircleAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = offset,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::Float,
        };
    };
    const auto colorAttribute = [&](std::size_t offset) {
        return CircleAttributeData{
            .data = paint.data(),
            .size = paint.size(),
            .offset = offset,
            .vertexOffset = 1,
            .stride = stride,
            .type = gfx::AttributeDataType::Float2,
        };
    };
    const CircleVertexAttributes attributes{
        .color = colorAttribute(color),
        .radius = scalarAttribute(radius),
        .blur = scalarAttribute(blur),
        .opacity = scalarAttribute(opacity),
        .strokeColor = colorAttribute(strokeColor),
        .strokeWidth = scalarAttribute(strokeWidth),
        .strokeOpacity = scalarAttribute(strokeOpacity),
    };

    std::vector<std::uint8_t> output;
    ASSERT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
              CircleVertexDataUpdate::Changed);
    ASSERT_EQ(output.size(), 2 * outputStride);
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[0].outputOffset), 21 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[1].outputOffset), 51 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[2].outputOffset), 11 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[3].outputOffset), 41 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[4].outputOffset), 61 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[5].outputOffset), 91 + vertex);
        EXPECT_FLOAT_EQ(readFloat(output, vertex * outputStride + properties[6].outputOffset), 81 + vertex);
    }
}

TEST(CommandExportCircleVertexData, ReportsFeatureStateChangesOnlyWhenNormalizedBytesChange) {
    const auto layout = makeLayout(1);
    std::vector<std::uint8_t> paint(sizeof(float));
    writeFloat(paint, 0, 4);
    CircleVertexAttributes attributes;
    attributes.radius = CircleAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float,
    };
    std::vector<std::uint8_t> output;

    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Changed);
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Unchanged);
    writeFloat(paint, 0, 9);
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Changed);
    EXPECT_FLOAT_EQ(readFloat(output, properties[1].outputOffset), 9);
    EXPECT_FLOAT_EQ(readFloat(output, properties[1].outputOffset + sizeof(float)), 9);
}

TEST(CommandExportCircleVertexData, RejectsMalformedInputWithoutChangingOutput) {
    const auto layout = makeLayout(2);
    std::vector<std::uint8_t> paint(sizeof(float) * 2);
    CircleVertexAttributes attributes;
    attributes.color = CircleAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float2,
    };
    std::vector<std::uint8_t> output{1, 2, 3};
    const auto original = output;

    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 2, attributes, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->size = layout.size();
    attributes.color->type = gfx::AttributeDataType::UInt;
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->type = gfx::AttributeDataType::Float2;
    attributes.color->stride = sizeof(float);
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    attributes.color->stride = sizeof(float) * 2;
    attributes.color->vertexOffset = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, attributes, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    CircleVertexAttributes invalidScalar;
    invalidScalar.opacity = CircleAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = paint.size(),
        .type = gfx::AttributeDataType::Float4,
    };
    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, invalidScalar, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    const auto shortLayout = std::span<const std::uint8_t>(layout).first(layoutStride - 1);
    CircleVertexAttributes validScalar;
    validScalar.blur = CircleAttributeData{
        .data = paint.data(),
        .size = paint.size(),
        .stride = sizeof(float),
        .type = gfx::AttributeDataType::Float,
    };
    EXPECT_EQ(updateCircleVertexData(shortLayout, 1, validScalar, output), CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);

    EXPECT_EQ(updateCircleVertexData(std::span<const std::uint8_t>(layout), 1, CircleVertexAttributes{}, output),
              CircleVertexDataUpdate::Failed);
    EXPECT_EQ(output, original);
}

} // namespace
} // namespace detail
} // namespace command_export
} // namespace mln
