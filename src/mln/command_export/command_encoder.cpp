#include <mln/command_export/command_encoder.hpp>
#include <mln/command_export/context.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/command_export/render_pass.hpp>
#include <mln/command_export/upload_pass.hpp>
#include <mln/gfx/render_pass.hpp>

#include <cstring>

namespace mln {
namespace command_export {

CommandEncoder::CommandEncoder(Context& context_)
    : context(context_) {}

CommandEncoder::~CommandEncoder() = default;

std::unique_ptr<gfx::UploadPass> CommandEncoder::createUploadPass(const char* /*name*/, gfx::Renderable&) {
    return std::make_unique<UploadPass>(context);
}

std::unique_ptr<gfx::RenderPass> CommandEncoder::createRenderPass(const char* name,
                                                                  const gfx::RenderPassDescriptor& descriptor) {
    // MapLibre can optimize the first solid background layer into the clear
    // color of its main render pass. Preserve that decision for the external
    // render target instead of inventing a backend-specific background.
    if (name && std::strcmp(name, "main buffer") == 0) {
        auto& frame = getFrameData();
        if (descriptor.clearColor) {
            const auto& color = *descriptor.clearColor;
            frame.clearColor = std::array<float, 4>{color.r, color.g, color.b, color.a};
        } else {
            frame.clearColor.reset();
        }
    }
    return std::make_unique<RenderPass>();
}

void CommandEncoder::present(gfx::Renderable&) {
    // No-op: presentation is owned by the external renderer.
}

void CommandEncoder::pushDebugGroup(const char*) {}
void CommandEncoder::popDebugGroup() {}

} // namespace command_export
} // namespace mln
