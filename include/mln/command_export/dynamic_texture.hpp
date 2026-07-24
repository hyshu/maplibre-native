#pragma once

#include <mln/gfx/dynamic_texture.hpp>

#include <unordered_map>
#include <vector>

namespace mln {
namespace command_export {

class Context;

/// Deferred CPU atlas used by the Command Export backend.
///
/// Image bytes are copied when they are added because the caller-owned image
/// may be released before GeometryTile uploads its deferred atlas images.
class DynamicTexture final : public gfx::DynamicTexture {
public:
    DynamicTexture(Context& context, Size size, gfx::TexturePixelType pixelType);

    void uploadImage(const uint8_t* pixelData, gfx::TextureHandle& texHandle) override;
    void uploadDeferredImages(gfx::UploadPass&) override;
    bool removeTexture(const gfx::TextureHandle& texHandle) override;

private:
    using ImagesToUpload = std::unordered_map<gfx::TextureHandle, std::vector<uint8_t>, gfx::TextureHandle::Hasher>;

    Context& context;
    const Size atlasSize;
    const gfx::TexturePixelType pixelType;
    bool deferredCreation = true;
    ImagesToUpload imagesToUpload;
};

} // namespace command_export
} // namespace mln
