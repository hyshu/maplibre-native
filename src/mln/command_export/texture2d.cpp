#include <mln/command_export/texture2d.hpp>
#include <mln/util/image.hpp>

#include <atomic>
#include <cstring>
#include <limits>
#include <optional>

namespace mln {
namespace command_export {
namespace {

std::optional<std::size_t> checkedDataSize(const Size& size, std::size_t stride) noexcept {
    const auto width = static_cast<std::size_t>(size.width);
    const auto height = static_cast<std::size_t>(size.height);
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    if ((width != 0 && height > max / width) || (width * height != 0 && stride > max / (width * height))) {
        return std::nullopt;
    }
    return width * height * stride;
}

} // namespace

Texture2D::Texture2D() {
    static std::atomic<uint32_t> nextTextureId{1};
    textureId = nextTextureId.fetch_add(1, std::memory_order_relaxed);
}

Texture2D::~Texture2D() = default;

gfx::Texture2D& Texture2D::setSamplerConfiguration(const SamplerState& state) noexcept {
    samplerState = state;
    return *this;
}

gfx::Texture2D& Texture2D::setFormat(gfx::TexturePixelType pixel, gfx::TextureChannelDataType channel) noexcept {
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
    return checkedDataSize(size, getPixelStride()).value_or(0);
}

void Texture2D::create() noexcept {
    if (!created) {
        const auto dataSize = checkedDataSize(size, getPixelStride());
        if (!dataSize) {
            return;
        }
        try {
            cpuData.assign(*dataSize, 0);
            created = true;
        } catch (...) {
            cpuData.clear();
        }
    }
}

void Texture2D::upload(const void* pixelData, const Size& size_) noexcept {
    size = size_;
    const auto dataSize = checkedDataSize(size, getPixelStride());
    if (!dataSize) {
        cpuData.clear();
        created = false;
        dirty = false;
        return;
    }
    try {
        cpuData.assign(*dataSize, 0);
        if (pixelData && *dataSize > 0) {
            std::memcpy(cpuData.data(), pixelData, *dataSize);
        }
    } catch (...) {
        cpuData.clear();
        created = false;
        dirty = false;
        return;
    }
    dirty = false;
    created = true;
    ++version;
}

void Texture2D::uploadSubRegion(const void* pixelData,
                                const Size& subSize,
                                uint16_t xOffset,
                                uint16_t yOffset) noexcept {
    if (subSize.isEmpty() || static_cast<uint64_t>(xOffset) + subSize.width > size.width ||
        static_cast<uint64_t>(yOffset) + subSize.height > size.height) {
        return;
    }
    if (!created) {
        create();
    }
    if (!created) {
        return;
    }
    const auto stride = getPixelStride();
    const auto sourceSize = checkedDataSize(subSize, stride);
    const auto destinationSize = checkedDataSize(size, stride);
    if (!sourceSize || !destinationSize || cpuData.size() != *destinationSize) {
        return;
    }
    const auto srcRowBytes = static_cast<std::size_t>(subSize.width) * stride;
    const auto dstRowBytes = static_cast<std::size_t>(size.width) * stride;

    for (uint32_t row = 0; row < subSize.height; ++row) {
        const auto srcOff = static_cast<std::size_t>(row) * srcRowBytes;
        const auto dstOff = static_cast<std::size_t>(yOffset + row) * dstRowBytes +
                            static_cast<std::size_t>(xOffset) * stride;
        if (pixelData) {
            std::memcpy(cpuData.data() + dstOff, static_cast<const uint8_t*>(pixelData) + srcOff, srcRowBytes);
        } else {
            std::memset(cpuData.data() + dstOff, 0, srcRowBytes);
        }
    }
    dirty = false;
    ++version;
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
