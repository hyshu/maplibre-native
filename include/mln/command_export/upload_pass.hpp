#pragma once

#include <mln/gfx/upload_pass.hpp>

namespace mln {
namespace command_export {

class Context;

class UploadPass final : public gfx::UploadPass {
public:
    explicit UploadPass(Context& context);
    ~UploadPass() override;

    gfx::Context& getContext() override;
    const gfx::Context& getContext() const override;

    gfx::AttributeBindingArray buildAttributeBindings(
        const std::size_t vertexCount,
        const gfx::AttributeDataType vertexType,
        const std::size_t vertexAttributeIndex,
        const std::vector<std::uint8_t>& vertexData,
        const gfx::VertexAttributeArray& defaults,
        const gfx::VertexAttributeArray& overrides,
        gfx::BufferUsageType,
        const std::optional<std::chrono::duration<double>> lastUpdate,
        std::vector<std::unique_ptr<gfx::VertexBufferResource>>& outBuffers) override;

protected:
    std::unique_ptr<gfx::VertexBufferResource> createVertexBufferResource(const void* data,
                                                                          std::size_t size,
                                                                          gfx::BufferUsageType,
                                                                          bool persistent) override;
    void updateVertexBufferResource(gfx::VertexBufferResource&, const void* data, std::size_t size) override;

public:
    std::unique_ptr<gfx::IndexBufferResource> createIndexBufferResource(const void* data,
                                                                        std::size_t size,
                                                                        gfx::BufferUsageType,
                                                                        bool persistent) override;
    void updateIndexBufferResource(gfx::IndexBufferResource&, const void* data, std::size_t size) override;

    void pushDebugGroup(const char* name) override;
    void popDebugGroup() override;

private:
    Context& context;
};

} // namespace command_export
} // namespace mln
