#pragma once

#include <mln/renderer/layer_group.hpp>
#include <mln/command_export/uniform_buffer.hpp>

namespace mln {

class PaintParameters;

namespace command_export {

class LayerGroup : public mln::LayerGroup {
public:
    LayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name);
    ~LayerGroup() override {}

    void upload(gfx::UploadPass&) override;
    void render(RenderOrchestrator&, PaintParameters&) override;

    const gfx::UniformBufferArray& getUniformBuffers() const override { return uniformBuffers; }
    gfx::UniformBufferArray& mutableUniformBuffers() override { return uniformBuffers; }

protected:
    UniformBufferArray uniformBuffers;
};

} // namespace command_export
} // namespace mln
