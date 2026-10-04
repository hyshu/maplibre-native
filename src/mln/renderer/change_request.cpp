#include <mln/renderer/change_request.hpp>
#include <mln/renderer/render_orchestrator.hpp>

namespace mln {

AddLayerGroupRequest::AddLayerGroupRequest(LayerGroupBasePtr layerGroup_)
    : layerGroup(std::move(layerGroup_)) {}

AddLayerGroupRequest::AddLayerGroupRequest(AddLayerGroupRequest &&other)
    : layerGroup(std::move(other.layerGroup)) {}

void AddLayerGroupRequest::execute(RenderOrchestrator &orchestrator) {
    orchestrator.addLayerGroup(std::move(layerGroup));
}

void RemoveLayerGroupRequest::execute(RenderOrchestrator &orchestrator) {
    orchestrator.removeLayerGroup(layerGroup);
}

UpdateLayerGroupIndexRequest::UpdateLayerGroupIndexRequest(LayerGroupBasePtr tileLayerGroup_, int32_t newLayerIndex_)
    : layerGroup(std::move(tileLayerGroup_)),
      newLayerIndex(newLayerIndex_) {}

void UpdateLayerGroupIndexRequest::execute(RenderOrchestrator &orchestrator) {
    orchestrator.updateLayerIndex(layerGroup, newLayerIndex);
}

AddRenderTargetRequest::AddRenderTargetRequest(RenderTargetPtr renderTarget_, util::SimpleIdentity owner_)
    : renderTarget(std::move(renderTarget_)), owner(owner_) {}

void AddRenderTargetRequest::execute(RenderOrchestrator &orchestrator) {
    orchestrator.addRenderTarget(std::move(renderTarget), owner);
}

RemoveRenderTargetRequest::RemoveRenderTargetRequest(RenderTargetPtr renderTarget_, util::SimpleIdentity owner_)
    : renderTarget(std::move(renderTarget_)), owner(owner_) {}

void RemoveRenderTargetRequest::execute(RenderOrchestrator &orchestrator) {
    orchestrator.removeRenderTarget(renderTarget, owner);
}

} // namespace mln
