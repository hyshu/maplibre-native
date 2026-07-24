#include <mln/command_export/tile_layer_group.hpp>
#include <mln/command_export/drawable.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/renderer/render_orchestrator.hpp>

namespace mln {
namespace command_export {

TileLayerGroup::TileLayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name)
    : mln::TileLayerGroup(layerIndex, initialCapacity, std::move(name)) {}

void TileLayerGroup::upload(gfx::UploadPass&) {
    // CPU-only backend: vertex/index data already in memory
}

void TileLayerGroup::render(RenderOrchestrator&, PaintParameters& parameters) {
    setCurrentLayerIndex(static_cast<uint32_t>(getLayerIndex()));
    const auto& layerUBOs = uniformBuffers;
    visitDrawables([&](gfx::Drawable& drawable) {
        if (!drawable.getEnabled()) return;
        // Copy layer UBOs (e.g. EvaluatedPropsUBO) to drawable
        drawable.mutableUniformBuffers().copyCpuDataFrom(layerUBOs);
        drawable.draw(parameters);
    });
}

} // namespace command_export
} // namespace mln
