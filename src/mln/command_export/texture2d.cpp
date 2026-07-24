#include <mln/command_export/texture2d.hpp>
#include <mln/util/image.hpp>

#include <cstring>

namespace mln {
namespace command_export {

Texture2D::~Texture2D() = default;

gfx::Texture2D& Texture2D::setSamplerConfiguration(const SamplerState& state) noexcept {
    samplerState = state;
    return *this;
}

gfx::Texture2D& Texture2D::setFormat(gfx::TexturePixelType pixel,
                                       gfx::TextureChannelDataType channel) noexcept {
    pixelType = pixel;
    channelType = channel;
    return *this;
}

gfx::Texture2D& Texture2D::setSize(Size size_) noexcept {
    size = size_;
    return *this;
}

gfx::Texture2D& Texture2D::setImage(std::shared_ptr<PremultipliedImage> image_) noexcept {
    image = std::move(image_);
    dirty = true;
    return *this;
}

size_t Texture2D::numChannels() const noexcept {
    switch (pixelType) {
        case gfx::TexturePixelType::RGBA:
            return 4;
        case gfx::TexturePixelType::Alpha:
            return 1;
        case gfx::TexturePixelType::Stencil:
            return 1;
        case gfx::TexturePixelType::Depth:
            return 1;
        case gfx::TexturePixelType::Luminance:
            return 1;
        default:
            return 4;
    }
}

size_t Texture2D::getPixelStride() const noexcept {
    size_t channelSize = 1; // UnsignedByte
    switch (channelType) {
        case gfx::TextureChannelDataType::UnsignedByte:
            channelSize = 1;
            break;
        case gfx::TextureChannelDataType::HalfFloat:
            channelSize = 2;
            break;
        case gfx::TextureChannelDataType::Float:
            channelSize = 4;
            break;
    }
    return numChannels() * channelSize;
}

size_t Texture2D::getDataSize() const noexcept {
    return static_cast<size_t>(size.width) * size.height * getPixelStride();
}

void Texture2D::create() noexcept {
    if (!created) {
        cpuData.resize(getDataSize(), 0);
        created = true;
    }
}

void Texture2D::upload(const void* pixelData, const Size& size_) noexcept {
    size = size_;
    const auto dataSize = getDataSize();
    cpuData.resize(dataSize);
    if (pixelData && dataSize > 0) {
        std::memcpy(cpuData.data(), pixelData, dataSize);
    }
    dirty = false;
    created = true;
}

void Texture2D::uploadSubRegion(const void* pixelData,
                                 const Size& subSize,
                                 uint16_t xOffset,
                                 uint16_t yOffset) noexcept {
    if (!created) {
        create();
    }
    const auto stride = getPixelStride();
    const auto srcRowBytes = subSize.width * stride;
    const auto dstRowBytes = size.width * stride;

    for (uint32_t row = 0; row < subSize.height; ++row) {
        const auto srcOff = row * srcRowBytes;
        const auto dstOff = (yOffset + row) * dstRowBytes + xOffset * stride;
        if (dstOff + srcRowBytes <= cpuData.size()) {
            std::memcpy(cpuData.data() + dstOff,
                       static_cast<const uint8_t*>(pixelData) + srcOff,
                       srcRowBytes);
        }
    }
    dirty = false;
}

void Texture2D::upload() noexcept {
    if (image) {
        upload(image->data.get(), image->size);
        image.reset();
    }
    dirty = false;
}

} // namespace command_export
} // namespace mln
