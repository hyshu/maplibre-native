#include <mln/gfx/dynamic_texture.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/gfx/context.hpp>

#include <limits>

namespace mln {
namespace gfx {

DynamicTexture::DynamicTexture(Size size, TexturePixelType pixelType)
    : pixelFormat(pixelType) {
    mapbox::ShelfPack::ShelfPackOptions options;
    options.autoResize = false;
    shelfPack = mapbox::ShelfPack(size.width, size.height, options);
}

DynamicTexture::DynamicTexture(Context& context, Size size, TexturePixelType pixelType)
    : DynamicTexture(size, pixelType) {
    texture = context.createTexture2D();
    texture->setSize(size);
    texture->setFormat(pixelType, gfx::TextureChannelDataType::UnsignedByte);
    texture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Linear,
                                      .wrapU = gfx::TextureWrapType::Clamp,
                                      .wrapV = gfx::TextureWrapType::Clamp});
}

const gfx::Texture2DPtr& DynamicTexture::getTexture() const {
    assert(texture);
    return texture;
}

TexturePixelType DynamicTexture::getPixelFormat() const {
    return pixelFormat;
}

bool DynamicTexture::isEmpty() const {
    return (numTextures == 0);
}

std::optional<TextureHandle> DynamicTexture::reserveSize(const Size& size, int32_t uniqueId) {
    std::scoped_lock lock(mutex);
    constexpr auto maxBinValue = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max());
    if (size.isEmpty() || size.width > maxBinValue || size.height > maxBinValue) {
        return std::nullopt;
    }
    mapbox::Bin* bin = shelfPack.packOne(uniqueId, size.width, size.height);
    if (!bin) {
        return std::nullopt;
    }
    if (bin->x < 0 || bin->y < 0 || bin->w <= 0 || bin->h <= 0 || static_cast<uint32_t>(bin->x) > maxBinValue ||
        static_cast<uint32_t>(bin->y) > maxBinValue || static_cast<uint32_t>(bin->w) > maxBinValue ||
        static_cast<uint32_t>(bin->h) > maxBinValue) {
        shelfPack.unref(*bin);
        return std::nullopt;
    }
    if (bin->refcount() == 1) {
        numTextures++;
    }
    return TextureHandle(*bin);
}

std::optional<TextureHandle> DynamicTexture::addImage(const uint8_t* pixelData,
                                                      const Size& imageSize,
                                                      int32_t uniqueId) {
    auto texHandle = reserveSize(imageSize, uniqueId);
    if (texHandle && texHandle->isUploadNeeded()) {
        uploadImage(pixelData, *texHandle);
    }
    return texHandle;
}

void DynamicTexture::uploadImage(const uint8_t* /*pixelData*/, gfx::TextureHandle& texHandle) {
    texHandle.needsUpload = false;
}

bool DynamicTexture::removeTexture(const TextureHandle& texHandle) {
    std::scoped_lock lock(mutex);
    auto* bin = shelfPack.getBin(texHandle.getId());
    if (!bin) {
        return false;
    }
    auto refcount = shelfPack.unref(*bin);
    if (refcount == 0) {
        numTextures--;
        return true;
    }
    return false;
}

} // namespace gfx
} // namespace mln
