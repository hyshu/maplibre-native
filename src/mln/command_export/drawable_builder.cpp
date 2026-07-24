#include <mln/command_export/drawable_builder.hpp>
#include <mln/command_export/drawable.hpp>
#include <mln/gfx/drawable_impl.hpp>
#include <mln/gfx/drawable_builder_impl.hpp>

#include <cstring>

namespace mln {
namespace command_export {

DrawableBuilder::DrawableBuilder(std::string name)
    : gfx::DrawableBuilder(std::move(name)) {}

std::unique_ptr<gfx::Drawable::DrawSegment> DrawableBuilder::createSegment(gfx::DrawMode mode,
                                                                             SegmentBase&& seg) {
    return std::make_unique<gfx::Drawable::DrawSegment>(mode, std::move(seg));
}

gfx::UniqueDrawable DrawableBuilder::createDrawable() const {
    return std::make_unique<Drawable>(name);
}

void DrawableBuilder::init() {
    auto& drawable = static_cast<Drawable&>(*currentDrawable);

    // Copy raw vertex data to drawable (same as Metal init)
    if (impl->rawVerticesCount) {
        auto raw = impl->rawVertices;
        drawable.setVertices(std::move(raw), impl->rawVerticesCount, impl->rawVerticesType);
    } else {
        const auto& verts = impl->vertices.vector();
        using VT = std::remove_reference<decltype(verts)>::type::value_type;
        constexpr auto vertSize = sizeof(VT);
        std::vector<uint8_t> raw(verts.size() * vertSize);
        std::memcpy(raw.data(), verts.data(), raw.size());
        drawable.setVertices(std::move(raw), verts.size(), gfx::AttributeDataType::Short2);
    }

    // Copy index data to drawable
    if (!impl->sharedIndexes && !impl->buildIndexes.empty()) {
        impl->sharedIndexes = std::make_shared<gfx::IndexVectorBase>(std::move(impl->buildIndexes));
    }
    if (impl->sharedIndexes && impl->sharedIndexes->elements()) {
        drawable.setIndexData(std::move(impl->sharedIndexes), std::move(impl->segments));
    }

    impl->clear();
    textures.fill(nullptr);
}

} // namespace command_export
} // namespace mln
