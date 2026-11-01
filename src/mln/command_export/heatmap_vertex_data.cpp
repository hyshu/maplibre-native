#include <mln/command_export/heatmap_vertex_data.hpp>

#include <cstring>
#include <limits>
#include <utility>

namespace mln {
namespace command_export {
namespace detail {
namespace {

constexpr std::size_t layoutStride = sizeof(std::int16_t) * 2;
constexpr std::size_t scalarRangeSize = sizeof(float) * 2;
constexpr std::size_t outputStride = layoutStride + scalarRangeSize * 2;
static_assert(outputStride == 20);

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

bool validateAttribute(const HeatmapAttributeData& attribute, std::size_t vertexCount) {
    const auto elementSize = attribute.type == gfx::AttributeDataType::Float    ? sizeof(float)
                             : attribute.type == gfx::AttributeDataType::Float2 ? scalarRangeSize
                                                                              : 0;
    if (!attribute.data || elementSize == 0 || attribute.stride < elementSize ||
        attribute.offset > attribute.stride || elementSize > attribute.stride - attribute.offset) {
        return false;
    }

    std::size_t start = 0;
    std::size_t lastVertexOffset = 0;
    std::size_t requiredSize = 0;
    return checkedMultiply(attribute.vertexOffset, attribute.stride, start) &&
           checkedAdd(start, attribute.offset, start) &&
           checkedMultiply(vertexCount - 1, attribute.stride, lastVertexOffset) &&
           checkedAdd(start, lastVertexOffset, requiredSize) && checkedAdd(requiredSize, elementSize, requiredSize) &&
           requiredSize <= attribute.size;
}

void copyRange(const HeatmapAttributeData& attribute, std::size_t index, std::uint8_t* destination) {
    const auto* source = attribute.data + (attribute.vertexOffset + index) * attribute.stride + attribute.offset;
    if (attribute.type == gfx::AttributeDataType::Float) {
        std::memcpy(destination, source, sizeof(float));
        std::memcpy(destination + sizeof(float), source, sizeof(float));
    } else {
        std::memcpy(destination, source, scalarRangeSize);
    }
}

} // namespace

HeatmapVertexDataUpdate updateHeatmapVertexData(std::span<const std::uint8_t> layoutData,
                                                std::size_t vertexCount,
                                                const HeatmapVertexAttributes& attributes,
                                                std::vector<std::uint8_t>& output) {
    if (vertexCount == 0 || attributes.empty()) {
        return HeatmapVertexDataUpdate::Failed;
    }

    std::size_t layoutBytes = 0;
    std::size_t outputBytes = 0;
    if (!checkedMultiply(vertexCount, layoutStride, layoutBytes) || layoutBytes > layoutData.size() ||
        !checkedMultiply(vertexCount, outputStride, outputBytes) ||
        (attributes.weight && !validateAttribute(*attributes.weight, vertexCount)) ||
        (attributes.radius && !validateAttribute(*attributes.radius, vertexCount))) {
        return HeatmapVertexDataUpdate::Failed;
    }

    std::vector<std::uint8_t> next(outputBytes);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        auto* destination = next.data() + i * outputStride;
        std::memcpy(destination, layoutData.data() + i * layoutStride, layoutStride);
        if (attributes.weight) copyRange(*attributes.weight, i, destination + layoutStride);
        if (attributes.radius) copyRange(*attributes.radius, i, destination + layoutStride + scalarRangeSize);
    }

    if (next == output) {
        return HeatmapVertexDataUpdate::Unchanged;
    }
    output = std::move(next);
    return HeatmapVertexDataUpdate::Changed;
}

} // namespace detail
} // namespace command_export
} // namespace mln
