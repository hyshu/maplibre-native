#include <mln/test/util.hpp>
#include <mln/test/stub_geometry_tile_feature.hpp>

#include <mln/command_export/draw_command.hpp>
#include <mln/renderer/buckets/fill_extrusion_bucket.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace mln {
namespace {

GeometryCoordinates square(int16_t x, int16_t y, int16_t size, bool reversed = false) {
    GeometryCoordinates ring{{x, y}, {static_cast<int16_t>(x + size), y},
                             {static_cast<int16_t>(x + size), static_cast<int16_t>(y + size)},
                             {x, static_cast<int16_t>(y + size)}, {x, y}};
    if (reversed) std::reverse(ring.begin(), ring.end());
    return ring;
}

FillExtrusionBucket makeBucket() {
    FillExtrusionBucket::PossiblyEvaluatedLayoutProperties layout;
    return FillExtrusionBucket(layout, {}, 14.0f, 0);
}

void addFeature(FillExtrusionBucket& bucket, FeatureIdentifier id, const GeometryCollection& geometry, std::size_t index) {
    bucket.addFeature(StubGeometryTileFeature{std::move(id), FeatureType::Polygon, geometry.clone(), {}},
                      geometry,
                      {},
                      PatternLayerMap(),
                      index,
                      CanonicalTileID(14, 3, 5));
}

TEST(FillExtrusionFeatureParts, RecordsEachPolygonInQueryOrder) {
    auto bucket = makeBucket();
    // Two outer rings form two polygons. The reversed ring is a hole of the second.
    const GeometryCollection geometry{square(0, 0, 10), square(20, 0, 10), square(22, 2, 4, true)};
    addFeature(bucket, uint64_t{42}, geometry, 7);

    ASSERT_EQ(bucket.featureParts.size(), 2u);
    const auto& first = bucket.featureParts[0];
    const auto& second = bucket.featureParts[1];
    EXPECT_EQ(first.featureIndex, 7u);
    EXPECT_EQ(first.polygonIndex, 0u);
    EXPECT_EQ(second.polygonIndex, 1u);
    EXPECT_EQ(first.idType, FillExtrusionFeatureIdType::Unsigned);
    EXPECT_EQ(first.idValue, 42u);

    // Five roof vertices and four wall quads per closed square ring.
    EXPECT_EQ(first.vertexOffset, 0u);
    EXPECT_EQ(first.vertexCount, 21u);
    EXPECT_EQ(second.vertexOffset, 21u);
    EXPECT_EQ(second.vertexCount, 42u);
    // Wall triangles precede each polygon's roof triangles.
    EXPECT_EQ(first.indexOffset, 0u);
    EXPECT_EQ(first.indexCount, 24u + 6u);
    EXPECT_EQ(second.indexOffset, first.indexCount);
    EXPECT_EQ(second.indexOffset + second.indexCount, bucket.triangles.elements());
    EXPECT_EQ(bucket.vertices.elements(), 63u);

    const auto segmentParts = bucket.featurePartsForSegment(0);
    ASSERT_EQ(segmentParts.size(), 2u);
    EXPECT_EQ(segmentParts.data(), bucket.featureParts.data());
    EXPECT_TRUE(bucket.featurePartsForSegment(1).empty());
}

TEST(FillExtrusionFeatureParts, EncodesIdentifiersAndCountsEmptyPolygons) {
    auto bucket = makeBucket();
    addFeature(bucket, std::string("building-a"), {square(0, 0, 4)}, 0);
    addFeature(bucket, int64_t{-3}, {square(10, 0, 4)}, 1);
    addFeature(bucket, 2.5, {square(20, 0, 4)}, 2);
    addFeature(bucket, NullValue{}, {square(30, 0, 4)}, 3);

    ASSERT_EQ(bucket.featureParts.size(), 4u);
    const auto& text = bucket.featureParts[0];
    EXPECT_EQ(text.idType, FillExtrusionFeatureIdType::String);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(static_cast<uintptr_t>(text.idValue)), text.idLength),
              "building-a");

    int64_t signedValue = 0;
    std::memcpy(&signedValue, &bucket.featureParts[1].idValue, sizeof(signedValue));
    EXPECT_EQ(bucket.featureParts[1].idType, FillExtrusionFeatureIdType::Signed);
    EXPECT_EQ(signedValue, -3);

    double doubleValue = 0;
    std::memcpy(&doubleValue, &bucket.featureParts[2].idValue, sizeof(doubleValue));
    EXPECT_EQ(bucket.featureParts[2].idType, FillExtrusionFeatureIdType::Double);
    EXPECT_EQ(doubleValue, 2.5);
    EXPECT_EQ(bucket.featureParts[3].idType, FillExtrusionFeatureIdType::None);

    // A polygon without vertices is skipped but still numbered.
    auto empty = makeBucket();
    addFeature(empty, uint64_t{1}, {square(0, 0, 0), square(10, 0, 4)}, 0);
    for (const auto& part : empty.featureParts) {
        EXPECT_GT(part.vertexCount, 0u);
    }
}

