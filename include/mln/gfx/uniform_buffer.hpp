#pragma once

#include <mln/shaders/shader_defines.hpp>

#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>

namespace mln {
namespace gfx {

class Context;
class UniformBuffer;
class UniformBufferArray;
class RenderPass;

using UniformBufferPtr = std::shared_ptr<UniformBuffer>;
using UniqueUniformBuffer = std::unique_ptr<UniformBuffer>;
using UniqueUniformBufferArray = std::unique_ptr<UniformBufferArray>;

class UniformBuffer {
    // Can only be created by platform specific implementations
protected:
    UniformBuffer(std::size_t size_)
        : size(size_) {}
    UniformBuffer(const UniformBuffer& other)
        : size(other.size) {}
    UniformBuffer(UniformBuffer&& other)
        : size(other.size) {}

public:
    virtual ~UniformBuffer() = default;
    virtual void update(const void* data, std::size_t dataSize) = 0;

    std::size_t getSize() const { return size; }

    /// CPU-side data copy (updated by emplaceOrUpdate)
    const std::vector<uint8_t>& getCpuData() const { return cpuData; }
    void setCpuData(const void* data, size_t dataSize) {
        cpuData.resize(dataSize);
        if (data && dataSize > 0) std::memcpy(cpuData.data(), data, dataSize);
    }

    UniformBuffer& operator=(const UniformBuffer&) = delete;

protected:
    UniformBuffer& operator=(UniformBuffer&& other) {
        size = other.size;
        return *this;
    }

protected:
    std::size_t size;
    std::vector<uint8_t> cpuData;
};

/// Stores a collection of uniform buffers by id
class UniformBufferArray {
public:
    UniformBufferArray() = default;
    UniformBufferArray(UniformBufferArray&&);
    // Would need to use the virtual assignment operator
    UniformBufferArray(const UniformBufferArray&) = delete;
    virtual ~UniformBufferArray() = default;

    /// Number of maximum allocated elements
    std::size_t allocatedSize() const { return uniformBufferVector.size(); }

    /// Get an uniform buffer element.
    /// Returns a pointer to the element on success, or null if the uniform buffer doesn't exists.
    const std::shared_ptr<UniformBuffer>& get(const size_t id) const;

    /// Set a new uniform buffer element or replace the existing one.
    virtual const std::shared_ptr<UniformBuffer>& set(const size_t id, std::shared_ptr<UniformBuffer> uniformBuffer);

    /// Create and add a new buffer or update an existing one
    void createOrUpdate(const size_t id, const std::vector<uint8_t>& data, gfx::Context&, bool persistent = false);
    virtual void createOrUpdate(
        const size_t id, const void* data, std::size_t size, gfx::Context&, bool persistent = false);
    template <typename T>
    void createOrUpdate(const size_t id, const T* data, gfx::Context& context, bool persistent = false)
        requires(!std::is_pointer_v<T>)
    {
        createOrUpdate(id, data, sizeof(T), context, persistent);
    }

    virtual void bind(gfx::RenderPass& renderPass) = 0;

    /// Get CPU-side copy of UBO data captured during createOrUpdate.
    /// Returns {data, size} or {nullptr, 0} if not captured.
    std::pair<const void*, size_t> getCpuCopy(size_t id) const {
        auto it = cpuCopies.find(id);
        if (it == cpuCopies.end() || it->second.empty()) return {nullptr, 0};
        return {it->second.data(), it->second.size()};
    }

    /// Copy CPU-side cached data from another array (e.g., layer group → drawable)
    void copyCpuDataFrom(const UniformBufferArray& other);

    UniformBufferArray& operator=(UniformBufferArray&&);
    UniformBufferArray& operator=(const UniformBufferArray&);

protected:
    virtual std::unique_ptr<UniformBuffer> copy(const UniformBuffer&) = 0;

protected:
    std::array<UniformBufferPtr, shaders::maxUBOCountPerShader> uniformBufferVector;
    static std::shared_ptr<UniformBuffer> nullref;
    std::unordered_map<size_t, std::vector<uint8_t>> cpuCopies;
};

} // namespace gfx
} // namespace mln
