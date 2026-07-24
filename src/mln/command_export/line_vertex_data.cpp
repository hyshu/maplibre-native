#include <mln/command_export/line_vertex_data.hpp>

#include <cstring>
#include <limits>
#include <utility>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = sizeof(std::int16_t) * 2 + sizeof(std::uint8_t) * 4;
constexpr std::size_t colorRangeSize = sizeof(float) * 4;
constexpr std::size_t scalarRangeSize = sizeof(float) * 2;
constexpr std::size_t patternSize = sizeof(std::uint16_t) * 4;
constexpr std::size_t outputStride = layoutStride + colorRangeSize + scalarRangeSize * 6 + patternSize * 2;

constexpr std::size_t colorOffset = layoutStride;
constexpr std::size_t blurOffset = colorOffset + colorRangeSize;
constexpr std::size_t opacityOffset = blurOffset + scalarRangeSize;
constexpr std::size_t gapWidthOffset = opacityOffset + scalarRangeSize;
constexpr std::size_t offsetOffset = gapWidthOffset + scalarRangeSize;
constexpr std::size_t widthOffset = offsetOffset + scalarRangeSize;
constexpr std::size_t floorWidthOffset = widthOffset + scalarRangeSize;
constexpr std::size_t patternFromOffset = floorWidthOffset + scalarRangeSize;
constexpr std::size_t patternToOffset = patternFromOffset + patternSize;

static_assert(layoutStride == 8);
static_assert(outputStride == 88);
static_assert(colorOffset == 8);
static_assert(blurOffset == 24);
static_assert(opacityOffset == 32);
static_assert(gapWidthOffset == 40);
static_assert(offsetOffset == 48);
static_assert(widthOffset == 56);
static_assert(floorWidthOffset == 64);
static_assert(patternFromOffset == 72);
static_assert(patternToOffset == 80);

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

std::size_t patternAttributeSize(gfx::AttributeDataType type) {
    return type == gfx::AttributeDataType::UShort4 ? patternSize : 0;
}

bool validateAttribute(const LineAttributeData& attribute, std::size_t vertexCount, std::size_t elementSize) {
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

const std::uint8_t* attributeValue(const LineAttributeData& attribute, std::size_t index) {
    return attribute.data + attribute.vertexOffset * attribute.stride + attribute.offset + index * attribute.stride;
}

void copyColorRange(const LineAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float2) {
        constexpr std::size_t sourceSize = sizeof(float) * 2;
        std::memcpy(destination, source, sourceSize);
        std::memcpy(destination + sourceSize, source, sourceSize);
    } else {
        std::memcpy(destination, source, colorRangeSize);
    }
}

void copyScalarRange(const LineAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float) {
        std::memcpy(destination, source, sizeof(float));
        std::memcpy(destination + sizeof(float), source, sizeof(float));
    } else {
        std::memcpy(destination, source, scalarRangeSize);
    }
}

void copyPattern(const LineAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    std::memcpy(destination, attributeValue(attribute, index), patternSize);
}

} // namespace

LineVertexDataUpdate updateLineVertexData(std::span<const std::uint8_t> layoutData,
                                          std::size_t vertexCount,
                                          const LineVertexAttributes& attributes,
                                          std::vector<std::uint8_t>& output) {
    if (vertexCount == 0 || attributes.empty() ||
        attributes.patternFrom.has_value() != attributes.patternTo.has_value()) {
        return LineVertexDataUpdate::Failed;
    }

    std::size_t layoutBytes = 0;
    std::size_t outputBytes = 0;
    if (!checkedMultiply(vertexCount, layoutStride, layoutBytes) || layoutBytes > layoutData.size() ||
        !checkedMultiply(vertexCount, outputStride, outputBytes) ||
        (attributes.color &&
         !validateAttribute(*attributes.color, vertexCount, colorAttributeSize(attributes.color->type))) ||
        (attributes.blur &&
         !validateAttribute(*attributes.blur, vertexCount, scalarAttributeSize(attributes.blur->type))) ||
        (attributes.opacity &&
         !validateAttribute(*attributes.opacity, vertexCount, scalarAttributeSize(attributes.opacity->type))) ||
        (attributes.gapWidth &&
         !validateAttribute(*attributes.gapWidth, vertexCount, scalarAttributeSize(attributes.gapWidth->type))) ||
        (attributes.offset &&
         !validateAttribute(*attributes.offset, vertexCount, scalarAttributeSize(attributes.offset->type))) ||
        (attributes.width &&
         !validateAttribute(*attributes.width, vertexCount, scalarAttributeSize(attributes.width->type))) ||
        (attributes.floorWidth &&
         !validateAttribute(*attributes.floorWidth, vertexCount, scalarAttributeSize(attributes.floorWidth->type))) ||
        (attributes.patternFrom &&
         !validateAttribute(
             *attributes.patternFrom, vertexCount, patternAttributeSize(attributes.patternFrom->type))) ||
        (attributes.patternTo &&
         !validateAttribute(*attributes.patternTo, vertexCount, patternAttributeSize(attributes.patternTo->type)))) {
        return LineVertexDataUpdate::Failed;
    }

    std::vector<std::uint8_t> next(outputBytes);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        auto* destination = next.data() + i * outputStride;
        std::memcpy(destination, layoutData.data() + i * layoutStride, layoutStride);
        if (attributes.color) copyColorRange(*attributes.color, i, destination + colorOffset);
        if (attributes.blur) copyScalarRange(*attributes.blur, i, destination + blurOffset);
        if (attributes.opacity) copyScalarRange(*attributes.opacity, i, destination + opacityOffset);
        if (attributes.gapWidth) copyScalarRange(*attributes.gapWidth, i, destination + gapWidthOffset);
        if (attributes.offset) copyScalarRange(*attributes.offset, i, destination + offsetOffset);
        if (attributes.width) copyScalarRange(*attributes.width, i, destination + widthOffset);
        if (attributes.floorWidth) copyScalarRange(*attributes.floorWidth, i, destination + floorWidthOffset);
        if (attributes.patternFrom) copyPattern(*attributes.patternFrom, i, destination + patternFromOffset);
        if (attributes.patternTo) copyPattern(*attributes.patternTo, i, destination + patternToOffset);
    }

    if (next == output) {
        return LineVertexDataUpdate::Unchanged;
    }
    output = std::move(next);
    return LineVertexDataUpdate::Changed;
}

} // namespace detail
} // namespace command_export
} // namespace mln
