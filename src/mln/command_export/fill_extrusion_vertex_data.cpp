#include <mln/command_export/fill_extrusion_vertex_data.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = 12;
constexpr std::size_t rangeSize = sizeof(float) * 2;
constexpr std::size_t colorRangeSize = sizeof(float) * 4;
constexpr std::size_t outputStride = layoutStride + rangeSize * 2 + colorRangeSize;

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

std::size_t rangeAttributeSize(gfx::AttributeDataType type) {
    switch (type) {
        case gfx::AttributeDataType::Float:
            return sizeof(float);
        case gfx::AttributeDataType::Float2:
            return sizeof(float) * 2;
        default:
            return 0;
    }
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

bool validateAttribute(const FillExtrusionAttributeData& attribute, std::size_t vertexCount, std::size_t elementSize) {
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

std::array<float, 2> readRange(const FillExtrusionAttributeData& attribute, std::size_t index) {
    const auto start = attribute.vertexOffset * attribute.stride + attribute.offset + index * attribute.stride;
    std::array<float, 2> range{};
    std::memcpy(range.data(), attribute.data + start, rangeAttributeSize(attribute.type));
    if (attribute.type == gfx::AttributeDataType::Float) {
        range[1] = range[0];
    }
    return range;
}

void copyColorRange(const FillExtrusionAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto start = attribute.vertexOffset * attribute.stride + attribute.offset + index * attribute.stride;
    const auto* source = attribute.data + start;
    if (attribute.type == gfx::AttributeDataType::Float2) {
        constexpr std::size_t sourceSize = sizeof(float) * 2;
        std::memcpy(destination, source, sourceSize);
        std::memcpy(destination + sourceSize, source, sourceSize);
    } else {
        std::memcpy(destination, source, colorRangeSize);
    }
}

} // namespace

FillExtrusionVertexDataUpdate updateFillExtrusionVertexData(std::span<const std::uint8_t> layoutData,
                                                            std::size_t vertexCount,
                                                            const std::optional<FillExtrusionAttributeData>& base,
                                                            const std::optional<FillExtrusionAttributeData>& height,
                                                            const std::optional<FillExtrusionAttributeData>& color,
                                                            float constantBase,
                                                            float constantHeight,
                                                            std::vector<std::uint8_t>& output) {
    if (vertexCount == 0 || (!base && !height && !color)) {
        return FillExtrusionVertexDataUpdate::Failed;
    }

    std::size_t layoutBytes = 0;
    std::size_t outputBytes = 0;
    if (!checkedMultiply(vertexCount, layoutStride, layoutBytes) || layoutBytes > layoutData.size() ||
        !checkedMultiply(vertexCount, outputStride, outputBytes) ||
        (base && !validateAttribute(*base, vertexCount, rangeAttributeSize(base->type))) ||
        (height && !validateAttribute(*height, vertexCount, rangeAttributeSize(height->type))) ||
        (color && !validateAttribute(*color, vertexCount, colorAttributeSize(color->type)))) {
        return FillExtrusionVertexDataUpdate::Failed;
    }

    const std::array<float, 2> constantBaseRange{constantBase, constantBase};
    const std::array<float, 2> constantHeightRange{constantHeight, constantHeight};
    std::vector<std::uint8_t> next(outputBytes);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        auto* dst = next.data() + i * outputStride;
        std::memcpy(dst, layoutData.data() + i * layoutStride, layoutStride);

        const auto baseRange = base ? readRange(*base, i) : constantBaseRange;
        const auto heightRange = height ? readRange(*height, i) : constantHeightRange;
        std::memcpy(dst + layoutStride, baseRange.data(), rangeSize);
        std::memcpy(dst + layoutStride + rangeSize, heightRange.data(), rangeSize);
        if (color) {
            copyColorRange(*color, i, dst + layoutStride + rangeSize * 2);
        }
    }

    if (next == output) {
        return FillExtrusionVertexDataUpdate::Unchanged;
    }
    output = std::move(next);
    return FillExtrusionVertexDataUpdate::Changed;
}

} // namespace detail
} // namespace command_export
} // namespace mln
