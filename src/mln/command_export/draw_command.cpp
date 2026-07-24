#include <mln/command_export/draw_command.hpp>

namespace mln {
namespace command_export {

static FrameData g_frameData;
static uint32_t g_currentLayerIndex = 0;

FrameData& getFrameData() {
    return g_frameData;
}

void setCurrentLayerIndex(uint32_t idx) {
    g_currentLayerIndex = idx;
}

uint32_t getCurrentLayerIndex() {
    return g_currentLayerIndex;
}

} // namespace command_export
} // namespace mln
