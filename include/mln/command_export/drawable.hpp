#pragma once

#include <mln/gfx/drawable.hpp>
#include <mln/command_export/uniform_buffer.hpp>

namespace mln {
namespace command_export {

class Drawable final : public gfx::Drawable {
public:
    Drawable(std::string name);
    ~Drawable() override;

    void draw(PaintParameters&) const override;

    void setIndexData(gfx::IndexVectorBasePtr, std::vector<UniqueDrawSegment> segments) override;
    void setVertices(std::vector<uint8_t>&&, std::size_t, gfx::AttributeDataType) override;

    const gfx::UniformBufferArray& getUniformBuffers() const override { return uniformBuffers; }
    gfx::UniformBufferArray& mutableUniformBuffers() override { return uniformBuffers; }

    void setColorMode(const gfx::ColorMode&) override;
    void setShader(gfx::ShaderProgramBasePtr) override;
    void setEnableStencil(bool) override;
    void setEnableDepth(bool) override;
    void setSubLayerIndex(int32_t) override;
    void setDepthType(gfx::DepthMaskType) override;

    void updateVertexAttributes(gfx::VertexAttributeArrayPtr,
                                std::size_t vertexCount,
                                gfx::DrawMode,
                                gfx::IndexVectorBasePtr,
                                const SegmentBase* segments,
                                std::size_t segmentCount) override;

    /// Access CPU-side vertex data
    const std::vector<uint8_t>& getVertexData() const { return vertexData; }
    std::size_t getVertexCount() const { return vertexCount; }

    /// Access CPU-side index data
    const gfx::IndexVectorBasePtr& getIndexVector() const { return indexVector; }

    /// Unique buffer identity for external GPU buffer caching.
    uint32_t getBufferId() const { return bufferId; }
    uint32_t getBufferVersion() const { return bufferVersion; }

private:
    uint32_t bufferId;
    uint32_t bufferVersion = 0;

    UniformBufferArray uniformBuffers;

    // CPU-side vertex storage
    std::vector<uint8_t> vertexData;
    std::size_t vertexCount = 0;
    gfx::AttributeDataType vertexType{};

    // CPU-side index storage
    gfx::IndexVectorBasePtr indexVector;
    std::vector<UniqueDrawSegment> segments;
};

} // namespace command_export
} // namespace mln
