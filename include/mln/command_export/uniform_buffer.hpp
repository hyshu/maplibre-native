#pragma once

#include <mln/gfx/uniform_buffer.hpp>

#include <vector>

namespace mln {
namespace command_export {

class UniformBuffer final : public gfx::UniformBuffer {
public:
    UniformBuffer(const void* data, std::size_t size);
    UniformBuffer(UniformBuffer&& other);
    ~UniformBuffer() override;

    UniformBuffer clone() const;
    void update(const void* data, std::size_t dataSize) override;

    const std::vector<uint8_t>& getData() const { return cpuData; }

private:
    std::vector<uint8_t> cpuData;
};

class UniformBufferArray final : public gfx::UniformBufferArray {
public:
    UniformBufferArray() = default;
    UniformBufferArray(UniformBufferArray&& other)
        : gfx::UniformBufferArray(std::move(other)) {}
    UniformBufferArray(const UniformBufferArray&) = delete;

    void bind(gfx::RenderPass&) override {}
    void unbind() {}

private:
    std::unique_ptr<gfx::UniformBuffer> copy(const gfx::UniformBuffer& buffer) override;
};

} // namespace command_export
} // namespace mln
