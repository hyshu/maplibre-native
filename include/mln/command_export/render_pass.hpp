#pragma once

#include <mln/gfx/render_pass.hpp>
#include <mln/command_export/texture2d.hpp>

namespace mln {
namespace command_export {

/// Tracks the output texture while an external consumer performs rendering.
class RenderPass final : public gfx::RenderPass {
public:
    explicit RenderPass(std::shared_ptr<Texture2D> target_ = {})
        : target(std::move(target_)) {}
    ~RenderPass() override = default;

    const std::shared_ptr<Texture2D>& getTarget() const { return target; }

protected:
    void pushDebugGroup(const char*) override {}
    void popDebugGroup() override {}
    void addDebugSignpost(const char*) override {}

private:
    std::shared_ptr<Texture2D> target;
};

} // namespace command_export
} // namespace mln
