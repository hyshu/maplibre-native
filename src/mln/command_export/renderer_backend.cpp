#include <mln/command_export/renderer_backend.hpp>
#include <mln/command_export/context.hpp>
#include <mln/command_export/shader_group.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/shaders/program_parameters.hpp>
#include <mln/util/logging.hpp>

namespace mln {
namespace command_export {

RendererBackend::RendererBackend(gfx::ContextMode contextMode)
    : gfx::RendererBackend(contextMode) {}

RendererBackend::~RendererBackend() = default;

std::unique_ptr<gfx::Context> RendererBackend::createContext() {
    return std::make_unique<Context>();
}

void RendererBackend::initShaders(gfx::ShaderRegistry& registry,
                                    const ProgramParameters&) {
    // Register stub shader groups for all built-in shaders.
    // Command Export does not compile GPU shaders; the external renderer selects pipelines.
    // These stubs let MapLibre's layer system create drawables.
    static const char* shaderNames[] = {
        "BackgroundShader",
        "BackgroundPatternShader",
        "CircleShader",
        "ClippingMaskProgram",
        "CollisionBoxShader",
        "CollisionCircleShader",
        "CustomGeometryShader",
        "CustomSymbolIconShader",
        "DebugShader",
        "FillShader",
        "FillOutlineShader",
        "FillPatternShader",
        "FillOutlinePatternShader",
        "FillOutlineTriangulatedShader",
        "FillExtrusionShader",
        "FillExtrusionPatternShader",
        "HeatmapShader",
        "HeatmapTextureShader",
        "HillshadeShader",
        "HillshadePrepareShader",
        "ColorReliefShader",
        "LineShader",
        "LineGradientShader",
        "LineSDFShader",
        "LinePatternShader",
        "LocationIndicatorShader",
        "LocationIndicatorTexturedShader",
        "RasterShader",
        "SymbolIconShader",
        "SymbolSDFShader",
        "SymbolTextAndIconShader",
        "WideVectorShader",
    };

    for (const auto* name : shaderNames) {
        auto group = std::make_shared<StubShaderGroup>(name);
        (void)registry.registerShaderGroup(std::move(group), name);
    }
}

PremultipliedImage RendererBackend::readFramebuffer(const Size& size) {
    return PremultipliedImage(size);
}

} // namespace command_export
} // namespace mln
