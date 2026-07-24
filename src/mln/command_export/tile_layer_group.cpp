#include <mln/command_export/tile_layer_group.hpp>
#include <mln/command_export/drawable.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/gfx/drawable_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_orchestrator.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/convert.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace mln {
namespace command_export {

namespace {

struct ClippingMaskVertex {
    int16_t x;
    int16_t y;
};

// Process-lifetime geometry: DrawCommand stores direct pointers and the consumer
// reads them after native rendering has finished producing the frame.
constexpr std::array<ClippingMaskVertex, 4> clippingMaskVertices{{
    {0, 0},
    {static_cast<int16_t>(util::EXTENT), 0},
    {0, static_cast<int16_t>(util::EXTENT)},
    {static_cast<int16_t>(util::EXTENT), static_cast<int16_t>(util::EXTENT)},
}};
constexpr std::array<uint16_t, 6> clippingMaskIndices{{0, 1, 2, 1, 2, 3}};
static_assert(sizeof(ClippingMaskVertex) == 4);

void emitClippingMaskCommand(PaintParameters& parameters, const UnwrappedTileID& tileID) {
    const auto resolvedStencil = parameters.stencilModeForClipping(tileID);
    const auto matrix = util::cast<float>(parameters.matrixForTile(tileID));

    auto& command = getFrameData().addCommand(ShaderType::ClippingMask,
                                              DrawModeType::Triangles,
                                              clippingMaskVertices.data(),
                                              sizeof(ClippingMaskVertex),
                                              clippingMaskVertices.size(),
                                              clippingMaskIndices.data(),
                                              clippingMaskIndices.size());
    command.layerIndex = getCurrentLayerIndex();
    command.subLayerIndex = std::numeric_limits<int32_t>::min();
    command.stencilReference = static_cast<uint32_t>(resolvedStencil.ref);
    command.stencilMode = StencilModeType::ClippingMask;
    std::memcpy(command.drawableUBO, matrix.data(), sizeof(matrix));
    command.drawableUBOSize = sizeof(matrix);
}

} // namespace

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
    // A mask is deliberately emitted for every TileLayerGroup, even when
    // PaintParameters reuses the same ID map, so later command sorting cannot
    // move a dependent layer ahead of the mask it needs.
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
        for (const auto& tile : *stencilTiles) {
            emitClippingMaskCommand(parameters, tile.get().id);
        }
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
