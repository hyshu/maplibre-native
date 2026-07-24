#include <mln/command_export/circle_vertex_data.hpp>

#include <cstring>
#include <limits>
#include <utility>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = sizeof(std::int16_t) * 2;
constexpr std::size_t colorRangeSize = sizeof(float) * 4;
constexpr std::size_t scalarRangeSize = sizeof(float) * 2;
constexpr std::size_t outputStride = layoutStride + colorRangeSize * 2 + scalarRangeSize * 5;

constexpr std::size_t colorOffset = layoutStride;
constexpr std::size_t radiusOffset = colorOffset + colorRangeSize;
constexpr std::size_t blurOffset = radiusOffset + scalarRangeSize;
constexpr std::size_t opacityOffset = blurOffset + scalarRangeSize;
constexpr std::size_t strokeColorOffset = opacityOffset + scalarRangeSize;
constexpr std::size_t strokeWidthOffset = strokeColorOffset + colorRangeSize;
constexpr std::size_t strokeOpacityOffset = strokeWidthOffset + scalarRangeSize;

static_assert(outputStride == 76);
static_assert(strokeOpacityOffset == 68);

bool checkedMultiply(std::size_t a, std::size_t b, std::size_t& result) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    result = a * b;
    return true;
}

bool checkedAdd(std::size_t a, std::size_t b, std::size_t& result) {
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return false;
    }
    result = a + b;
    return true;
}

std::size_t colorAttributeSize(gfx::AttributeDataType type) {
    switch (type) {
        case gfx::AttributeDataType::Float2:
            return sizeof(float) * 2;
        case gfx::AttributeDataType::Float4:
            return sizeof(float) * 4;
        default:
            return 0;
    }
}

std::size_t scalarAttributeSize(gfx::AttributeDataType type) {
    switch (type) {
        case gfx::AttributeDataType::Float:
            return sizeof(float);
        case gfx::AttributeDataType::Float2:
            return sizeof(float) * 2;
        default:
            return 0;
    }
}

bool validateAttribute(const CircleAttributeData& attribute, std::size_t vertexCount, std::size_t elementSize) {
    if (!attribute.data || elementSize == 0 || attribute.stride < elementSize) {
        return false;
    }
    if (attribute.offset > attribute.stride || elementSize > attribute.stride - attribute.offset) {
        return false;
    }

    std::size_t start = 0;
    if (!checkedMultiply(attribute.vertexOffset, attribute.stride, start) ||
        !checkedAdd(start, attribute.offset, start)) {
        return false;
    }

    std::size_t lastVertexOffset = 0;
    if (vertexCount > 0 && !checkedMultiply(vertexCount - 1, attribute.stride, lastVertexOffset)) {
        return false;
    }

    std::size_t requiredSize = 0;
    return checkedAdd(start, lastVertexOffset, requiredSize) && checkedAdd(requiredSize, elementSize, requiredSize) &&
           requiredSize <= attribute.size;
}

const std::uint8_t* attributeValue(const CircleAttributeData& attribute, std::size_t index) {
    return attribute.data + attribute.vertexOffset * attribute.stride + attribute.offset + index * attribute.stride;
}

void copyColorRange(const CircleAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float2) {
        constexpr std::size_t sourceSize = sizeof(float) * 2;
        std::memcpy(destination, source, sourceSize);
        std::memcpy(destination + sourceSize, source, sourceSize);
    } else {
        std::memcpy(destination, source, colorRangeSize);
    }
}

void copyScalarRange(const CircleAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float) {
        std::memcpy(destination, source, sizeof(float));
        std::memcpy(destination + sizeof(float), source, sizeof(float));
    } else {
        std::memcpy(destination, source, scalarRangeSize);
    }
}

} // namespace

CircleVertexDataUpdate updateCircleVertexData(std::span<const std::uint8_t> layoutData,
                                              std::size_t vertexCount,
                                              const CircleVertexAttributes& attributes,
                                              std::vector<std::uint8_t>& output) {
    if (vertexCount == 0 || attributes.empty()) {
        return CircleVertexDataUpdate::Failed;
    }

    std::size_t layoutBytes = 0;
    std::size_t outputBytes = 0;
    if (!checkedMultiply(vertexCount, layoutStride, layoutBytes) || layoutBytes > layoutData.size() ||
        !checkedMultiply(vertexCount, outputStride, outputBytes) ||
        (attributes.color &&
         !validateAttribute(*attributes.color, vertexCount, colorAttributeSize(attributes.color->type))) ||
        (attributes.radius &&
         !validateAttribute(*attributes.radius, vertexCount, scalarAttributeSize(attributes.radius->type))) ||
        (attributes.blur &&
         !validateAttribute(*attributes.blur, vertexCount, scalarAttributeSize(attributes.blur->type))) ||
        (attributes.opacity &&
         !validateAttribute(*attributes.opacity, vertexCount, scalarAttributeSize(attributes.opacity->type))) ||
        (attributes.strokeColor &&
         !validateAttribute(*attributes.strokeColor, vertexCount, colorAttributeSize(attributes.strokeColor->type))) ||
        (attributes.strokeWidth &&
         !validateAttribute(*attributes.strokeWidth, vertexCount, scalarAttributeSize(attributes.strokeWidth->type))) ||
        (attributes.strokeOpacity &&
         !validateAttribute(
             *attributes.strokeOpacity, vertexCount, scalarAttributeSize(attributes.strokeOpacity->type)))) {
        return CircleVertexDataUpdate::Failed;
    }

    std::vector<std::uint8_t> next(outputBytes);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        auto* destination = next.data() + i * outputStride;
        std::memcpy(destination, layoutData.data() + i * layoutStride, layoutStride);
        if (attributes.color) copyColorRange(*attributes.color, i, destination + colorOffset);
        if (attributes.radius) copyScalarRange(*attributes.radius, i, destination + radiusOffset);
        if (attributes.blur) copyScalarRange(*attributes.blur, i, destination + blurOffset);
        if (attributes.opacity) copyScalarRange(*attributes.opacity, i, destination + opacityOffset);
        if (attributes.strokeColor) copyColorRange(*attributes.strokeColor, i, destination + strokeColorOffset);
        if (attributes.strokeWidth) copyScalarRange(*attributes.strokeWidth, i, destination + strokeWidthOffset);
        if (attributes.strokeOpacity) copyScalarRange(*attributes.strokeOpacity, i, destination + strokeOpacityOffset);
    }

    if (next == output) {
        return CircleVertexDataUpdate::Unchanged;
    }
    output = std::move(next);
    return CircleVertexDataUpdate::Changed;
}

} // namespace detail
} // namespace command_export
} // namespace mln
