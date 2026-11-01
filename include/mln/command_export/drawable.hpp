#pragma once

#include <mln/gfx/drawable.hpp>
#include <mln/command_export/uniform_buffer.hpp>

#include <optional>

namespace mln {
namespace command_export {

class Drawable final : public gfx::Drawable {
public:
    Drawable(std::string name);
    ~Drawable() override;

    void draw(PaintParameters&) const override;

    void setIndexData(gfx::IndexVectorBasePtr, std::vector<UniqueDrawSegment> segments) override;
    void setVertices(std::vector<uint8_t>&&, std::size_t, gfx::AttributeDataType) override;

    const gfx::UniformBufferArray& getUniformBuffers() const override { return uniformBuffers; }
    gfx::UniformBufferArray& mutableUniformBuffers() override { return uniformBuffers; }

    void setColorMode(const gfx::ColorMode&) override;
    void setShader(gfx::ShaderProgramBasePtr) override;
    void setEnableStencil(bool) override;
    void setEnableDepth(bool) override;
    void setSubLayerIndex(int32_t) override;
    void setDepthType(gfx::DepthMaskType) override;

    void updateVertexAttributes(gfx::VertexAttributeArrayPtr,
                                std::size_t vertexCount,
                                gfx::DrawMode,
                                gfx::IndexVectorBasePtr,
                                const SegmentBase* segments,
                                std::size_t segmentCount) override;

    /// Access CPU-side vertex data
    const std::vector<uint8_t>& getVertexData() const { return vertexData; }
    std::size_t getVertexCount() const { return vertexCount; }

    /// Access CPU-side index data
    const gfx::IndexVectorBasePtr& getIndexVector() const { return indexVector; }

    /// Unique buffer identity for consumer-side GPU buffer caching.
    uint32_t getBufferId() const { return bufferId; }
    uint32_t getBufferVersion() const { return bufferVersion; }

    /// Set the group-wide reference returned by stencilModeFor3D(). The
    /// reference is cached at TileLayerGroup scope because asking
    /// PaintParameters for it more than once allocates a different value.
    void setStencilReferenceFor3D(std::optional<int32_t> value) { stencilReferenceFor3D = value; }

private:
    struct DDAttributeCacheState {
        bool present = false;
        const void* owner = nullptr;
        const void* data = nullptr;
        std::size_t size = 0;
        uint32_t offset = 0;
        uint32_t vertexOffset = 0;
        uint32_t stride = 0;
        gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
        double lastModified = 0;

        bool operator==(const DDAttributeCacheState&) const = default;
    };

    struct DDVertexCacheState {
        DDAttributeCacheState base;
        DDAttributeCacheState color;
        DDAttributeCacheState height;
        std::size_t vertexCount = 0;
        uint32_t constantBase = 0;
        uint32_t constantHeight = 0;

        bool operator==(const DDVertexCacheState&) const = default;
    };

    struct FillDDVertexCacheState {
        DDAttributeCacheState color;
        DDAttributeCacheState opacity;
        std::size_t vertexCount = 0;

        bool operator==(const FillDDVertexCacheState&) const = default;
    };

    struct CircleDDVertexCacheState {
        DDAttributeCacheState color;
        DDAttributeCacheState radius;
        DDAttributeCacheState blur;
        DDAttributeCacheState opacity;
        DDAttributeCacheState strokeColor;
        DDAttributeCacheState strokeWidth;
        DDAttributeCacheState strokeOpacity;
        std::size_t vertexCount = 0;

        bool operator==(const CircleDDVertexCacheState&) const = default;
    };

    struct LineDDVertexCacheState {
        DDAttributeCacheState color;
        DDAttributeCacheState blur;
        DDAttributeCacheState opacity;
        DDAttributeCacheState gapWidth;
        DDAttributeCacheState offset;
        DDAttributeCacheState width;
        DDAttributeCacheState floorWidth;
        DDAttributeCacheState patternFrom;
        DDAttributeCacheState patternTo;
        std::size_t vertexCount = 0;

        bool operator==(const LineDDVertexCacheState&) const = default;
    };

    uint32_t bufferId;
    mutable uint32_t bufferVersion = 0;

    UniformBufferArray uniformBuffers;

    std::optional<int32_t> stencilReferenceFor3D;

    // CPU-side vertex storage
    std::vector<uint8_t> vertexData;
    std::size_t vertexCount = 0;
    gfx::AttributeDataType vertexType{};

    // CPU-side index storage
    gfx::IndexVectorBasePtr indexVector;
    std::vector<UniqueDrawSegment> segments;

    // Data-driven fill-extrusion: interleaved pos/normal + base + height +
    // packed color (44 bytes per vertex), rebuilt when geometry, paint
    // attributes, or an independently uniform base/height value changes.
    mutable std::vector<uint8_t> ddVertexData;
    mutable uint32_t ddVertexVersion = 0xFFFFFFFF;
    mutable std::optional<DDVertexCacheState> ddVertexCacheState;

    // Data-driven fill / triangulated fill outline: interleaved FillLayout
    // (28 bytes total) or LineLayout (32 bytes total), followed by packed
    // color and opacity ranges. Zoom-only changes do not rebuild this cache.
    mutable std::vector<uint8_t> fillDDVertexData;
    mutable uint32_t fillDDVertexVersion = 0xFFFFFFFF;
    mutable std::optional<FillDDVertexCacheState> fillDDVertexCacheState;

    // Data-driven circle: interleaved position, two packed-color ranges, and
    // five scalar ranges (76 bytes per vertex). Zoom interpolation remains in
    // CircleDrawableUBO, so zoom-only changes do not rebuild this cache.
    mutable std::vector<uint8_t> circleDDVertexData;
    mutable uint32_t circleDDVertexVersion = 0xFFFFFFFF;
    mutable std::optional<CircleDDVertexCacheState> circleDDVertexCacheState;

    // Data-driven line: interleaved LineLayoutVertex, packed-color/scalar
    // zoom ranges, and pattern atlas rectangles (88 bytes per vertex). Zoom
    // interpolation remains in the line variant's drawable UBO.
    mutable std::vector<uint8_t> lineDDVertexData;
    mutable uint32_t lineDDVertexVersion = 0xFFFFFFFF;
    mutable std::optional<LineDDVertexCacheState> lineDDVertexCacheState;
};

} // namespace command_export
} // namespace mln
