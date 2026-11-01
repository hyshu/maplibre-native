#include <mln/command_export/dynamic_texture.hpp>

#include <mln/command_export/context.hpp>
#include <mln/command_export/texture2d.hpp>

#include <cstring>
#include <limits>
#include <optional>

namespace mln {
namespace command_export {
namespace {

std::size_t pixelStride(gfx::TexturePixelType pixelType) noexcept {
    return pixelType == gfx::TexturePixelType::RGBA ? 4u : 1u;
}

std::optional<std::size_t> checkedByteSize(const Size& size, gfx::TexturePixelType pixelType) noexcept {
    const auto width = static_cast<std::size_t>(size.width);
    const auto height = static_cast<std::size_t>(size.height);
    const auto stride = pixelStride(pixelType);
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    if ((width != 0 && height > max / width) || (width * height != 0 && stride > max / (width * height))) {
        return std::nullopt;
    }
    return width * height * stride;
}

bool isValidAtlasRect(const Rect<uint16_t>& rect, const Size& atlasSize) noexcept {
    const auto x = static_cast<uint64_t>(rect.x);
    const auto y = static_cast<uint64_t>(rect.y);
    const auto width = static_cast<uint64_t>(rect.w);
    const auto height = static_cast<uint64_t>(rect.h);
    return width > 0 && height > 0 && x + width <= static_cast<uint64_t>(atlasSize.width) &&
           y + height <= static_cast<uint64_t>(atlasSize.height);
}

bool matchesHandle(const mapbox::Bin& bin, const gfx::TextureHandle& handle) noexcept {
    const auto& rect = handle.getRectangle();
    return bin.refcount() > 0 && bin.x == rect.x && bin.y == rect.y && bin.w == rect.w && bin.h == rect.h;
}

} // namespace

DynamicTexture::DynamicTexture(Context& context_, Size size_, gfx::TexturePixelType pixelType_)
    : gfx::DynamicTexture(size_, pixelType_),
      context(context_),
      atlasSize(size_),
      pixelType(pixelType_) {}

void DynamicTexture::uploadImage(const uint8_t* pixelData, gfx::TextureHandle& texHandle) {
    std::scoped_lock lock(mutex);
    const auto* bin = shelfPack.getBin(texHandle.getId());
    const auto& rect = texHandle.getRectangle();
    const auto byteSize = checkedByteSize(Size(rect.w, rect.h), pixelType);

    if (bin && matchesHandle(*bin, texHandle) && isValidAtlasRect(rect, atlasSize) && byteSize) {
        std::vector<uint8_t> imageData(*byteSize, 0);
        if (pixelData && *byteSize > 0) {
            std::memcpy(imageData.data(), pixelData, *byteSize);
        }
        // A duplicate uniqueId has needsUpload=false and never reaches this
        // method. try_emplace also keeps the first owned upload if raced.
        imagesToUpload.try_emplace(texHandle, std::move(imageData));
    }

    // We already own the base mutex; the base implementation only updates the
    // handle flag and intentionally does not lock.
    gfx::DynamicTexture::uploadImage(pixelData, texHandle);
}

void DynamicTexture::uploadDeferredImages(gfx::UploadPass&) {
    std::scoped_lock lock(mutex);
    if (deferredCreation) {
        texture = context.createTexture2D();
        texture->setSize(atlasSize);
        texture->setFormat(pixelType, gfx::TextureChannelDataType::UnsignedByte);
        texture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Linear,
                                          .wrapU = gfx::TextureWrapType::Clamp,
                                          .wrapV = gfx::TextureWrapType::Clamp});
        texture->create();
        deferredCreation = false;
    }

    for (const auto& [handle, data] : imagesToUpload) {
        const auto& rect = handle.getRectangle();
        const auto expectedSize = checkedByteSize(Size(rect.w, rect.h), pixelType);
        if (!isValidAtlasRect(rect, atlasSize) || !expectedSize || data.size() != *expectedSize) {
            continue;
        }
        texture->uploadSubRegion(data.data(), Size(rect.w, rect.h), rect.x, rect.y);
    }
    imagesToUpload.clear();
}

bool DynamicTexture::removeTexture(const gfx::TextureHandle& texHandle) {
    std::scoped_lock lock(mutex);
    auto* bin = shelfPack.getBin(texHandle.getId());
    if (!bin || !matchesHandle(*bin, texHandle)) {
        return false;
    }

    const auto refcount = shelfPack.unref(*bin);
    if (refcount != 0) {
        return false;
    }

    if (numTextures > 0) {
        --numTextures;
    }
    imagesToUpload.erase(texHandle);
    return true;
}

} // namespace command_export
} // namespace mln
