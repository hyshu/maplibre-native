#include <mln/command_export/drawable.hpp>
#include <mln/command_export/draw_command.hpp>
#include <mln/gfx/color_mode.hpp>
#include <mln/gfx/drawable_impl.hpp>
#include <mln/gfx/index_vector.hpp>
#include <mln/gfx/vertex_attribute.hpp>
#include <mln/gfx/vertex_vector.hpp>
#include <mln/util/logging.hpp>

#include <atomic>
#include <cstring>

namespace mln {
namespace command_export {

Drawable::Drawable(std::string name)
    : gfx::Drawable(std::move(name)) {
    static std::atomic<uint32_t> nextBufferId{1};
    bufferId = nextBufferId.fetch_add(1, std::memory_order_relaxed);
}

Drawable::~Drawable() = default;

/// Determine shader type from drawable name (same heuristic as POC, but
/// the backend will eventually set this explicitly via setType())
static ShaderType shaderTypeFromName(const std::string& name) {
    if (name.find("extrusion") != std::string::npos)
        return ShaderType::FillExtrusion;
    if (name.find("-3d/") != std::string::npos)
        return ShaderType::Unknown; // 3D buildings need depth buffer (not yet working)
    if (name.find("/fill") != std::string::npos) {
        if (name.find("outline") != std::string::npos)
            return ShaderType::FillOutline;
        if (name.find("pattern") != std::string::npos)
            return ShaderType::Unknown;
        return ShaderType::Fill;
    }
    if (name.find("line") != std::string::npos) {
        if (name.find("SDF") != std::string::npos || name.find("sdf") != std::string::npos)
            return ShaderType::Unknown; // LineSDF has different UBO layout, not supported yet
        return ShaderType::Line;
    }
    if (name.find("background") != std::string::npos)
        return ShaderType::Background;
    return ShaderType::Unknown;
}

/// Determine vertex stride from shader type
static uint32_t vertexStrideForShader(ShaderType shader) {
    switch (shader) {
        case ShaderType::Fill:
        case ShaderType::FillOutline:
            return 4; // short2 = 4 bytes
        case ShaderType::Line:
            return 8; // short2 + uchar4 = 8 bytes
        case ShaderType::Background:
            return 4; // short2 = 4 bytes
        case ShaderType::FillExtrusion:
            return 12; // short2 + short4 = 12 bytes (pos + normal_ed)
        default:
            return 0;
    }
}

/// Map gfx::DrawModeType to our DrawModeType
static DrawModeType convertDrawMode(const gfx::DrawMode& mode) {
    switch (mode.type) {
        case gfx::DrawModeType::Triangles:
        case gfx::DrawModeType::TriangleStrip:
            return DrawModeType::Triangles;
        case gfx::DrawModeType::Lines:
            return DrawModeType::Lines;
        case gfx::DrawModeType::LineStrip:
            return DrawModeType::LineStrip;
        case gfx::DrawModeType::Points:
            return DrawModeType::Points;
        default:
            break;
    }
    return DrawModeType::Triangles;
}

void Drawable::draw(PaintParameters&) const {
    if (!enabled) return;

    const ShaderType shader = shaderTypeFromName(name);

    // Debug: log drawable names to help identify rendering issues
    static std::set<std::string> loggedNames;
    if (loggedNames.find(name) == loggedNames.end()) {
        loggedNames.insert(name);
        printf("[FGPU] drawable: %s shader=%d vd=%zu va=%d idx=%d\n",
               name.c_str(),
               static_cast<int>(shader),
               vertexData.size(),
               vertexAttributes ? 1 : 0,
               indexVector ? static_cast<int>(indexVector->elements()) : -1);
        fflush(stdout);
    }

    if (shader == ShaderType::Unknown) return;

    // Get index data
    if (!indexVector || indexVector->elements() == 0) return;

    // Get vertex data — try direct storage first, then shared raw data from attributes
    const void* rawVertexPtr = nullptr;
    uint32_t rawVertexBytes = 0;
    uint32_t rawVertexStride = 0;
    uint32_t rawVertexCount = 0;

    if (!vertexData.empty()) {
        // Direct vertex data (from setVertices)
        rawVertexStride = vertexStrideForShader(shader);
        if (rawVertexStride == 0) return;
        rawVertexPtr = vertexData.data();
        rawVertexBytes = static_cast<uint32_t>(vertexData.size());
        rawVertexCount = rawVertexBytes / rawVertexStride;
    } else if (vertexAttributes) {
        // Try to get raw data from vertex attributes (from updateVertexAttributes)
        vertexAttributes->visitAttributes([&](const gfx::VertexAttribute& attr) {
            if (rawVertexPtr) return; // already found
            const auto& shared = attr.getSharedRawData();
            if (shared && shared->getRawCount() > 0) {
                rawVertexPtr = shared->getRawData();
                rawVertexStride = static_cast<uint32_t>(shared->getRawSize());
                rawVertexCount = static_cast<uint32_t>(shared->getRawCount());
                rawVertexBytes = rawVertexCount * rawVertexStride;
            }
        });
    }

    if (!rawVertexPtr || rawVertexBytes == 0) return;

    // Override stride with shader-known stride if available
    const uint32_t expectedStride = vertexStrideForShader(shader);
    if (expectedStride > 0) {
        rawVertexStride = expectedStride;
        rawVertexCount = rawVertexBytes / rawVertexStride;
    }

    // Helper: get UBO data from either direct buffer or cpuCopy
    auto getUboData = [&](size_t idx) -> std::pair<const void*, size_t> {
        // Try direct UBO first
        const auto& ub = uniformBuffers.get(idx);
        if (ub) {
            const auto& buf = static_cast<const UniformBuffer&>(*ub);
            if (!buf.getData().empty()) {
                return {buf.getData().data(), buf.getData().size()};
            }
        }
        // Try cpuCopy (from layer group)
        return uniformBuffers.getCpuCopy(idx);
    };

    // Extract UBO data once — shared by all segments of this drawable.
    // Drawable UBO (index 2 = idDrawableReservedVertexOnlyUBO)
    const auto [drawableUboData, drawableUboSize] = getUboData(2);
    // Props UBO — try indices 4 (line) and 5 (fill)
    const void* propsUboData = nullptr;
    size_t propsUboSize = 0;
    for (size_t idx : {4, 5}) {
        auto [d, s] = getUboData(idx);
        if (d && s > 0 && s <= sizeof(DrawCommand::propsUBO)) {
            propsUboData = d;
            propsUboSize = s;
            break;
        }
    }

    auto& frame = getFrameData();
    const auto* vertexBase = static_cast<const uint8_t*>(rawVertexPtr);
    const uint16_t* indexBase = indexVector->data();
    const auto totalIndexCount = static_cast<uint32_t>(indexVector->elements());

    auto emit = [&](DrawModeType mode, const uint8_t* vp, uint32_t vc, const uint16_t* ip, uint32_t ic) {
        auto& cmd = frame.addCommand(shader, mode, vp, rawVertexStride, vc, ip, ic);
        cmd.layerIndex = getCurrentLayerIndex();
        cmd.bufferId = bufferId;
        cmd.bufferVersion = bufferVersion;
        if (drawableUboData && drawableUboSize > 0 && drawableUboSize <= sizeof(cmd.drawableUBO)) {
            std::memcpy(cmd.drawableUBO, drawableUboData, drawableUboSize);
            cmd.drawableUBOSize = static_cast<uint32_t>(drawableUboSize);
        }
        if (propsUboData) {
            std::memcpy(cmd.propsUBO, propsUboData, propsUboSize);
            cmd.propsUBOSize = static_cast<uint32_t>(propsUboSize);
        }
    };

    if (segments.empty()) {
        emit(DrawModeType::Triangles, vertexBase, rawVertexCount, indexBase, totalIndexCount);
    } else {
        // One command per segment. Indices are relative to the segment's
        // vertexOffset (kept < 65536 that way), so offset the vertex pointer
        // instead — Command Export's draw() has no baseVertex parameter.
        for (const auto& seg : segments) {
            const auto& s = seg->getSegment();
            if (s.indexLength == 0) continue;
            if (s.indexOffset + s.indexLength > totalIndexCount) continue;
            if (s.vertexOffset >= rawVertexCount) continue;
            auto vc = static_cast<uint32_t>(s.vertexLength ? s.vertexLength
                                                           : rawVertexCount - s.vertexOffset);
            if (s.vertexOffset + vc > rawVertexCount) {
                vc = static_cast<uint32_t>(rawVertexCount - s.vertexOffset);
            }
            emit(convertDrawMode(seg->getMode()),
                 vertexBase + s.vertexOffset * rawVertexStride,
                 vc,
                 indexBase + s.indexOffset,
                 static_cast<uint32_t>(s.indexLength));
        }
    }
}

void Drawable::setIndexData(gfx::IndexVectorBasePtr indices,
                             std::vector<UniqueDrawSegment> segs) {
    indexVector = std::move(indices);
    segments = std::move(segs);
    ++bufferVersion;
}

void Drawable::setVertices(std::vector<uint8_t>&& data,
                            std::size_t count,
                            gfx::AttributeDataType type) {
    vertexData = std::move(data);
    vertexCount = count;
    vertexType = type;
    ++bufferVersion;
}

void Drawable::setColorMode(const gfx::ColorMode& mode) {
    gfx::Drawable::setColorMode(mode);
}

void Drawable::setShader(gfx::ShaderProgramBasePtr s) {
    gfx::Drawable::setShader(std::move(s));
}

void Drawable::setEnableStencil(bool value) {
    gfx::Drawable::setEnableStencil(value);
}

void Drawable::setEnableDepth(bool value) {
    gfx::Drawable::setEnableDepth(value);
}

void Drawable::setSubLayerIndex(int32_t value) {
    gfx::Drawable::setSubLayerIndex(value);
}

void Drawable::setDepthType(gfx::DepthMaskType value) {
    gfx::Drawable::setDepthType(value);
}

void Drawable::updateVertexAttributes(gfx::VertexAttributeArrayPtr attrs,
                                       std::size_t count,
                                       gfx::DrawMode mode,
                                       gfx::IndexVectorBasePtr indices,
                                       const SegmentBase* segs,
                                       std::size_t segmentCount) {
    vertexAttributes = std::move(attrs);
    vertexCount = count;
    indexVector = std::move(indices);
    ++bufferVersion;

    std::vector<UniqueDrawSegment> drawSegs;
    drawSegs.reserve(segmentCount);
    for (std::size_t i = 0; i < segmentCount; ++i) {
        const auto& s = segs[i];
        drawSegs.push_back(std::make_unique<gfx::Drawable::DrawSegment>(
            mode,
            SegmentBase{s.vertexOffset, s.indexOffset, s.vertexLength, s.indexLength, s.sortKey}));
    }
    segments = std::move(drawSegs);
    // TODO: extract raw vertex data from attribute bindings for updateVertexAttributes path
}

} // namespace command_export
} // namespace mln
