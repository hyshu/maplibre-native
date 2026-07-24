#include <mln/command_export/headless_backend.hpp>
#include <mln/command_export/context.hpp>
#include <mln/gfx/backend_scope.hpp>

namespace mln {
namespace command_export {

class HeadlessRenderableResource final : public gfx::RenderableResource {
public:
    void bind() override {}
    Size getSize() const { return size; }
    void setSize(Size s) { size = s; }

private:
    Size size{0, 0};
};

HeadlessBackend::HeadlessBackend(Size size, SwapBehaviour swapBehaviour, gfx::ContextMode contextMode)
    : command_export::RendererBackend(contextMode),
      gfx::HeadlessBackend(size) {
    auto resource = std::make_unique<HeadlessRenderableResource>();
    resource->setSize(size);
    gfx::Renderable::setResource(std::move(resource));
}

HeadlessBackend::~HeadlessBackend() = default;

void HeadlessBackend::activate() {
    active = true;
}

void HeadlessBackend::deactivate() {
    active = false;
}

void HeadlessBackend::updateAssumedState() {
    // No GPU state to track
}

gfx::Renderable& HeadlessBackend::getDefaultRenderable() {
    return *this;
}

PremultipliedImage HeadlessBackend::readStillImage() {
    return readFramebuffer(getSize());
}

RendererBackend* HeadlessBackend::getRendererBackend() {
    return this;
}

} // namespace command_export

namespace gfx {

template <>
std::unique_ptr<gfx::HeadlessBackend> Backend::Create<gfx::Backend::Type::CommandExport>(
    const Size size, gfx::HeadlessBackend::SwapBehaviour swapBehavior, const gfx::ContextMode contextMode) {
    return std::make_unique<command_export::HeadlessBackend>(size, swapBehavior, contextMode);
}

} // namespace gfx
} // namespace mln
