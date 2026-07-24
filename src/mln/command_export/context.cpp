#include <mln/command_export/context.hpp>
#include <mln/command_export/command_encoder.hpp>
#include <mln/command_export/dynamic_texture.hpp>
#include <mln/command_export/drawable_builder.hpp>
#include <mln/command_export/layer_group.hpp>
#include <mln/command_export/render_pass.hpp>
#include <mln/command_export/texture2d.hpp>
#include <mln/command_export/tile_layer_group.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/gfx/offscreen_texture.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/gfx/vertex_attribute.hpp>
#include <mln/gfx/color_mode.hpp>
#include <mln/gfx/depth_mode.hpp>
#include <mln/util/logging.hpp>

namespace mln {
namespace command_export {

Context::Context()
    : gfx::Context(32) {}

Context::~Context() noexcept = default;

void Context::beginFrame() {}

void Context::endFrame() {}

std::unique_ptr<gfx::CommandEncoder> Context::createCommandEncoder() {
    return std::make_unique<CommandEncoder>(*this);
}

void Context::performCleanup() {}

gfx::UniqueDrawableBuilder Context::createDrawableBuilder(std::string name) {
    return std::make_unique<DrawableBuilder>(std::move(name));
}

gfx::UniformBufferPtr Context::createUniformBuffer(const void* data,
                                                   std::size_t size,
                                                   bool /*persistent*/,
                                                   bool /*ssbo*/) {
    return std::make_shared<UniformBuffer>(data, size);
}

gfx::UniqueUniformBufferArray Context::createLayerUniformBufferArray() {
    return std::make_unique<UniformBufferArray>();
}

gfx::ShaderProgramBasePtr Context::getGenericShader(gfx::ShaderRegistry& shaders, const std::string& name) {
    const auto shaderGroup = shaders.getShaderGroup(name);
    if (!shaderGroup) return {};
    auto shader = shaderGroup->getOrCreateShader(*this, {});
    return std::static_pointer_cast<gfx::ShaderProgramBase>(std::move(shader));
}

TileLayerGroupPtr Context::createTileLayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name) {
    return std::make_shared<command_export::TileLayerGroup>(layerIndex, initialCapacity, std::move(name));
}

LayerGroupPtr Context::createLayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name) {
    return std::make_shared<command_export::LayerGroup>(layerIndex, initialCapacity, std::move(name));
}

gfx::Texture2DPtr Context::createTexture2D() {
    return std::make_shared<Texture2D>();
}

gfx::DynamicTexturePtr Context::createDynamicTexture(Size size, gfx::TexturePixelType pixelType) {
    return std::make_shared<command_export::DynamicTexture>(*this, size, pixelType);
}

RenderTargetPtr Context::createRenderTarget(const Size /*size*/, const gfx::TextureChannelDataType /*type*/) {
    // TODO: implement when needed
    return nullptr;
}

void Context::resetState(gfx::DepthMode, gfx::ColorMode) {}

void Context::setDirtyState() {}

std::unique_ptr<gfx::OffscreenTexture> Context::createOffscreenTexture(Size, gfx::TextureChannelDataType) {
    // TODO: implement when needed
    return nullptr;
}

std::unique_ptr<gfx::RenderbufferResource> Context::createRenderbufferResource(gfx::RenderbufferPixelType, Size) {
    return nullptr;
}

std::unique_ptr<gfx::DrawScopeResource> Context::createDrawScopeResource() {
    return nullptr;
}

gfx::VertexAttributeArrayPtr Context::createVertexAttributeArray() const {
    return std::make_shared<gfx::VertexAttributeArray>();
}

#if !defined(NDEBUG)
void Context::visualizeStencilBuffer() {}
void Context::visualizeDepthBuffer(float) {}
#endif

void Context::clearStencilBuffer(int32_t) {}

bool Context::emplaceOrUpdateUniformBuffer(gfx::UniformBufferPtr& buffer,
                                           const void* data,
                                           std::size_t size,
                                           bool persistent) {
    if (buffer) {
        buffer->update(data, size);
    } else {
        buffer = createUniformBuffer(data, size, persistent);
    }
    return !buffer;
}

void Context::bindGlobalUniformBuffers(gfx::RenderPass&) const noexcept {}

} // namespace command_export
} // namespace mln
