#include <mln/test/util.hpp>

#include <mln/command_export/drawable.hpp>
#include <mln/gfx/draw_mode.hpp>
#include <mln/gfx/vertex_attribute.hpp>
#include <mln/renderer/buckets/hillshade_bucket.hpp>
#include <mln/renderer/render_static_data.hpp>
#include <mln/shaders/segment.hpp>
#include <mln/shaders/shader_defines.hpp>

#include <cstddef>
#include <vector>

namespace mln::command_export {
namespace {

TEST(CommandExportDrawable, TracksSegmentChangesWithoutInvalidatingIdenticalUpdates) {
    Drawable drawable("segment-cache");
    const auto update = [&](std::size_t baseInstance, std::size_t instanceCount, float sortKey) {
        const SegmentBase segment{0, 0, 4, 6, baseInstance, instanceCount, sortKey};
        drawable.updateVertexAttributes(nullptr, 4, gfx::Triangles(), nullptr, &segment, 1);
    };

    update(0, 1, 1.5f);
    auto version = drawable.getBufferVersion();
    update(0, 1, 1.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(0, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(0, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(2, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(2, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(2, 3, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(2, 3, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);
}

TEST(CommandExportDrawable, TracksIndexOwnerChangesWithIdenticalContents) {
    Drawable drawable("index-owner-cache");
    const auto first = std::make_shared<HillshadeBucket::IndexVector>(RenderStaticData::quadTriangleIndices());
    const auto second = std::make_shared<HillshadeBucket::IndexVector>(*first);
    const SegmentBase segment{0, 0, 4, 6};
    const auto update = [&](const auto& indices) {
        drawable.updateVertexAttributes(nullptr, 4, gfx::Triangles(), indices, &segment, 1);
    };

    ASSERT_NE(first, second);
    ASSERT_EQ(first->vector(), second->vector());
    update(first);
    auto version = drawable.getBufferVersion();
    update(first);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(second);
    EXPECT_EQ(drawable.getIndexVector(), second);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(second);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(first);
    EXPECT_EQ(drawable.getIndexVector(), first);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(first);
    EXPECT_EQ(drawable.getBufferVersion(), version);
}

TEST(CommandExportDrawable, TracksHillshadeMasksWithRetainedFullTileGeometry) {
    HillshadeBucket bucket(PremultipliedImage({16, 16}), Tileset::RasterEncoding::Mapbox);
    const auto fullVertices = std::make_shared<HillshadeBucket::VertexVector>(RenderStaticData::rasterVertices());
    const auto fullIndices = std::make_shared<HillshadeBucket::IndexVector>(RenderStaticData::quadTriangleIndices());
    const auto fullSegments = RenderStaticData::rasterSegments();
    const TileMask fullMask{{0, 0, 0}};
    Drawable drawable("hillshade-mask-cache");

    const auto update = [&](const auto& vertices, const auto& indices, const auto& segments) {
        auto attributes = std::make_shared<gfx::VertexAttributeArray>();
        attributes->set(shaders::idHillshadePosVertexAttribute)
            ->setSharedRawData(vertices,
                              offsetof(HillshadeLayoutVertex, a1),
                              0,
                              sizeof(HillshadeLayoutVertex),
                              gfx::AttributeDataType::Short2);
        attributes->set(shaders::idHillshadeTexturePosVertexAttribute)
            ->setSharedRawData(vertices,
                              offsetof(HillshadeLayoutVertex, a2),
                              0,
                              sizeof(HillshadeLayoutVertex),
                              gfx::AttributeDataType::Short2);
        drawable.updateVertexAttributes(std::move(attributes),
                                        vertices->elements(),
                                        gfx::Triangles(),
                                        indices,
                                        segments.data(),
                                        segments.size());
    };
    const auto vertexBytes = [&] {
        const auto& position = drawable.getVertexAttributes()->get(shaders::idHillshadePosVertexAttribute);
        const auto& vertices = position->getSharedRawData();
        const auto* data = static_cast<const uint8_t*>(vertices->getRawData());
        return std::vector<uint8_t>(data, data + vertices->getRawCount() * vertices->getRawSize());
    };

    ASSERT_NE(fullVertices, bucket.sharedVertices);
    ASSERT_NE(fullIndices, bucket.sharedIndices);
    update(fullVertices, fullIndices, fullSegments);
    auto version = drawable.getBufferVersion();
    const auto fullBytes = vertexBytes();
    auto previousBytes = fullBytes;
    update(fullVertices, fullIndices, fullSegments);
    EXPECT_EQ(drawable.getBufferVersion(), version);
    EXPECT_EQ(vertexBytes(), fullBytes);

    // Mask changes can preserve owners, counts and indices while changing vertex positions.
    for (const auto& mask : {TileMask{{1, 0, 0}}, fullMask, TileMask{{1, 1, 1}}, TileMask{{1, 0, 0}}}) {
        bucket.setMask(TileMask(mask));
        const bool full = mask == fullMask;
        const auto& vertices = full ? fullVertices : bucket.sharedVertices;
        const auto& indices = full ? fullIndices : bucket.sharedIndices;
        const auto& segments = full ? fullSegments : bucket.segments;
        ASSERT_EQ(vertices->elements(), 4u);
        ASSERT_EQ(indices->vector(), fullIndices->vector());

        update(vertices, indices, segments);
        EXPECT_EQ(drawable.getBufferVersion(), ++version);
        EXPECT_EQ(drawable.getIndexVector(), indices);
        const auto bytes = vertexBytes();
        EXPECT_NE(bytes, previousBytes);
        EXPECT_EQ(bytes == fullBytes, full);

        bucket.setMask(TileMask(mask));
        update(vertices, indices, segments);
        EXPECT_EQ(drawable.getBufferVersion(), version);
        EXPECT_EQ(vertexBytes(), bytes);
        previousBytes = bytes;
    }
}

} // namespace
} // namespace mln::command_export
