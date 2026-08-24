#include <mbgl/command_export/drawable.hpp>
#include <mbgl/command_export/circle_vertex_data.hpp>
#include <mbgl/command_export/draw_command.hpp>
#include <mbgl/command_export/fill_extrusion_vertex_data.hpp>
#include <mbgl/command_export/fill_vertex_data.hpp>
#include <mbgl/command_export/line_vertex_data.hpp>
#include <mbgl/command_export/texture2d.hpp>
#include <mbgl/gfx/color_mode.hpp>
#include <mbgl/gfx/depth_mode.hpp>
#include <mbgl/gfx/drawable_impl.hpp>
#include <mbgl/gfx/index_vector.hpp>
#include <mbgl/gfx/vertex_attribute.hpp>
#include <mbgl/gfx/vertex_vector.hpp>
#include <mbgl/renderer/bucket.hpp>
#include <mbgl/renderer/paint_parameters.hpp>
#include <mbgl/shaders/circle_layer_ubo.hpp>
#include <mbgl/shaders/fill_extrusion_layer_ubo.hpp>
#include <mbgl/shaders/fill_layer_ubo.hpp>
#include <mbgl/shaders/line_layer_ubo.hpp>
#include <mbgl/shaders/shader_defines.hpp>
#include <mbgl/shaders/shader_program_base.hpp>
#include <mbgl/util/logging.hpp>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>

namespace mbgl {
namespace command_export {

namespace {

constexpr uint32_t kStableFillExtrusionBufferIdNamespace = 0xC0000000u;
constexpr uint32_t kStableFillExtrusionBufferIdValueMask = 0x3fffffffu;

struct FillExtrusionAttributeRevision {
    bool present = false;
    const void* owner = nullptr;
    const void* data = nullptr;
    std::size_t size = 0;
    uint32_t offset = 0;
    uint32_t vertexOffset = 0;
    uint32_t stride = 0;
    gfx::AttributeDataType type = gfx::AttributeDataType::Invalid;
    double lastModified = 0;

    bool operator==(const FillExtrusionAttributeRevision&) const = default;
};

struct FillExtrusionVertexRevision {
    const void* layoutOwner = nullptr;
    const void* layoutData = nullptr;
    std::size_t layoutSize = 0;
    uint32_t layoutStride = 0;
    uint32_t vertexCount = 0;
    double layoutLastModified = 0;
    bool dataDriven = false;
    FillExtrusionAttributeRevision base;
    FillExtrusionAttributeRevision color;
    FillExtrusionAttributeRevision height;
    uint32_t constantBase = 0;
    uint32_t constantHeight = 0;

    bool operator==(const FillExtrusionVertexRevision&) const = default;
};

struct FillExtrusionIndexRevision {
    const void* owner = nullptr;
    const void* data = nullptr;
    uint32_t totalIndexCount = 0;
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexLength = 0;
    uint32_t indexLength = 0;

    bool operator==(const FillExtrusionIndexRevision&) const = default;
};

struct StableFillExtrusionKey {
    int64_t bucketId = 0;
    uint8_t tileZ = 0;
    uint32_t tileX = 0;
    uint32_t tileY = 0;
    uint32_t layerIndex = 0;
    uint32_t segmentOrdinal = 0;

