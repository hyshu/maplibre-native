#include <mln/test/util.hpp>

#include <mln/command_export/context.hpp>
#include <mln/command_export/dynamic_texture.hpp>
#include <mln/command_export/texture2d.hpp>
#include <mln/command_export/upload_pass.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace mln {
namespace command_export {
namespace {

std::vector<uint8_t> readRegion(const Texture2D& texture, const gfx::TextureHandle& handle, std::size_t stride) {
    const auto& rect = handle.getRectangle();
    const auto& pixels = texture.getPixelData();
    const auto atlasWidth = static_cast<std::size_t>(texture.getSize().width);
    std::vector<uint8_t> result;
    result.reserve(static_cast<std::size_t>(rect.w) * rect.h * stride);
    for (uint32_t row = 0; row < rect.h; ++row) {
        const auto offset = ((static_cast<std::size_t>(rect.y) + row) * atlasWidth + rect.x) * stride;
        result.insert(result.end(), pixels.begin() + offset, pixels.begin() + offset + rect.w * stride);
    }
    return result;
}

TEST(CommandExportDynamicTexture, CopiesAndUploadsRGBASubregions) {
    Context context;
    UploadPass uploadPass(context);
    DynamicTexture atlas(context, Size{4, 3}, gfx::TexturePixelType::RGBA);
    const std::vector<uint8_t> expected{
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
    };
    auto callerPixels = expected;

    const auto handle = atlas.addImage(callerPixels.data(), Size{2, 2}, 7);
    ASSERT_TRUE(handle);
    std::fill(callerPixels.begin(), callerPixels.end(), 0xff);
    atlas.uploadDeferredImages(uploadPass);

    const auto texture = std::static_pointer_cast<Texture2D>(atlas.getTexture());
    EXPECT_EQ(texture->getSize(), (Size{4, 3}));
    EXPECT_EQ(texture->numChannels(), 4u);
    EXPECT_EQ(texture->getSamplerFilter(), gfx::TextureFilterType::Linear);
    EXPECT_EQ(texture->getVersion(), 1u);
    EXPECT_EQ(readRegion(*texture, *handle, 4), expected);

    atlas.uploadDeferredImages(uploadPass);
    EXPECT_EQ(texture->getVersion(), 1u);
}

TEST(CommandExportDynamicTexture, UploadsAlphaWithOneByteStride) {
    Context context;
    UploadPass uploadPass(context);
    DynamicTexture atlas(context, Size{5, 3}, gfx::TexturePixelType::Alpha);
    const std::vector<uint8_t> expected{3, 5, 7, 11, 13, 17};

    const auto handle = atlas.addImage(expected.data(), Size{3, 2}, 8);
    ASSERT_TRUE(handle);
    atlas.uploadDeferredImages(uploadPass);

    const auto texture = std::static_pointer_cast<Texture2D>(atlas.getTexture());
    EXPECT_EQ(texture->numChannels(), 1u);
    EXPECT_EQ(texture->getPixelData().size(), 15u);
    EXPECT_EQ(readRegion(*texture, *handle, 1), expected);
}

TEST(CommandExportDynamicTexture, DuplicateUniqueIdKeepsFirstPendingUploadUntilFinalRemove) {
    Context context;
    UploadPass uploadPass(context);
    DynamicTexture atlas(context, Size{4, 4}, gfx::TexturePixelType::RGBA);
    const std::vector<uint8_t> firstPixels{1, 2, 3, 4};
    const std::vector<uint8_t> duplicatePixels{9, 9, 9, 9};

    const auto first = atlas.addImage(firstPixels.data(), Size{1, 1}, 42);
    const auto duplicate = atlas.addImage(duplicatePixels.data(), Size{1, 1}, 42);
    ASSERT_TRUE(first);
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(*first, *duplicate);
    EXPECT_FALSE(first->isUploadNeeded());
    EXPECT_FALSE(duplicate->isUploadNeeded());
    EXPECT_FALSE(atlas.removeTexture(*duplicate));

    atlas.uploadDeferredImages(uploadPass);
    const auto texture = std::static_pointer_cast<Texture2D>(atlas.getTexture());
    EXPECT_EQ(texture->getVersion(), 1u);
    EXPECT_EQ(readRegion(*texture, *first, 4), firstPixels);
    EXPECT_TRUE(atlas.removeTexture(*first));
    EXPECT_TRUE(atlas.isEmpty());
}

TEST(CommandExportDynamicTexture, FinalRemoveBeforeFlushErasesPendingUpload) {
    Context context;
    UploadPass uploadPass(context);
    DynamicTexture atlas(context, Size{2, 2}, gfx::TexturePixelType::RGBA);
    const std::vector<uint8_t> pixels{1, 2, 3, 4};

    const auto handle = atlas.addImage(pixels.data(), Size{1, 1}, 1);
    ASSERT_TRUE(handle);
    EXPECT_TRUE(atlas.removeTexture(*handle));
    atlas.uploadDeferredImages(uploadPass);

    const auto texture = std::static_pointer_cast<Texture2D>(atlas.getTexture());
    EXPECT_EQ(texture->getVersion(), 0u);
    EXPECT_TRUE(std::all_of(
        texture->getPixelData().begin(), texture->getPixelData().end(), [](uint8_t value) { return value == 0; }));
}

TEST(CommandExportDynamicTexture, NullPixelsZeroFillReusedAtlasRegion) {
    Context context;
    UploadPass uploadPass(context);
    DynamicTexture atlas(context, Size{2, 1}, gfx::TexturePixelType::RGBA);
    const std::vector<uint8_t> pixels{10, 20, 30, 40, 50, 60, 70, 80};

    const auto first = atlas.addImage(pixels.data(), Size{2, 1}, 1);
    ASSERT_TRUE(first);
    atlas.uploadDeferredImages(uploadPass);
    EXPECT_TRUE(atlas.removeTexture(*first));

    const auto zeroed = atlas.addImage(nullptr, Size{2, 1}, 2);
    ASSERT_TRUE(zeroed);
    atlas.uploadDeferredImages(uploadPass);

    const auto texture = std::static_pointer_cast<Texture2D>(atlas.getTexture());
    EXPECT_EQ(texture->getVersion(), 2u);
    EXPECT_EQ(readRegion(*texture, *zeroed, 4), std::vector<uint8_t>(8, 0));
}

TEST(CommandExportDynamicTexture, RejectsDimensionsThatCannotBeRepresentedByTextureHandle) {
    Context context;
    DynamicTexture atlas(context, Size{4, 4}, gfx::TexturePixelType::RGBA);

    EXPECT_FALSE(atlas.addImage(nullptr, Size{0, 1}, 1));
    EXPECT_FALSE(atlas.addImage(nullptr, Size{1, 0}, 2));
    EXPECT_FALSE(atlas.addImage(nullptr, Size{std::numeric_limits<uint32_t>::max(), 1}, 3));
}

} // namespace
} // namespace command_export
} // namespace mln
