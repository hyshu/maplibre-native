#include <mln/command_export/tile_layer_group.hpp>
#include <mln/command_export/drawable.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/gfx/drawable_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_orchestrator.hpp>
#include <mln/renderer/render_tile.hpp>

#include <optional>

namespace mln {
namespace command_export {

TileLayerGroup::TileLayerGroup(int32_t layerIndex, std::size_t initialCapacity, std::string name)
    : mln::TileLayerGroup(layerIndex, initialCapacity, std::move(name)) {}

void TileLayerGroup::upload(gfx::UploadPass&) {
    // CPU-only backend: vertex/index data already in memory
}

void TileLayerGroup::render(RenderOrchestrator&, PaintParameters& parameters) {
    if (!enabled || !getDrawableCount() || !parameters.renderPass) {
        return;
    }

    setCurrentLayerIndex(static_cast<uint32_t>(getLayerIndex()));

    // Match the immediate backends: 3D drawables share one stencil reference
    // for the whole layer, while 2D drawables are clipped by per-tile masks.
    bool features3D = false;
    bool stencil3D = false;
    if (stencilTiles && !stencilTiles->empty()) {
        visitDrawables([&](const gfx::Drawable& drawable) {
            if (drawable.getEnabled() && drawable.getIs3D() && drawable.hasRenderPass(parameters.pass)) {
                features3D = true;
                stencil3D = stencil3D || drawable.getEnableStencil();
            }
        });
    }

    std::optional<int32_t> stencilReference3D;
    if (features3D) {
        if (stencil3D) {
            stencilReference3D = parameters.stencilModeFor3D().ref;
        }
    } else if (stencilTiles && !stencilTiles->empty()) {
        parameters.renderTileClippingMasks(stencilTiles);
    }

    const auto& layerUBOs = uniformBuffers;
    visitDrawables([&](gfx::Drawable& drawable) {
        if (!drawable.getEnabled() || !drawable.hasRenderPass(parameters.pass)) {
            return;
        }

        // Copy layer UBOs (e.g. EvaluatedPropsUBO) to drawable
        drawable.mutableUniformBuffers().copyCpuDataFrom(layerUBOs);

        for (const auto& tweaker : drawable.getTweakers()) {
            tweaker->execute(drawable, parameters);
        }

        if (features3D) {
            static_cast<Drawable&>(drawable).setStencilReferenceFor3D(
                drawable.getEnableStencil() ? stencilReference3D : std::optional<int32_t>{});
        }

        drawable.draw(parameters);
    });
}

} // namespace command_export
} // namespace mln
