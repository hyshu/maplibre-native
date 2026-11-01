#include <mln/test/util.hpp>

#include <mln/command_export/drawable.hpp>
#include <mln/gfx/draw_mode.hpp>
#include <mln/shaders/segment.hpp>

namespace mln::command_export {
namespace {

TEST(CommandExportDrawable, TracksSegmentChangesWithoutInvalidatingIdenticalUpdates) {
    Drawable drawable("segment-cache");
    const auto update = [&](std::size_t baseInstance, std::size_t instanceCount, float sortKey) {
        const SegmentBase segment{0, 0, 4, 6, baseInstance, instanceCount, sortKey};
        drawable.updateVertexAttributes(nullptr, 4, gfx::Triangles(), nullptr, &segment, 1);
    };

    update(0, 1, 1.5f);
    auto version = drawable.getBufferVersion();
    update(0, 1, 1.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(0, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(0, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(2, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(2, 1, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);

    update(2, 3, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), ++version);
    update(2, 3, 2.5f);
    EXPECT_EQ(drawable.getBufferVersion(), version);
}

} // namespace
} // namespace mln::command_export
