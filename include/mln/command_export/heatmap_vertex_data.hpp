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

struct HeatmapAttributeData {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    std::size_t vertexOffset = 0;
    std::size_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
};

struct HeatmapVertexAttributes {
    std::optional<HeatmapAttributeData> weight;
    std::optional<HeatmapAttributeData> radius;

    bool empty() const { return !weight && !radius; }
};

enum class HeatmapVertexDataUpdate : std::uint8_t {
    Failed,
    Unchanged,
    Changed,
};

/// Pack short2 position and float2 weight/radius zoom ranges into 20 bytes.
/// Source values are duplicated into both range endpoints. Absent attributes
/// stay zero and command flags select the evaluated layer constant instead.
/// Invalid input returns Failed and leaves output unchanged.
HeatmapVertexDataUpdate updateHeatmapVertexData(std::span<const std::uint8_t> layoutData,
                                                std::size_t vertexCount,
                                                const HeatmapVertexAttributes& attributes,
                                                std::vector<std::uint8_t>& output);

} // namespace detail
} // namespace command_export
} // namespace mln