    bool operator<(const StableFillExtrusionKey& other) const {
        return std::tie(bucketId, tileZ, tileX, tileY, layerIndex, segmentOrdinal) <
               std::tie(other.bucketId,
                        other.tileZ,
                        other.tileX,
                        other.tileY,
                        other.layerIndex,
                        other.segmentOrdinal);
    }
};

struct StableFillExtrusionState {
    std::weak_ptr<Bucket> bucket;
    uint32_t bufferId = 0;
    uint32_t version = 1;
    int64_t drawableId = 0;
    uint32_t localBufferVersion = 0;
    FillExtrusionVertexRevision vertexRevision;
    FillExtrusionIndexRevision indexRevision;
};

struct StableFillExtrusionIdentity {
    uint32_t bufferId = 0;
    uint32_t version = 0;
};

uint32_t nextStableFillExtrusionBufferId() {
    static std::atomic<uint32_t> nextValue{1};
    const uint32_t value = nextValue.fetch_add(1, std::memory_order_relaxed);
    if (value == 0 || value > kStableFillExtrusionBufferIdValueMask) {
        return 0;
    }
    return kStableFillExtrusionBufferIdNamespace | value;
}

StableFillExtrusionIdentity stableFillExtrusionIdentityFor(
    const std::shared_ptr<Bucket>& bucket,
    const OverscaledTileID& tileID,
    uint32_t layerIndex,
    uint32_t segmentOrdinal,
    int64_t drawableId,
    uint32_t localBufferVersion,
    const FillExtrusionVertexRevision& vertexRevision,
    const FillExtrusionIndexRevision& indexRevision) {
    static std::map<StableFillExtrusionKey, StableFillExtrusionState> states;
    static uint32_t lookupCount = 0;

    if ((++lookupCount & 0xffu) == 0) {
        for (auto it = states.begin(); it != states.end();) {
            if (it->second.bucket.expired()) {
                it = states.erase(it);
            } else {
                ++it;
            }
        }
    }

    const auto& canonical = tileID.canonical;
    const StableFillExtrusionKey key{
        bucket->getID().id(),
        canonical.z,
        canonical.x,
        canonical.y,
        layerIndex,
        segmentOrdinal,
    };

    const auto found = states.find(key);
    if (found == states.end()) {
        StableFillExtrusionState state;
        state.bucket = bucket;
        state.bufferId = nextStableFillExtrusionBufferId();
        state.drawableId = drawableId;
        state.localBufferVersion = localBufferVersion;
        state.vertexRevision = vertexRevision;
        state.indexRevision = indexRevision;
        const auto [inserted, _] = states.emplace(key, std::move(state));
        return {inserted->second.bufferId, inserted->second.version};
    }

    auto& state = found->second;
    const bool sourceChanged = state.vertexRevision != vertexRevision || state.indexRevision != indexRevision;
    const bool localGenerationChanged =
        state.drawableId == drawableId && state.localBufferVersion != localBufferVersion;
    if (sourceChanged || localGenerationChanged) {
        if (state.version == std::numeric_limits<uint32_t>::max()) {
            state.bufferId = nextStableFillExtrusionBufferId();
            state.version = 1;
        } else {
            ++state.version;
        }
    }
    state.bucket = bucket;
    state.drawableId = drawableId;
    state.localBufferVersion = localBufferVersion;
    state.vertexRevision = vertexRevision;
    state.indexRevision = indexRevision;
    return {state.bufferId, state.version};
}

} // namespace

Drawable::Drawable(std::string name)
    : gfx::Drawable(std::move(name)) {
    static std::atomic<uint32_t> nextBufferId{1};
    bufferId = nextBufferId.fetch_add(1, std::memory_order_relaxed);
}

Drawable::~Drawable() = default;

/// Map the shader program selected by MapLibre to the consumer pipeline type.
/// Drawable names contain arbitrary style layer IDs and are not type-safe.
static constexpr ShaderType shaderTypeFromProgramName(const std::string_view name) {
    if (name == "FillShader") return ShaderType::Fill;
    if (name == "FillOutlineTriangulatedShader") return ShaderType::FillOutlineTriangulated;
    if (name == "FillOutlineShader") return ShaderType::FillOutline;
    if (name == "LineShader") return ShaderType::Line;
    if (name == "LineSDFShader") return ShaderType::LineSDF;
    if (name == "LineGradientShader") return ShaderType::LineGradient;
    if (name == "LinePatternShader") return ShaderType::LinePattern;
    if (name == "BackgroundShader") return ShaderType::Background;
    if (name == "FillExtrusionShader") return ShaderType::FillExtrusion;
    if (name == "CircleShader") return ShaderType::Circle;
    if (name == "RasterShader") return ShaderType::Raster;
    if (name == "BackgroundPatternShader") return ShaderType::BackgroundPattern;

    // Patterned fills/extrusions and the remaining built-in shaders need
    // distinct consumer pipelines. Do not misrender them as their untextured
    // variants while support is absent.
    return ShaderType::Unknown;
}

static_assert(shaderTypeFromProgramName("FillExtrusionShader") == ShaderType::FillExtrusion);
static_assert(shaderTypeFromProgramName("FillOutlineShader") == ShaderType::FillOutline);
static_assert(shaderTypeFromProgramName("FillOutlineTriangulatedShader") == ShaderType::FillOutlineTriangulated);
static_assert(shaderTypeFromProgramName("BackgroundPatternShader") == ShaderType::BackgroundPattern);
static_assert(shaderTypeFromProgramName("FillExtrusionPatternShader") == ShaderType::Unknown);
static_assert(shaderTypeFromProgramName("SymbolIconShader") == ShaderType::Unknown);
static_assert(shaderTypeFromProgramName("SymbolSDFShader") == ShaderType::Unknown);
static_assert(shaderTypeFromProgramName("SymbolTextAndIconShader") == ShaderType::Unknown);
static_assert(offsetof(shaders::FillExtrusionPropsUBO, base) == 44);
static_assert(offsetof(shaders::FillExtrusionPropsUBO, height) == 48);
static_assert(offsetof(shaders::FillExtrusionDrawableUBO, pad1) == 108);
static_assert(offsetof(shaders::FillOutlineTriangulatedDrawableUBO, pad1) == 68);
static_assert(offsetof(shaders::FillOutlineTriangulatedDrawableUBO, pad2) == 72);
static_assert(offsetof(shaders::FillOutlineTriangulatedDrawableUBO, pad3) == 76);
static_assert(shaders::idFillOutlineColorVertexAttribute == shaders::idFillOpacityVertexAttribute + 1);
static_assert(offsetof(shaders::CircleEvaluatedPropsUBO, pad1) == 60);
static_assert(offsetof(shaders::CircleDrawableUBO, color_t) == 72);
static_assert(offsetof(shaders::CircleDrawableUBO, radius_t) == 76);
static_assert(offsetof(shaders::CircleDrawableUBO, blur_t) == 80);
static_assert(offsetof(shaders::CircleDrawableUBO, opacity_t) == 84);
static_assert(offsetof(shaders::CircleDrawableUBO, stroke_color_t) == 88);
static_assert(offsetof(shaders::CircleDrawableUBO, stroke_width_t) == 92);
static_assert(offsetof(shaders::CircleDrawableUBO, stroke_opacity_t) == 96);
static_assert(shaders::idCircleColorVertexAttribute == shaders::idCirclePosVertexAttribute + 1);
static_assert(shaders::idCircleRadiusVertexAttribute == shaders::idCircleColorVertexAttribute + 1);
static_assert(shaders::idCircleBlurVertexAttribute == shaders::idCircleRadiusVertexAttribute + 1);
static_assert(shaders::idCircleOpacityVertexAttribute == shaders::idCircleBlurVertexAttribute + 1);
static_assert(shaders::idCircleStrokeColorVertexAttribute == shaders::idCircleOpacityVertexAttribute + 1);
static_assert(shaders::idCircleStrokeWidthVertexAttribute == shaders::idCircleStrokeColorVertexAttribute + 1);
static_assert(shaders::idCircleStrokeOpacityVertexAttribute == shaders::idCircleStrokeWidthVertexAttribute + 1);
static_assert(offsetof(shaders::LineEvaluatedPropsUBO, expressionMask) == 40);
static_assert(offsetof(shaders::LineDrawableUBO, color_t) == 68);
static_assert(offsetof(shaders::LineDrawableUBO, blur_t) == 72);
static_assert(offsetof(shaders::LineDrawableUBO, opacity_t) == 76);
static_assert(offsetof(shaders::LineDrawableUBO, gapwidth_t) == 80);
static_assert(offsetof(shaders::LineDrawableUBO, offset_t) == 84);
static_assert(offsetof(shaders::LineDrawableUBO, width_t) == 88);
static_assert(offsetof(shaders::LineGradientDrawableUBO, blur_t) == 68);
static_assert(offsetof(shaders::LineGradientDrawableUBO, opacity_t) == 72);
static_assert(offsetof(shaders::LineGradientDrawableUBO, gapwidth_t) == 76);
static_assert(offsetof(shaders::LineGradientDrawableUBO, offset_t) == 80);
static_assert(offsetof(shaders::LineGradientDrawableUBO, width_t) == 84);
static_assert(offsetof(shaders::LinePatternDrawableUBO, blur_t) == 68);
static_assert(offsetof(shaders::LinePatternDrawableUBO, opacity_t) == 72);
static_assert(offsetof(shaders::LinePatternDrawableUBO, gapwidth_t) == 76);
static_assert(offsetof(shaders::LinePatternDrawableUBO, offset_t) == 80);
static_assert(offsetof(shaders::LinePatternDrawableUBO, width_t) == 84);
static_assert(offsetof(shaders::LinePatternDrawableUBO, pattern_from_t) == 88);
static_assert(offsetof(shaders::LinePatternDrawableUBO, pattern_to_t) == 92);
static_assert(offsetof(shaders::LineSDFDrawableUBO, color_t) == 92);
static_assert(offsetof(shaders::LineSDFDrawableUBO, blur_t) == 96);
static_assert(offsetof(shaders::LineSDFDrawableUBO, opacity_t) == 100);
static_assert(offsetof(shaders::LineSDFDrawableUBO, gapwidth_t) == 104);
static_assert(offsetof(shaders::LineSDFDrawableUBO, offset_t) == 108);
static_assert(offsetof(shaders::LineSDFDrawableUBO, width_t) == 112);
static_assert(offsetof(shaders::LineSDFDrawableUBO, floorwidth_t) == 116);
static_assert(shaders::idLineColorVertexAttribute == shaders::idLineDataVertexAttribute + 1);
static_assert(shaders::idLineBlurVertexAttribute == shaders::idLineColorVertexAttribute + 1);
static_assert(shaders::idLineOpacityVertexAttribute == shaders::idLineBlurVertexAttribute + 1);
static_assert(shaders::idLineGapWidthVertexAttribute == shaders::idLineOpacityVertexAttribute + 1);
static_assert(shaders::idLineOffsetVertexAttribute == shaders::idLineGapWidthVertexAttribute + 1);
static_assert(shaders::idLineWidthVertexAttribute == shaders::idLineOffsetVertexAttribute + 1);
static_assert(shaders::idLineFloorWidthVertexAttribute == shaders::idLineWidthVertexAttribute + 1);
static_assert(shaders::idLinePatternFromVertexAttribute == shaders::idLineFloorWidthVertexAttribute + 1);
static_assert(shaders::idLinePatternToVertexAttribute == shaders::idLinePatternFromVertexAttribute + 1);

/// Determine vertex stride from shader type
static constexpr uint32_t vertexStrideForShader(ShaderType shader) {
    switch (shader) {
        case ShaderType::Fill:
        case ShaderType::FillOutline:
            return 4; // short2 = 4 bytes
        case ShaderType::Line:
        case ShaderType::LineSDF:
        case ShaderType::LineGradient:
        case ShaderType::LinePattern:
        case ShaderType::FillOutlineTriangulated:
            return 8; // short2 + uchar4 = 8 bytes (all line variants share LineLayoutVertex)
        case ShaderType::Background:
        case ShaderType::BackgroundPattern:
            return 4; // short2 = 4 bytes
        case ShaderType::Circle:
            return 4; // short2 = 4 bytes (pos*2 + extrude bit encoding)
        case ShaderType::Raster:
            return 8; // short2 pos + short2 texture_pos (RasterLayoutVertex)
        case ShaderType::FillExtrusion:
            return 12; // short2 + short4 = 12 bytes (pos + normal_ed)
        default:
            return 0;
    }
}

static_assert(vertexStrideForShader(ShaderType::FillOutline) == 4);
static_assert(vertexStrideForShader(ShaderType::FillOutlineTriangulated) == 8);

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

void Drawable::draw(PaintParameters& parameters) const {
    if (!enabled) return;

    // MapLibre's fill-extrusion depth prepass has color writes disabled.
    // Consumers perform their own depth-tested color pass, so exporting it
    // would render the geometry twice.
    if (!getEnableColor()) return;

    const auto& shaderProgram = getShader();
    if (!shaderProgram) return;
    const ShaderType shader = shaderTypeFromProgramName(shaderProgram->typeName());

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

    std::optional<FillExtrusionVertexRevision> fillExtrusionRevision;
    if (shader == ShaderType::FillExtrusion) {
        FillExtrusionVertexRevision revision;
        revision.layoutSize = rawVertexBytes;
        revision.layoutStride = rawVertexStride;
        revision.vertexCount = rawVertexCount;
        if (vertexAttributes) {
            const auto& position = vertexAttributes->get(shaders::idFillExtrusionPosVertexAttribute);
            if (position) {
                const auto& shared = position->getSharedRawData();
                if (shared) {
                    revision.layoutOwner = shared.get();
                    revision.layoutData = shared->getRawData();
                    revision.layoutLastModified = shared->getLastModified().count();
                }
            }
        }
        fillExtrusionRevision = revision;
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
    // Tile-props UBO (index 3 = idDrawableReservedFragmentOnlyUBO):
    // LineSDFTilePropsUBO / LinePatternTilePropsUBO
    const auto [tilePropsUboData, tilePropsUboSize] = getUboData(3);
    // Props UBO — index depends on the layer type (see shader_defines.hpp).
    // A fixed index (not a search) matters: FillExtrusion also has a 48-byte
    // TilePropsUBO at index 4 that a {4,5} scan would pick up by mistake.
    const size_t propsIdx = [&] {
        switch (shader) {
            case ShaderType::Background:
            case ShaderType::BackgroundPattern:
                return static_cast<size_t>(shaders::idBackgroundPropsUBO); // 4
            case ShaderType::Line:
            case ShaderType::LineSDF:
            case ShaderType::LineGradient:
            case ShaderType::LinePattern:
                return static_cast<size_t>(shaders::idLineEvaluatedPropsUBO); // 4
            case ShaderType::Circle:
                return static_cast<size_t>(shaders::idCircleEvaluatedPropsUBO); // 4
            case ShaderType::Raster:
                return static_cast<size_t>(shaders::idRasterEvaluatedPropsUBO); // 4
            case ShaderType::FillExtrusion:
                return static_cast<size_t>(shaders::idFillExtrusionPropsUBO); // 5
            default:
                return static_cast<size_t>(shaders::idFillEvaluatedPropsUBO); // 5
        }
    }();
    const void* propsUboData = nullptr;
    size_t propsUboSize = 0;
    {
        auto [d, s] = getUboData(propsIdx);
        if (d && s > 0 && s <= sizeof(DrawCommand::propsUBO)) {
            propsUboData = d;
            propsUboSize = s;
        }
    }

    // Data-driven fill-extrusion base, height, and color are independent.
    // Normalize source/composite encodings to the fixed 44-byte Command Export ABI
    // format (layout + base range + height range + packed color range).
    // Missing base/height ranges are populated from the evaluated props UBO;
    // a missing color range remains zero and is selected from the props UBO
    // by the shader's data-driven mask.
    uint32_t extraFlags = 0;
    if (getEnableDepth()) {
        const auto depthMode = getIs3D() ? parameters.depthModeFor3D()
                                         : parameters.depthModeForSublayer(getSubLayerIndex(), getDepthType());
        if (depthMode.func != gfx::DepthFunctionType::Always) {
            extraFlags |= DrawCommandFlags::DepthTest;
            if (depthMode.mask == gfx::DepthMaskType::ReadWrite) {
                extraFlags |= DrawCommandFlags::DepthWrite;
            }
        }
    }

    uint32_t stencilReference = 0;
    StencilModeType stencilMode = StencilModeType::Disabled;
    if (getEnableStencil()) {
        if (getIs3D()) {
            if (stencilReferenceFor3D) {
                stencilReference = static_cast<uint32_t>(*stencilReferenceFor3D);
                stencilMode = StencilModeType::FillExtrusion;
            }
        } else if (const auto& tileID = getTileID()) {
            const auto resolvedStencil = parameters.stencilModeForClipping(tileID->toUnwrapped());
            stencilReference = static_cast<uint32_t>(resolvedStencil.ref);
            stencilMode = StencilModeType::ClippingTest;
        }
    }
    if (shader == ShaderType::FillExtrusion && vertexAttributes) {
        std::optional<detail::FillExtrusionAttributeData> baseData;
        std::optional<detail::FillExtrusionAttributeData> colorData;
        std::optional<detail::FillExtrusionAttributeData> heightData;
        DDVertexCacheState cacheState;
        cacheState.vertexCount = rawVertexCount;

        const auto readAttribute =
            [&](size_t id, std::optional<detail::FillExtrusionAttributeData>& result, DDAttributeCacheState& state) {
                const auto& attr = vertexAttributes->get(id);
                if (!attr) {
                    return true;
                }

                state.present = true;
                const auto& shared = attr->getSharedRawData();
                if (!shared) {
                    return false;
                }

                const auto rawSize = shared->getRawSize();
                const auto rawCount = shared->getRawCount();
                if (rawSize != 0 && rawCount > std::numeric_limits<std::size_t>::max() / rawSize) {
                    return false;
                }
                const auto dataSize = rawSize * rawCount;
                const auto* data = static_cast<const uint8_t*>(shared->getRawData());
                if (!data || dataSize == 0) {
                    return false;
                }

                state.owner = shared.get();
                state.data = data;
                state.size = dataSize;
                state.offset = attr->getSharedOffset();
                state.vertexOffset = attr->getSharedVertexOffset();
                state.stride = attr->getSharedStride();
                state.type = attr->getSharedType();
                state.lastModified = shared->getLastModified().count();
                result = detail::FillExtrusionAttributeData{
                    .data = data,
                    .size = dataSize,
                    .offset = state.offset,
                    .vertexOffset = state.vertexOffset,
                    .stride = state.stride,
                    .type = state.type,
                };
                return true;
            };

        if (!readAttribute(shaders::idFillExtrusionBaseVertexAttribute, baseData, cacheState.base) ||
            !readAttribute(shaders::idFillExtrusionColorVertexAttribute, colorData, cacheState.color) ||
            !readAttribute(shaders::idFillExtrusionHeightVertexAttribute, heightData, cacheState.height)) {
            return;
        }

        if (baseData || heightData || colorData) {
            float constantBase = 0;
            float constantHeight = 0;
            const bool hasProps = propsUboData && propsUboSize >= sizeof(shaders::FillExtrusionPropsUBO);
            if ((!baseData || !heightData || !colorData) && !hasProps) {
                return;
            }
            if (hasProps) {
                const auto* props = static_cast<const uint8_t*>(propsUboData);
                std::memcpy(&constantBase, props + offsetof(shaders::FillExtrusionPropsUBO, base), sizeof(float));
                std::memcpy(&constantHeight, props + offsetof(shaders::FillExtrusionPropsUBO, height), sizeof(float));
            }
            cacheState.constantBase = std::bit_cast<uint32_t>(constantBase);
            cacheState.constantHeight = std::bit_cast<uint32_t>(constantHeight);

            const bool geometryDirty = ddVertexVersion != bufferVersion;
            if (geometryDirty || !ddVertexCacheState || *ddVertexCacheState != cacheState) {
                const auto update = detail::updateFillExtrusionVertexData(
                    {static_cast<const uint8_t*>(rawVertexPtr), rawVertexBytes},
                    rawVertexCount,
                    baseData,
                    heightData,
                    colorData,
                    constantBase,
                    constantHeight,
                    ddVertexData);
                if (update == detail::FillExtrusionVertexDataUpdate::Failed) {
                    return;
                }
                if (!geometryDirty && update == detail::FillExtrusionVertexDataUpdate::Changed) {
                    // Consumers cache GPU buffers by (bufferId, bufferVersion,
                    // pointer). Paint-only mutations do not call any of this
                    // backend's setters, so advance the exported generation.
                    ++bufferVersion;
                }
                ddVertexVersion = bufferVersion;
                ddVertexCacheState = cacheState;
            }

            auto snapshotAttributeRevision = [](const DDAttributeCacheState& state) {
                FillExtrusionAttributeRevision revision;
                revision.present = state.present;
                revision.owner = state.owner;
                revision.data = state.data;
                revision.size = state.size;
                revision.offset = state.offset;
                revision.vertexOffset = state.vertexOffset;
                revision.stride = state.stride;
                revision.type = state.type;
                revision.lastModified = state.lastModified;
                return revision;
            };
            if (fillExtrusionRevision) {
                fillExtrusionRevision->dataDriven = true;
                fillExtrusionRevision->base = snapshotAttributeRevision(cacheState.base);
                fillExtrusionRevision->color = snapshotAttributeRevision(cacheState.color);
                fillExtrusionRevision->height = snapshotAttributeRevision(cacheState.height);
                fillExtrusionRevision->constantBase = baseData ? 0 : cacheState.constantBase;
                fillExtrusionRevision->constantHeight = heightData ? 0 : cacheState.constantHeight;
            }

            constexpr uint32_t ddStride = 12 + 8 + 8 + 16;
            rawVertexPtr = ddVertexData.data();
            rawVertexStride = ddStride;
            extraFlags |= DrawCommandFlags::FillExtrusionDataDriven;
            if (colorData) {
                extraFlags |= DrawCommandFlags::FillExtrusionColorDataDriven;
            }
        }
    }

    // Fill and Command Export triangulated fill-outline paint properties are
    // independently data-driven. Normalize their source/composite encodings
    // while keeping interpolation factors in the drawable UBO. A constant
    // triangulated outline has no paint attributes and remains 8 bytes.
    if ((shader == ShaderType::Fill || shader == ShaderType::FillOutlineTriangulated) && vertexAttributes) {
        std::optional<detail::FillAttributeData> colorData;
        std::optional<detail::FillAttributeData> opacityData;
        FillDDVertexCacheState cacheState;
        cacheState.vertexCount = rawVertexCount;

        const auto readAttribute =
            [&](size_t id, std::optional<detail::FillAttributeData>& result, DDAttributeCacheState& state) {
                const auto& attr = vertexAttributes->get(id);
                if (!attr) {
                    return true;
                }

                state.present = true;
                const auto& shared = attr->getSharedRawData();
                if (!shared) {
                    return false;
                }

                const auto rawSize = shared->getRawSize();
                const auto rawCount = shared->getRawCount();
                if (rawSize != 0 && rawCount > std::numeric_limits<std::size_t>::max() / rawSize) {
                    return false;
                }
                const auto dataSize = rawSize * rawCount;
                const auto* data = static_cast<const uint8_t*>(shared->getRawData());
                if (!data || dataSize == 0) {
                    return false;
                }

                state.owner = shared.get();
                state.data = data;
                state.size = dataSize;
                state.offset = attr->getSharedOffset();
                state.vertexOffset = attr->getSharedVertexOffset();
                state.stride = attr->getSharedStride();
                state.type = attr->getSharedType();
                state.lastModified = shared->getLastModified().count();
                result = detail::FillAttributeData{
                    .data = data,
                    .size = dataSize,
                    .offset = state.offset,
                    .vertexOffset = state.vertexOffset,
                    .stride = state.stride,
                    .type = state.type,
                };
                return true;
            };

        const auto colorAttributeID = shader == ShaderType::Fill ? shaders::idFillColorVertexAttribute
                                                                 : shaders::idFillOutlineColorVertexAttribute;
        if (!readAttribute(colorAttributeID, colorData, cacheState.color) ||
            !readAttribute(shaders::idFillOpacityVertexAttribute, opacityData, cacheState.opacity)) {
            return;
        }

        if (colorData || opacityData) {
            const bool geometryDirty = fillDDVertexVersion != bufferVersion;
            if (geometryDirty || !fillDDVertexCacheState || *fillDDVertexCacheState != cacheState) {
                const auto layoutData = std::span<const uint8_t>{static_cast<const uint8_t*>(rawVertexPtr),
                                                                 rawVertexBytes};
                const auto update = shader == ShaderType::Fill
                                        ? detail::updateFillVertexData(
                                              layoutData, rawVertexCount, colorData, opacityData, fillDDVertexData)
                                        : detail::updateFillOutlineTriangulatedVertexData(
                                              layoutData, rawVertexCount, colorData, opacityData, fillDDVertexData);
                if (update == detail::FillVertexDataUpdate::Failed) {
                    return;
                }
                if (!geometryDirty && update == detail::FillVertexDataUpdate::Changed) {
                    // Paint attribute storage can mutate without invoking a
                    // backend setter. Advance the consumer GPU cache generation
                    // only when the normalized bytes actually changed.
                    ++bufferVersion;
                }
                fillDDVertexVersion = bufferVersion;
                fillDDVertexCacheState = cacheState;
            }

            const uint32_t ddStride = shader == ShaderType::Fill ? 4 + 16 + 8 : 8 + 16 + 8;
            rawVertexPtr = fillDDVertexData.data();
            rawVertexStride = ddStride;
            if (shader == ShaderType::Fill) {
                if (colorData) {
                    extraFlags |= DrawCommandFlags::FillColorDataDriven;
                }
                if (opacityData) {
                    extraFlags |= DrawCommandFlags::FillOpacityDataDriven;
                }
            } else {
                if (colorData) {
                    extraFlags |= DrawCommandFlags::FillOutlineColorDataDriven;
                }
                if (opacityData) {
                    extraFlags |= DrawCommandFlags::FillOutlineOpacityDataDriven;
                }
            }
        }
    }

    // Circle color, radius, blur, opacity, stroke color, stroke width, and
    // stroke opacity are independently data-driven. Normalize source and
    // composite encodings into one fixed layout; absent properties remain
    // constants in CircleEvaluatedPropsUBO and are selected by command flags.
    if (shader == ShaderType::Circle && vertexAttributes) {
        detail::CircleVertexAttributes attributes;
        CircleDDVertexCacheState cacheState;
        cacheState.vertexCount = rawVertexCount;

        const auto readAttribute =
            [&](size_t id, std::optional<detail::CircleAttributeData>& result, DDAttributeCacheState& state) {
                const auto& attr = vertexAttributes->get(id);
                if (!attr) {
                    return true;
                }

                state.present = true;
                const auto& shared = attr->getSharedRawData();
                if (!shared) {
                    return false;
                }

                const auto rawSize = shared->getRawSize();
                const auto rawCount = shared->getRawCount();
                if (rawSize != 0 && rawCount > std::numeric_limits<std::size_t>::max() / rawSize) {
                    return false;
                }
                const auto dataSize = rawSize * rawCount;
                const auto* data = static_cast<const uint8_t*>(shared->getRawData());
                if (!data || dataSize == 0) {
                    return false;
                }

                state.owner = shared.get();
                state.data = data;
                state.size = dataSize;
                state.offset = attr->getSharedOffset();
                state.vertexOffset = attr->getSharedVertexOffset();
                state.stride = attr->getSharedStride();
                state.type = attr->getSharedType();
                state.lastModified = shared->getLastModified().count();
                result = detail::CircleAttributeData{
                    .data = data,
                    .size = dataSize,
                    .offset = state.offset,
                    .vertexOffset = state.vertexOffset,
                    .stride = state.stride,
                    .type = state.type,
                };
                return true;
            };

        if (!readAttribute(shaders::idCircleColorVertexAttribute, attributes.color, cacheState.color) ||
            !readAttribute(shaders::idCircleRadiusVertexAttribute, attributes.radius, cacheState.radius) ||
            !readAttribute(shaders::idCircleBlurVertexAttribute, attributes.blur, cacheState.blur) ||
            !readAttribute(shaders::idCircleOpacityVertexAttribute, attributes.opacity, cacheState.opacity) ||
            !readAttribute(
                shaders::idCircleStrokeColorVertexAttribute, attributes.strokeColor, cacheState.strokeColor) ||
            !readAttribute(
                shaders::idCircleStrokeWidthVertexAttribute, attributes.strokeWidth, cacheState.strokeWidth) ||
            !readAttribute(
                shaders::idCircleStrokeOpacityVertexAttribute, attributes.strokeOpacity, cacheState.strokeOpacity)) {
            return;
        }

        if (!attributes.empty()) {
            const bool geometryDirty = circleDDVertexVersion != bufferVersion;
            if (geometryDirty || !circleDDVertexCacheState || *circleDDVertexCacheState != cacheState) {
                const auto update = detail::updateCircleVertexData(
                    {static_cast<const uint8_t*>(rawVertexPtr), rawVertexBytes},
                    rawVertexCount,
                    attributes,
                    circleDDVertexData);
                if (update == detail::CircleVertexDataUpdate::Failed) {
                    return;
                }
                if (!geometryDirty && update == detail::CircleVertexDataUpdate::Changed) {
                    // Feature-state paint mutations can bypass backend
                    // setters, so advance the consumer GPU buffer generation when
                    // normalized bytes change independently of geometry.
                    ++bufferVersion;
                }
                circleDDVertexVersion = bufferVersion;
                circleDDVertexCacheState = cacheState;
            }

            constexpr uint32_t ddStride = 76;
            rawVertexPtr = circleDDVertexData.data();
            rawVertexStride = ddStride;
            if (attributes.color) extraFlags |= DrawCommandFlags::CircleColorDataDriven;
            if (attributes.radius) extraFlags |= DrawCommandFlags::CircleRadiusDataDriven;
            if (attributes.blur) extraFlags |= DrawCommandFlags::CircleBlurDataDriven;
            if (attributes.opacity) extraFlags |= DrawCommandFlags::CircleOpacityDataDriven;
            if (attributes.strokeColor) extraFlags |= DrawCommandFlags::CircleStrokeColorDataDriven;
            if (attributes.strokeWidth) extraFlags |= DrawCommandFlags::CircleStrokeWidthDataDriven;
            if (attributes.strokeOpacity) extraFlags |= DrawCommandFlags::CircleStrokeOpacityDataDriven;
        }
    }

    // Line color, blur, opacity, gap width, offset, width, floor width, and
    // pattern are independently data-driven across the simple, gradient,
    // pattern, and SDF variants. Normalize every present attribute into one
    // layout; each shader consumes only the properties relevant to its path.
    if ((shader == ShaderType::Line || shader == ShaderType::LineGradient || shader == ShaderType::LinePattern ||
         shader == ShaderType::LineSDF) &&
        vertexAttributes) {
        detail::LineVertexAttributes attributes;
        LineDDVertexCacheState cacheState;
        cacheState.vertexCount = rawVertexCount;

        const auto readAttribute =
            [&](size_t id, std::optional<detail::LineAttributeData>& result, DDAttributeCacheState& state) {
                const auto& attr = vertexAttributes->get(id);
                if (!attr) {
                    return true;
                }

                state.present = true;
                const auto& shared = attr->getSharedRawData();
                if (!shared) {
                    return false;
                }

                const auto rawSize = shared->getRawSize();
                const auto rawCount = shared->getRawCount();
                if (rawSize != 0 && rawCount > std::numeric_limits<std::size_t>::max() / rawSize) {
                    return false;
                }
                const auto dataSize = rawSize * rawCount;
                const auto* data = static_cast<const uint8_t*>(shared->getRawData());
                if (!data || dataSize == 0) {
                    return false;
                }

                state.owner = shared.get();
                state.data = data;
                state.size = dataSize;
                state.offset = attr->getSharedOffset();
                state.vertexOffset = attr->getSharedVertexOffset();
                state.stride = attr->getSharedStride();
                state.type = attr->getSharedType();
                state.lastModified = shared->getLastModified().count();
                result = detail::LineAttributeData{
                    .data = data,
                    .size = dataSize,
                    .offset = state.offset,
                    .vertexOffset = state.vertexOffset,
                    .stride = state.stride,
                    .type = state.type,
                };
                return true;
            };

        // Bind pattern rectangles by the shader-facing fixed IDs. LinePattern
        // declares its property template parameters in the opposite textual
        // order, so inferring these slots from property names would be wrong.
        if (!readAttribute(shaders::idLineColorVertexAttribute, attributes.color, cacheState.color) ||
            !readAttribute(shaders::idLineBlurVertexAttribute, attributes.blur, cacheState.blur) ||
            !readAttribute(shaders::idLineOpacityVertexAttribute, attributes.opacity, cacheState.opacity) ||
            !readAttribute(shaders::idLineGapWidthVertexAttribute, attributes.gapWidth, cacheState.gapWidth) ||
            !readAttribute(shaders::idLineOffsetVertexAttribute, attributes.offset, cacheState.offset) ||
            !readAttribute(shaders::idLineWidthVertexAttribute, attributes.width, cacheState.width) ||
            !readAttribute(shaders::idLineFloorWidthVertexAttribute, attributes.floorWidth, cacheState.floorWidth) ||
            !readAttribute(shaders::idLinePatternFromVertexAttribute, attributes.patternFrom, cacheState.patternFrom) ||
            !readAttribute(shaders::idLinePatternToVertexAttribute, attributes.patternTo, cacheState.patternTo)) {
            return;
        }

        if (!attributes.empty()) {
            const bool geometryDirty = lineDDVertexVersion != bufferVersion;
            if (geometryDirty || !lineDDVertexCacheState || *lineDDVertexCacheState != cacheState) {
                const auto update = detail::updateLineVertexData(
                    {static_cast<const uint8_t*>(rawVertexPtr), rawVertexBytes},
                    rawVertexCount,
                    attributes,
                    lineDDVertexData);
                if (update == detail::LineVertexDataUpdate::Failed) {
                    return;
                }
                if (!geometryDirty && update == detail::LineVertexDataUpdate::Changed) {
                    // Feature-state paint mutations can bypass backend
                    // setters. Advance the consumer cache generation only when the
                    // normalized vertex bytes actually changed.
                    ++bufferVersion;
                }
                lineDDVertexVersion = bufferVersion;
                lineDDVertexCacheState = cacheState;
            }

            constexpr uint32_t ddStride = 88;
            rawVertexPtr = lineDDVertexData.data();
            rawVertexStride = ddStride;
            if (attributes.color) extraFlags |= DrawCommandFlags::LineColorDataDriven;
            if (attributes.blur) extraFlags |= DrawCommandFlags::LineBlurDataDriven;
            if (attributes.opacity) extraFlags |= DrawCommandFlags::LineOpacityDataDriven;
            if (attributes.gapWidth) extraFlags |= DrawCommandFlags::LineGapWidthDataDriven;
            if (attributes.offset) extraFlags |= DrawCommandFlags::LineOffsetDataDriven;
            if (attributes.width) extraFlags |= DrawCommandFlags::LineWidthDataDriven;
            if (attributes.floorWidth) extraFlags |= DrawCommandFlags::LineFloorWidthDataDriven;
            if (attributes.patternFrom && attributes.patternTo) {
                extraFlags |= DrawCommandFlags::LinePatternDataDriven;
            }
        }
    }

    // Texture export (dash atlas / gradient ramp / pattern atlas / raster
    // tile). The Texture2D keeps its pixels CPU-side; materialize a pending
    // image first.
    const Texture2D* exportTex = nullptr;
    int32_t texSlot = -1;
    if (shader == ShaderType::LineSDF || shader == ShaderType::LineGradient || shader == ShaderType::LinePattern) {
        texSlot = shaders::idLineImageTexture;
    } else if (shader == ShaderType::BackgroundPattern) {
        texSlot = shaders::idBackgroundImageTexture;
    } else if (shader == ShaderType::Raster) {
        // Image0 and image1 are the same bucket texture on this path
        texSlot = shaders::idRasterImage0Texture;
    }
    if (texSlot >= 0) {
        if (const auto& tex = getTexture(static_cast<size_t>(texSlot))) {
            auto* commandExportTexture = static_cast<Texture2D*>(tex.get());
            if (commandExportTexture->needsUpload()) {
                commandExportTexture->upload();
            }
            if (!commandExportTexture->getPixelData().empty()) {
                exportTex = commandExportTexture;
            }
        }
        // These shaders can't render without their texture
        if (!exportTex) return;
    }

    auto& frame = getFrameData();
    const auto* vertexBase = static_cast<const uint8_t*>(rawVertexPtr);
    const uint16_t* indexBase = indexVector->data();
    const auto totalIndexCount = static_cast<uint32_t>(indexVector->elements());
    const float cameraDistance = static_cast<float>(parameters.state.getCameraToCenterDistance());
    const uint32_t layerIndex = getCurrentLayerIndex();

    auto emit = [&](DrawModeType mode,
                    const uint8_t* vp,
                    uint32_t vc,
                    const uint16_t* ip,
                    uint32_t ic,
                    uint32_t segmentOrdinal,
                    const SegmentBase* segment) {
        uint32_t exportedBufferId = bufferId;
        uint32_t exportedBufferVersion = bufferVersion;
        if (shader == ShaderType::FillExtrusion && fillExtrusionRevision) {
            const auto& bucket = getBucket();
            const auto& tileID = getTileID();
            if (bucket && tileID) {
                FillExtrusionIndexRevision indexRevision;
                indexRevision.owner = indexVector.get();
                indexRevision.data = indexBase;
                indexRevision.totalIndexCount = totalIndexCount;
                if (segment) {
                    indexRevision.vertexOffset = static_cast<uint32_t>(segment->vertexOffset);
                    indexRevision.indexOffset = static_cast<uint32_t>(segment->indexOffset);
                    indexRevision.vertexLength = static_cast<uint32_t>(segment->vertexLength);
                    indexRevision.indexLength = static_cast<uint32_t>(segment->indexLength);
                } else {
                    indexRevision.vertexLength = rawVertexCount;
                    indexRevision.indexLength = totalIndexCount;
                }
                const auto stable = stableFillExtrusionIdentityFor(bucket,
                                                                   *tileID,
                                                                   layerIndex,
                                                                   segmentOrdinal,
                                                                   getID().id(),
                                                                   bufferVersion,
                                                                   *fillExtrusionRevision,
                                                                   indexRevision);
                if (stable.bufferId != 0) {
                    exportedBufferId = stable.bufferId;
                    exportedBufferVersion = stable.version;
                }
            }
        }

        auto& cmd = frame.addCommand(shader, mode, vp, rawVertexStride, vc, ip, ic);
        cmd.layerIndex = layerIndex;
        cmd.subLayerIndex = getSubLayerIndex();
        cmd.bufferId = exportedBufferId;
        cmd.bufferVersion = exportedBufferVersion;
        cmd.cameraDistance = cameraDistance;
        cmd.flags = extraFlags;
        cmd.stencilReference = stencilReference;
        cmd.stencilMode = stencilMode;
        if (drawableUboData && drawableUboSize > 0 && drawableUboSize <= sizeof(cmd.drawableUBO)) {
            std::memcpy(cmd.drawableUBO, drawableUboData, drawableUboSize);
            cmd.drawableUBOSize = static_cast<uint32_t>(drawableUboSize);
        }
        if (propsUboData) {
            std::memcpy(cmd.propsUBO, propsUboData, propsUboSize);
            cmd.propsUBOSize = static_cast<uint32_t>(propsUboSize);
        }
        if (tilePropsUboData && tilePropsUboSize > 0 && tilePropsUboSize <= sizeof(cmd.tilePropsUBO)) {
            std::memcpy(cmd.tilePropsUBO, tilePropsUboData, tilePropsUboSize);
            cmd.tilePropsUBOSize = static_cast<uint32_t>(tilePropsUboSize);
        }
        if (exportTex) {
            cmd.texData = exportTex->getPixelData().data();
            cmd.texWidth = exportTex->getSize().width;
            cmd.texHeight = exportTex->getSize().height;
            cmd.texChannels = static_cast<uint32_t>(exportTex->numChannels());
            cmd.texId = exportTex->getTextureId();
            cmd.texVersion = exportTex->getVersion();
            cmd.texFilter = exportTex->getSamplerFilter() == gfx::TextureFilterType::Nearest
                                ? TextureFilterType::Nearest
                                : TextureFilterType::Linear;
        }
    };

    if (segments.empty()) {
        emit(DrawModeType::Triangles, vertexBase, rawVertexCount, indexBase, totalIndexCount, 0, nullptr);
    } else {
        // One command per segment. Indices are relative to the segment's
        // vertexOffset (kept < 65536 that way), so offset the vertex pointer
        // instead because the command ABI has no baseVertex field.
        for (std::size_t segmentOrdinal = 0; segmentOrdinal < segments.size(); ++segmentOrdinal) {
            const auto& seg = segments[segmentOrdinal];
            const auto& s = seg->getSegment();
            if (s.indexLength == 0) continue;
            if (s.indexOffset + s.indexLength > totalIndexCount) continue;
            if (s.vertexOffset >= rawVertexCount) continue;
            auto vc = static_cast<uint32_t>(s.vertexLength ? s.vertexLength : rawVertexCount - s.vertexOffset);
            if (s.vertexOffset + vc > rawVertexCount) {
                vc = static_cast<uint32_t>(rawVertexCount - s.vertexOffset);
            }
            emit(convertDrawMode(seg->getMode()),
                 vertexBase + s.vertexOffset * rawVertexStride,
                 vc,
                 indexBase + s.indexOffset,
                 static_cast<uint32_t>(s.indexLength),
                 static_cast<uint32_t>(segmentOrdinal),
                 &s);
        }
    }
}

void Drawable::setIndexData(gfx::IndexVectorBasePtr indices, std::vector<UniqueDrawSegment> segs) {
    indexVector = std::move(indices);
    segments = std::move(segs);
    ++bufferVersion;
}

void Drawable::setVertices(std::vector<uint8_t>&& data, std::size_t count, gfx::AttributeDataType type) {
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
    bool attributeBindingsChanged = static_cast<bool>(vertexAttributes) != static_cast<bool>(attrs);
    if (!attributeBindingsChanged && vertexAttributes && attrs) {
        for (std::size_t id = 0; id < shaders::maxVertexAttributeCountPerShader; ++id) {
            const auto& previous = vertexAttributes->get(id);
            const auto& next = attrs->get(id);
            if (static_cast<bool>(previous) != static_cast<bool>(next)) {
                attributeBindingsChanged = true;
                break;
            }
            if (!previous) continue;

            if (previous->getSharedRawData() != next->getSharedRawData() ||
                previous->getSharedOffset() != next->getSharedOffset() ||
                previous->getSharedVertexOffset() != next->getSharedVertexOffset() ||
                previous->getSharedStride() != next->getSharedStride() ||
                previous->getSharedType() != next->getSharedType()) {
                attributeBindingsChanged = true;
                break;
            }
        }
    }

    const bool attributesChanged = attributeBindingsChanged || !attributeUpdateTime ||
                                   (attrs && attrs->isModifiedAfter(*attributeUpdateTime));
    const bool indicesChanged = indexVector != indices || vertexCount != count;
    bool segmentsChanged = segments.size() != segmentCount;
    if (!segmentsChanged) {
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const auto& previousMode = segments[i]->getMode();
            const auto& previous = segments[i]->getSegment();
            const auto& next = segs[i];
            if (previousMode.type != mode.type || previousMode.size != mode.size ||
                previous.vertexOffset != next.vertexOffset || previous.indexOffset != next.indexOffset ||
                previous.vertexLength != next.vertexLength || previous.indexLength != next.indexLength ||
                previous.sortKey != next.sortKey) {
                segmentsChanged = true;
                break;
            }
        }
    }

    vertexAttributes = std::move(attrs);
    vertexCount = count;
    indexVector = std::move(indices);
    if (attributesChanged || indicesChanged || segmentsChanged) {
        ++bufferVersion;
    }
    attributeUpdateTime = util::MonotonicTimer::now();

    if (segmentsChanged) {
        std::vector<UniqueDrawSegment> drawSegs;
        drawSegs.reserve(segmentCount);
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const auto& s = segs[i];
            drawSegs.push_back(std::make_unique<gfx::Drawable::DrawSegment>(
                mode, SegmentBase{s.vertexOffset, s.indexOffset, s.vertexLength, s.indexLength, s.sortKey}));
        }
        segments = std::move(drawSegs);
    }
    // TODO: extract raw vertex data from attribute bindings for updateVertexAttributes path
}

} // namespace command_export
} // namespace mbgl
