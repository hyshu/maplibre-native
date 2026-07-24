#include <mln/command_export/command_encoder.hpp>
#include <mln/command_export/context.hpp>
#include <mln/command_export/render_pass.hpp>
#include <mln/command_export/upload_pass.hpp>

namespace mln {
namespace command_export {

CommandEncoder::CommandEncoder(Context& context_)
    : context(context_) {}

CommandEncoder::~CommandEncoder() = default;

std::unique_ptr<gfx::UploadPass> CommandEncoder::createUploadPass(const char* /*name*/,
                                                                    gfx::Renderable&) {
    return std::make_unique<UploadPass>(context);
}

std::unique_ptr<gfx::RenderPass> CommandEncoder::createRenderPass(const char* /*name*/,
                                                                    const gfx::RenderPassDescriptor&) {
    return std::make_unique<RenderPass>();
}

void CommandEncoder::present(gfx::Renderable&) {
    // No-op: presentation is handled by the external renderer.
}

void CommandEncoder::pushDebugGroup(const char*) {}
void CommandEncoder::popDebugGroup() {}

} // namespace command_export
} // namespace mln
