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

struct LineAttributeData {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    std::size_t vertexOffset = 0;
    std::size_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
};

struct LineVertexAttributes {
    std::optional<LineAttributeData> color;
    std::optional<LineAttributeData> blur;
    std::optional<LineAttributeData> opacity;
    std::optional<LineAttributeData> gapWidth;
    std::optional<LineAttributeData> offset;
    std::optional<LineAttributeData> width;
    std::optional<LineAttributeData> floorWidth;
    std::optional<LineAttributeData> patternFrom;
    std::optional<LineAttributeData> patternTo;

    bool empty() const {
        return !color && !blur && !opacity && !gapWidth && !offset && !width && !floorWidth && !patternFrom &&
               !patternTo;
    }
};

enum class LineVertexDataUpdate : std::uint8_t {
    Failed,
    Unchanged,
    Changed,
};

/// Normalize MapLibre's independently data-driven line paint attributes into
/// the Command Export ABI's fixed 88-byte line vertex format:
///
///   LineLayoutVertex + float4 packed-color range + float2 blur range +
///   float2 opacity range + float2 gap-width range + float2 offset range +
///   float2 width range + float2 floor-width range + ushort4 pattern-from +
///   ushort4 pattern-to
///
/// Source functions store one value per vertex and are duplicated into a
/// range. Composite functions already contain both zoom-stop values and are
/// copied unchanged. Pattern atlas rectangles are copied without conversion.
/// An absent property leaves its range zeroed; command flags tell the consumer to use
/// the evaluated layer constant instead. Pattern attributes must occur as a
/// pair because they represent one cross-faded paint property.
LineVertexDataUpdate updateLineVertexData(std::span<const std::uint8_t> layoutData,
                                          std::size_t vertexCount,
                                          const LineVertexAttributes& attributes,
                                          std::vector<std::uint8_t>& output);

} // namespace detail
} // namespace command_export
} // namespace mln
