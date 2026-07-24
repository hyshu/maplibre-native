#include <mln/command_export/layer_group.hpp>
#include <mln/command_export/drawable.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/renderer/render_orchestrator.hpp>

namespace mln {
namespace command_export {

LayerGroup::LayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name)
    : mln::LayerGroup(layerIndex, initialCapacity, std::move(name)) {}

void LayerGroup::upload(gfx::UploadPass&) {
    // CPU-only backend: data already in memory
}

void LayerGroup::render(RenderOrchestrator&, PaintParameters& parameters) {
    setCurrentLayerIndex(static_cast<uint32_t>(getLayerIndex()));
    const auto& layerUBOs = uniformBuffers;
    visitDrawables([&](gfx::Drawable& drawable) {
        if (!drawable.getEnabled()) return;
        drawable.mutableUniformBuffers().copyCpuDataFrom(layerUBOs);
        drawable.draw(parameters);
    });
}

} // namespace command_export
} // namespace mln
