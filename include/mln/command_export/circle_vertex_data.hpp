#pragma once

#include <mln/gfx/gfx_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mln {
namespace command_export {
namespace detail {

struct CircleAttributeData {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    std::size_t vertexOffset = 0;
    std::size_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
};

struct CircleVertexAttributes {
    std::optional<CircleAttributeData> color;
    std::optional<CircleAttributeData> radius;
    std::optional<CircleAttributeData> blur;
    std::optional<CircleAttributeData> opacity;
    std::optional<CircleAttributeData> strokeColor;
    std::optional<CircleAttributeData> strokeWidth;
    std::optional<CircleAttributeData> strokeOpacity;

    bool empty() const {
        return !color && !radius && !blur && !opacity && !strokeColor && !strokeWidth && !strokeOpacity;
    }
};

enum class CircleVertexDataUpdate : std::uint8_t {
    Failed,
    Unchanged,
    Changed,
};

/// Normalize MapLibre's seven independently data-driven circle paint
/// attributes into the Command Export ABI's fixed 76-byte circle vertex format:
///
///   short2 position + float4 packed color range + float2 radius range +
///   float2 blur range + float2 opacity range + float4 packed stroke-color
///   range + float2 stroke-width range + float2 stroke-opacity range
///
/// Source functions store one value per vertex and are duplicated into a
/// range. Composite functions already contain both zoom-stop values and are
/// copied unchanged. An absent property leaves its range zeroed; command
/// flags tell the consumer shader to use the evaluated layer constant instead.
CircleVertexDataUpdate updateCircleVertexData(std::span<const std::uint8_t> layoutData,
                                              std::size_t vertexCount,
                                              const CircleVertexAttributes& attributes,
                                              std::vector<std::uint8_t>& output);

} // namespace detail
} // namespace command_export
} // namespace mln
