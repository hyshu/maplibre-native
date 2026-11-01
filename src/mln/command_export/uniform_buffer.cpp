#include <mln/command_export/uniform_buffer.hpp>

#include <cstring>

namespace mln {
namespace command_export {

UniformBuffer::UniformBuffer(const void* data, std::size_t size)
    : gfx::UniformBuffer(size) {
    if (data && size > 0) {
        cpuData.resize(size);
        std::memcpy(cpuData.data(), data, size);
        setCpuData(data, size); // also set base class cpuData for copyCpuDataFrom
    }
}

UniformBuffer::UniformBuffer(UniformBuffer&& other)
    : gfx::UniformBuffer(std::move(other)),
      cpuData(std::move(other.cpuData)) {}

UniformBuffer::~UniformBuffer() = default;

UniformBuffer UniformBuffer::clone() const {
    return UniformBuffer(cpuData.data(), cpuData.size());
}

void UniformBuffer::update(const void* data, std::size_t dataSize) {
    if (data && dataSize > 0) {
        cpuData.resize(dataSize);
        std::memcpy(cpuData.data(), data, dataSize);
        setCpuData(data, dataSize); // also set base class cpuData
        size = dataSize;
    }
}

std::unique_ptr<gfx::UniformBuffer> UniformBufferArray::copy(const gfx::UniformBuffer& buffer) {
    const auto& src = static_cast<const UniformBuffer&>(buffer);
    auto clone = std::make_unique<UniformBuffer>(src.clone());
    return clone;
}

} // namespace command_export
} // namespace mln