TEST(FillExtrusionFeatureParts, RoundTripsThroughCommandPayloads) {
    using namespace command_export;
    std::array<CommandFillExtrusionPart, 2> parts{};
    parts[1].polygonIndex = 1;

    FrameData frame;
    auto& first = frame.addCommand(ShaderType::FillExtrusion, DrawModeType::Triangles, nullptr, 12, 0, nullptr, 0);
    const std::array<uint8_t, 4> drawable{1, 2, 3, 4};
    DrawCommandPayload payload;
    payload.drawableUBO = drawable;
    payload.stencil = CommandStencil{1, StencilModeType::FillExtrusion};
    const std::string layerID = "buildings-3d";
    CommandFillExtrusionFeatures features;
    features.parts = parts.data();
    features.layerId = layerID.data();
    features.partCount = static_cast<uint32_t>(parts.size());
    features.partStride = sizeof(CommandFillExtrusionPart);
    features.layerIdLength = static_cast<uint32_t>(layerID.size());
    features.tileWrap = -1;
    features.tileOverscaledZ = 16;
    features.tileZ = 14;
    features.tileX = 3;
    features.tileY = 5;
    features.segmentIndex = 2;
    features.metadataId = uint64_t{1} << 40;
    payload.fillExtrusionFeatures = features;
    frame.setPayload(first, payload);
    frame.addCommand(ShaderType::Fill, DrawModeType::Triangles, nullptr, 4, 0, nullptr, 0);
    frame.compactPayload();

    const CommandPayloadView view(frame.payload, frame.commands[0]);
    ASSERT_TRUE(view.valid());
    const auto& exported = view.get().fillExtrusionFeatures;
    ASSERT_TRUE(exported.has_value());
    EXPECT_EQ(exported->parts, parts.data());
    EXPECT_EQ(std::string_view(exported->layerId, exported->layerIdLength), "buildings-3d");
    EXPECT_EQ(exported->partCount, 2u);
    EXPECT_EQ(exported->partStride, sizeof(CommandFillExtrusionPart));
    EXPECT_EQ(exported->tileWrap, -1);
    EXPECT_EQ(exported->tileOverscaledZ, 16u);
    EXPECT_EQ(exported->tileZ, 14u);
    EXPECT_EQ(exported->tileX, 3u);
    EXPECT_EQ(exported->tileY, 5u);
    EXPECT_EQ(exported->segmentIndex, 2u);
    EXPECT_EQ(exported->metadataId, uint64_t{1} << 40);
    ASSERT_TRUE(view.get().stencil.has_value());
    EXPECT_EQ(view.get().drawableUBO.size(), drawable.size());
    EXPECT_EQ(frame.commands[0].payloadSize % 8, 0u);
}

} // namespace
} // namespace mln
