#pragma once

#include <mln/command_export/texture2d.hpp>
#include <mln/gfx/offscreen_texture.hpp>

namespace mln {
namespace command_export {

/// A render target whose GPU storage belongs to the command consumer.
class OffscreenTexture final : public gfx::OffscreenTexture {
public:
    OffscreenTexture(Size size, gfx::TextureChannelDataType type)
        : gfx::OffscreenTexture(size, nullptr),
          texture(std::make_shared<Texture2D>()) {
        texture->setSize(size);
        texture->setFormat(gfx::TexturePixelType::RGBA, type);
        texture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Linear,
                                          .wrapU = gfx::TextureWrapType::Clamp,
                                          .wrapV = gfx::TextureWrapType::Clamp});
    }

    bool isRenderable() override { return !texture->getSize().isEmpty(); }

    /// Pixel readback is unavailable because the consumer owns the GPU image.
    PremultipliedImage readStillImage() override { return {}; }

    const gfx::Texture2DPtr& getTexture() override { return texture; }

private:
    gfx::Texture2DPtr texture;
};

} // namespace command_export
} // namespace mln
