#include <mln/command_export/upload_pass.hpp>
#include <mln/command_export/context.hpp>
#include <mln/gfx/vertex_buffer.hpp>
#include <mln/gfx/index_buffer.hpp>
#include <mln/gfx/vertex_attribute.hpp>

#include <cstring>

namespace mln {
namespace command_export {

/// CPU-side buffer resource that just holds data in memory
class VertexBufferResource final : public gfx::VertexBufferResource {
public:
    VertexBufferResource(const void* data, std::size_t size) {
        if (data && size > 0) {
            cpuData.resize(size);
            std::memcpy(cpuData.data(), data, size);
        }
    }
    ~VertexBufferResource() override = default;

    void update(const void* data, std::size_t size) {
        cpuData.resize(size);
        if (data && size > 0) {
            std::memcpy(cpuData.data(), data, size);
        }
    }

    const std::vector<uint8_t>& getData() const { return cpuData; }

private:
    std::vector<uint8_t> cpuData;
};

class IndexBufferResource final : public gfx::IndexBufferResource {
public:
    IndexBufferResource(const void* data, std::size_t size) {
        if (data && size > 0) {
            cpuData.resize(size);
            std::memcpy(cpuData.data(), data, size);
        }
    }
    ~IndexBufferResource() override = default;

    void update(const void* data, std::size_t size) {
        cpuData.resize(size);
        if (data && size > 0) {
            std::memcpy(cpuData.data(), data, size);
        }
    }

    const std::vector<uint8_t>& getData() const { return cpuData; }

private:
    std::vector<uint8_t> cpuData;
};

UploadPass::UploadPass(Context& context_)
    : context(context_) {}

UploadPass::~UploadPass() = default;

gfx::Context& UploadPass::getContext() {
    return context;
}

const gfx::Context& UploadPass::getContext() const {
    return context;
}

std::unique_ptr<gfx::VertexBufferResource> UploadPass::createVertexBufferResource(
    const void* data, std::size_t size, gfx::BufferUsageType, bool) {
    return std::make_unique<VertexBufferResource>(data, size);
}

void UploadPass::updateVertexBufferResource(gfx::VertexBufferResource& resource,
                                             const void* data,
                                             std::size_t size) {
    static_cast<VertexBufferResource&>(resource).update(data, size);
}

std::unique_ptr<gfx::IndexBufferResource> UploadPass::createIndexBufferResource(
    const void* data, std::size_t size, gfx::BufferUsageType, bool) {
    return std::make_unique<IndexBufferResource>(data, size);
}

void UploadPass::updateIndexBufferResource(gfx::IndexBufferResource& resource,
                                            const void* data,
                                            std::size_t size) {
    static_cast<IndexBufferResource&>(resource).update(data, size);
}

gfx::AttributeBindingArray UploadPass::buildAttributeBindings(
    const std::size_t /*vertexCount*/,
    const gfx::AttributeDataType /*vertexType*/,
    const std::size_t /*vertexAttributeIndex*/,
    const std::vector<std::uint8_t>& /*vertexData*/,
    const gfx::VertexAttributeArray& /*defaults*/,
    const gfx::VertexAttributeArray& /*overrides*/,
    gfx::BufferUsageType,
    const std::optional<std::chrono::duration<double>> /*lastUpdate*/,
    std::vector<std::unique_ptr<gfx::VertexBufferResource>>& /*outBuffers*/) {
    // Return empty bindings — vertex data is read directly from CPU buffers
    return {};
}

void UploadPass::pushDebugGroup(const char*) {}
void UploadPass::popDebugGroup() {}

} // namespace command_export
} // namespace mln
