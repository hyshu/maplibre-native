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

struct FillExtrusionAttributeData {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    std::size_t vertexOffset = 0;
    std::size_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
};

enum class FillExtrusionVertexDataUpdate : std::uint8_t {
    Failed,
    Unchanged,
    Changed,
};

/// Normalize MapLibre's independently data-driven base, height, and color
/// attributes into the Command Export ABI's fixed 44-byte fill-extrusion vertex format:
///
///   short2 position + short4 normal/edge-distance + float2 base range +
///   float2 height range + float4 packed-color range
///
/// Source base/height functions store one float per vertex and source color
/// functions store one packed float2; both are duplicated into zoom ranges.
/// Composite functions already contain both zoom stops. Missing base/height
/// attributes are layer constants and are duplicated into a pair. A missing
/// color range remains zeroed; the command flag tells the shader to use the
/// evaluated layer constant instead.
FillExtrusionVertexDataUpdate updateFillExtrusionVertexData(std::span<const std::uint8_t> layoutData,
                                                            std::size_t vertexCount,
                                                            const std::optional<FillExtrusionAttributeData>& base,
                                                            const std::optional<FillExtrusionAttributeData>& height,
                                                            const std::optional<FillExtrusionAttributeData>& color,
                                                            float constantBase,
                                                            float constantHeight,
                                                            std::vector<std::uint8_t>& output);

} // namespace detail
} // namespace command_export
} // namespace mln
