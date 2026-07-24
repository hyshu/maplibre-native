#pragma once

#include <mln/gfx/context.hpp>
#include <mln/command_export/uniform_buffer.hpp>

namespace mln {
namespace command_export {

class Context final : public gfx::Context {
public:
    Context();
    ~Context() noexcept override;

    void beginFrame() override;
    void endFrame() override;
    std::unique_ptr<gfx::CommandEncoder> createCommandEncoder() override;
    void performCleanup() override;
    void reduceMemoryUsage() override {}

    gfx::UniqueDrawableBuilder createDrawableBuilder(std::string name) override;
    gfx::UniformBufferPtr createUniformBuffer(const void* data,
                                               std::size_t size,
                                               bool persistent = false,
                                               bool ssbo = false) override;
    gfx::UniqueUniformBufferArray createLayerUniformBufferArray() override;
    gfx::ShaderProgramBasePtr getGenericShader(gfx::ShaderRegistry&, const std::string& name) override;

    TileLayerGroupPtr createTileLayerGroup(int32_t layerIndex,
                                            std::size_t initialCapacity,
                                            std::string name) override;
    LayerGroupPtr createLayerGroup(int32_t layerIndex,
                                    std::size_t initialCapacity,
                                    std::string name) override;

    gfx::Texture2DPtr createTexture2D() override;
    gfx::DynamicTexturePtr createDynamicTexture(Size size, gfx::TexturePixelType pixelType) override;
    RenderTargetPtr createRenderTarget(const Size size, const gfx::TextureChannelDataType type) override;

    void resetState(gfx::DepthMode depthMode, gfx::ColorMode colorMode) override;
    void setDirtyState() override;

    std::unique_ptr<gfx::OffscreenTexture> createOffscreenTexture(Size, gfx::TextureChannelDataType) override;
    std::unique_ptr<gfx::RenderbufferResource> createRenderbufferResource(gfx::RenderbufferPixelType, Size) override;
    std::unique_ptr<gfx::DrawScopeResource> createDrawScopeResource() override;

    gfx::VertexAttributeArrayPtr createVertexAttributeArray() const override;

#if !defined(NDEBUG)
    void visualizeStencilBuffer() override;
    void visualizeDepthBuffer(float depthRangeSize) override;
#endif
    void clearStencilBuffer(int32_t) override;

    bool emplaceOrUpdateUniformBuffer(gfx::UniformBufferPtr&,
                                       const void* data,
                                       std::size_t size,
                                       bool persistent = false) override;

    const gfx::UniformBufferArray& getGlobalUniformBuffers() const override { return globalUniformBuffers; }
    gfx::UniformBufferArray& mutableGlobalUniformBuffers() override { return globalUniformBuffers; }
    void bindGlobalUniformBuffers(gfx::RenderPass&) const noexcept override;
    void unbindGlobalUniformBuffers(gfx::RenderPass&) const noexcept override {}

private:
    UniformBufferArray globalUniformBuffers;
};

} // namespace command_export
} // namespace mln
