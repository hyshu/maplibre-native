#include <mln/command_export/fill_vertex_data.hpp>

#include <cstring>
#include <limits>
#include <utility>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t fillLayoutStride = sizeof(std::int16_t) * 2;
constexpr std::size_t lineLayoutStride = sizeof(std::int16_t) * 2 + sizeof(std::uint8_t) * 4;
constexpr std::size_t colorRangeSize = sizeof(float) * 4;
constexpr std::size_t opacityRangeSize = sizeof(float) * 2;

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

std::size_t opacityAttributeSize(gfx::AttributeDataType type) {
    switch (type) {
        case gfx::AttributeDataType::Float:
            return sizeof(float);
        case gfx::AttributeDataType::Float2:
            return sizeof(float) * 2;
        default:
            return 0;
    }
}

bool validateAttribute(const FillAttributeData& attribute, std::size_t vertexCount, std::size_t elementSize) {
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

const std::uint8_t* attributeValue(const FillAttributeData& attribute, std::size_t index) {
    return attribute.data + attribute.vertexOffset * attribute.stride + attribute.offset + index * attribute.stride;
}

void copyColorRange(const FillAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float2) {
        constexpr std::size_t sourceSize = sizeof(float) * 2;
        std::memcpy(destination, source, sourceSize);
        std::memcpy(destination + sourceSize, source, sourceSize);
    } else {
        std::memcpy(destination, source, colorRangeSize);
    }
}

void copyOpacityRange(const FillAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attributeValue(attribute, index);
    if (attribute.type == gfx::AttributeDataType::Float) {
        std::memcpy(destination, source, sizeof(float));
        std::memcpy(destination + sizeof(float), source, sizeof(float));
    } else {
        std::memcpy(destination, source, opacityRangeSize);
    }
}

FillVertexDataUpdate updatePaintedVertexData(std::span<const std::uint8_t> layoutData,
                                             std::size_t layoutStride,
                                             std::size_t vertexCount,
                                             const std::optional<FillAttributeData>& color,
                                             const std::optional<FillAttributeData>& opacity,
                                             std::vector<std::uint8_t>& output) {
    if (vertexCount == 0 || (!color && !opacity)) {
        return FillVertexDataUpdate::Failed;
    }

    std::size_t outputStride = 0;
    std::size_t layoutBytes = 0;
    std::size_t outputBytes = 0;
    if (!checkedAdd(layoutStride, colorRangeSize + opacityRangeSize, outputStride) ||
        !checkedMultiply(vertexCount, layoutStride, layoutBytes) || layoutBytes > layoutData.size() ||
        !checkedMultiply(vertexCount, outputStride, outputBytes) ||
        (color && !validateAttribute(*color, vertexCount, colorAttributeSize(color->type))) ||
        (opacity && !validateAttribute(*opacity, vertexCount, opacityAttributeSize(opacity->type)))) {
        return FillVertexDataUpdate::Failed;
    }

    std::vector<std::uint8_t> next(outputBytes);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        auto* destination = next.data() + i * outputStride;
        std::memcpy(destination, layoutData.data() + i * layoutStride, layoutStride);
        if (color) {
            copyColorRange(*color, i, destination + layoutStride);
        }
        if (opacity) {
            copyOpacityRange(*opacity, i, destination + layoutStride + colorRangeSize);
        }
    }

    if (next == output) {
        return FillVertexDataUpdate::Unchanged;
    }
    output = std::move(next);
    return FillVertexDataUpdate::Changed;
}

} // namespace

FillVertexDataUpdate updateFillVertexData(std::span<const std::uint8_t> layoutData,
                                          std::size_t vertexCount,
                                          const std::optional<FillAttributeData>& color,
                                          const std::optional<FillAttributeData>& opacity,
                                          std::vector<std::uint8_t>& output) {
    return updatePaintedVertexData(layoutData, fillLayoutStride, vertexCount, color, opacity, output);
}

FillVertexDataUpdate updateFillOutlineTriangulatedVertexData(std::span<const std::uint8_t> layoutData,
                                                             std::size_t vertexCount,
                                                             const std::optional<FillAttributeData>& color,
                                                             const std::optional<FillAttributeData>& opacity,
                                                             std::vector<std::uint8_t>& output) {
    return updatePaintedVertexData(layoutData, lineLayoutStride, vertexCount, color, opacity, output);
}

} // namespace detail
} // namespace command_export
} // namespace mln
