#include <mln/test/util.hpp>

#include <mln/command_export/texture2d.hpp>

#include <cstdint>
#include <vector>

namespace mln {
namespace command_export {
namespace {

TEST(CommandExportTexture2D, PreservesSamplerFilterForCommandExport) {
    Texture2D texture;
    EXPECT_EQ(texture.getSamplerFilter(), gfx::TextureFilterType::Nearest);

    gfx::Texture2D::SamplerState sampler;
    sampler.filter = gfx::TextureFilterType::Linear;
    texture.setSamplerConfiguration(sampler);
    EXPECT_EQ(texture.getSamplerFilter(), gfx::TextureFilterType::Linear);

    sampler.filter = gfx::TextureFilterType::Nearest;
    texture.setSamplerConfiguration(sampler);
    EXPECT_EQ(texture.getSamplerFilter(), gfx::TextureFilterType::Nearest);
}

TEST(CommandExportTexture2D, NullUploadsZeroFillWithoutDereferencingNull) {
    Texture2D texture;
    texture.setSize(Size{2, 1});
    texture.setFormat(gfx::TexturePixelType::RGBA, gfx::TextureChannelDataType::UnsignedByte);

    const std::vector<uint8_t> initial{1, 2, 3, 4, 5, 6, 7, 8};
    texture.upload(initial.data(), Size{2, 1});
    texture.uploadSubRegion(nullptr, Size{1, 1}, 1, 0);

    EXPECT_EQ(texture.getPixelData(), (std::vector<uint8_t>{1, 2, 3, 4, 0, 0, 0, 0}));
    EXPECT_EQ(texture.getVersion(), 2u);

    texture.upload(nullptr, Size{2, 1});
    EXPECT_EQ(texture.getPixelData(), std::vector<uint8_t>(8, 0));
    EXPECT_EQ(texture.getVersion(), 3u);
}

TEST(CommandExportTexture2D, OutOfBoundsSubregionIsRejectedWithoutMutation) {
    Texture2D texture;
    texture.setSize(Size{2, 2});
    texture.setFormat(gfx::TexturePixelType::Alpha, gfx::TextureChannelDataType::UnsignedByte);
    const std::vector<uint8_t> initial{1, 2, 3, 4};
    const std::vector<uint8_t> patch{9, 9};
    texture.upload(initial.data(), Size{2, 2});

    texture.uploadSubRegion(patch.data(), Size{2, 1}, 1, 0);
    texture.uploadSubRegion(patch.data(), Size{1, 2}, 0, 1);

    EXPECT_EQ(texture.getPixelData(), initial);
    EXPECT_EQ(texture.getVersion(), 1u);
}

} // namespace
} // namespace command_export
} // namespace mln
