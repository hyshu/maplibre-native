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

struct FillAttributeData {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    std::size_t vertexOffset = 0;
    std::size_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
};

enum class FillVertexDataUpdate : std::uint8_t {
    Failed,
    Unchanged,
    Changed,
};

/// Normalize MapLibre's independently data-driven fill color and opacity
/// attributes into the Command Export ABI's fixed 28-byte vertex format:
///
///   short2 position + float4 packed-color range + float2 opacity range
///
/// Source functions store one value per vertex and are duplicated into a
/// range. Composite functions already contain both zoom-stop values and are
/// copied unchanged. An absent attribute leaves its range zeroed; the command
/// flags tell the consumer shader to use the evaluated layer constant instead.
FillVertexDataUpdate updateFillVertexData(std::span<const std::uint8_t> layoutData,
                                          std::size_t vertexCount,
                                          const std::optional<FillAttributeData>& color,
                                          const std::optional<FillAttributeData>& opacity,
                                          std::vector<std::uint8_t>& output);

/// Normalize the Command Export ABI's triangulated fill outline into a fixed 32-byte
/// format:
///
///   LineLayoutVertex + float4 packed-outline-color range + float2 opacity range
///
/// MapLibre uses a second set of fill paint binders populated to the generated
/// line vertex count on this backend, preserving source, composite, and
/// feature-state paint values without falling back to non-antialiased line
/// primitives.
FillVertexDataUpdate updateFillOutlineTriangulatedVertexData(std::span<const std::uint8_t> layoutData,
                                                             std::size_t vertexCount,
                                                             const std::optional<FillAttributeData>& color,
                                                             const std::optional<FillAttributeData>& opacity,
                                                             std::vector<std::uint8_t>& output);

} // namespace detail
} // namespace command_export
} // namespace mln
