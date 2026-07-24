#pragma once

#include <mln/gfx/texture2d.hpp>

#include <vector>

namespace mln {
namespace command_export {

/// CPU-side texture that stores pixel data for an external consumer.
class Texture2D final : public gfx::Texture2D {
public:
    Texture2D();
    ~Texture2D() override;

    gfx::Texture2D& setSamplerConfiguration(const SamplerState&) noexcept override;
    gfx::Texture2D& setFormat(gfx::TexturePixelType, gfx::TextureChannelDataType) noexcept override;
    gfx::Texture2D& setSize(Size size_) noexcept override;
    gfx::Texture2D& setImage(std::shared_ptr<PremultipliedImage>) noexcept override;

    gfx::TexturePixelType getFormat() const noexcept override { return pixelType; }
    Size getSize() const noexcept override { return size; }
    size_t getDataSize() const noexcept override;
    size_t getPixelStride() const noexcept override;
    size_t numChannels() const noexcept override;

    void create() noexcept override;
    void upload(const void* pixelData, const Size& size_) noexcept override;
    void uploadSubRegion(const void* pixelData, const Size& size, uint16_t xOffset, uint16_t yOffset) noexcept override;
    void upload() noexcept override;
    bool needsUpload() const noexcept override { return dirty; }

    /// Access the CPU-side pixel data
    const std::vector<uint8_t>& getPixelData() const { return cpuData; }

    /// Identity for consumer-side GPU texture caching (unique per instance,
    /// version bumps on every upload).
    uint32_t getTextureId() const { return textureId; }
    uint32_t getVersion() const { return version; }
    gfx::TextureFilterType getSamplerFilter() const noexcept { return samplerState.filter; }

private:
    uint32_t textureId;
    uint32_t version = 0;
    Size size{0, 0};
    gfx::TexturePixelType pixelType = gfx::TexturePixelType::RGBA;
    gfx::TextureChannelDataType channelType = gfx::TextureChannelDataType::UnsignedByte;
    SamplerState samplerState;
    std::shared_ptr<PremultipliedImage> image;
    std::vector<uint8_t> cpuData;
    bool dirty = false;
    bool created = false;
};

} // namespace command_export
} // namespace mln
