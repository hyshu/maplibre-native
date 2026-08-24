#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace mbgl {
namespace command_export {

/// Shader type enum — determines the vertex format and consumer pipeline.
enum class ShaderType : uint32_t {
    Fill = 0,
    FillOutline = 1,
    Line = 2,
    Background = 3,
    FillExtrusion = 4,
    LineSDF = 5,                  // dashed lines (line-dasharray), samples dash atlas
    LineGradient = 6,             // line-gradient, samples color ramp texture
    LinePattern = 7,              // line-pattern, samples tile icon atlas
    Circle = 8,                   // circle layer (POIs, dots)
    Raster = 9,                   // raster tiles (satellite imagery etc.), samples tile texture
    FillOutlineTriangulated = 10, // antialiased fill outline (LineLayoutVertex; optional paint ranges)
    ClippingMask = 11,            // tile clipping quad; writes only the stencil attachment
    BackgroundPattern = 12,       // repeating/crossfaded background pattern atlas
    // Future: Symbol, ...
    Unknown = 255
};

/// Primitive draw mode
enum class DrawModeType : uint32_t {
    Triangles = 0,
    Lines = 1,
    LineStrip = 2,
    Points = 3,
};

/// Texture sampler filter exported to the consumer. Values are part of the ABI.
enum class TextureFilterType : uint32_t {
    Nearest = 0,
    Linear = 1,
};

/// Resolved stencil behavior exported to the consumer. Values are part of the ABI.
/// The named modes correspond exactly to the modes produced by
/// PaintParameters::renderTileClippingMasks/stencilModeForClipping/
/// stencilModeFor3D. Clear is an ordered control command with no geometry.
enum class StencilModeType : uint32_t {
    Disabled = 0,
    ClippingMask = 1,  // compare Always, pass Replace, write mask 0xff
    ClippingTest = 2,  // compare Equal, pass Replace, write mask 0x00
    FillExtrusion = 3, // compare NotEqual, pass Replace, write mask 0xff
    Clear = 4,         // clear the stencil attachment to stencilReference
};

/// DrawCommand::flags bits. Values are part of the FFI ABI.
namespace DrawCommandFlags {
constexpr uint32_t CrossTileMerged = 1u << 0;
constexpr uint32_t FillExtrusionDataDriven = 1u << 1;
constexpr uint32_t FillColorDataDriven = 1u << 2;
constexpr uint32_t FillOpacityDataDriven = 1u << 3;
constexpr uint32_t FillExtrusionColorDataDriven = 1u << 4;
constexpr uint32_t CircleColorDataDriven = 1u << 5;
constexpr uint32_t CircleRadiusDataDriven = 1u << 6;
constexpr uint32_t CircleBlurDataDriven = 1u << 7;
constexpr uint32_t CircleOpacityDataDriven = 1u << 8;
constexpr uint32_t CircleStrokeColorDataDriven = 1u << 9;
constexpr uint32_t CircleStrokeWidthDataDriven = 1u << 10;
constexpr uint32_t CircleStrokeOpacityDataDriven = 1u << 11;
constexpr uint32_t LineColorDataDriven = 1u << 12;
constexpr uint32_t LineBlurDataDriven = 1u << 13;
constexpr uint32_t LineOpacityDataDriven = 1u << 14;
constexpr uint32_t LineGapWidthDataDriven = 1u << 15;
constexpr uint32_t LineOffsetDataDriven = 1u << 16;
constexpr uint32_t LineWidthDataDriven = 1u << 17;
constexpr uint32_t LineFloorWidthDataDriven = 1u << 18;
constexpr uint32_t LinePatternDataDriven = 1u << 19;
constexpr uint32_t FillOutlineColorDataDriven = 1u << 20;
constexpr uint32_t FillOutlineOpacityDataDriven = 1u << 21;
// Resolved at draw time through PaintParameters. Exporting the effective
// state (rather than Drawable's requested state) preserves opaquePassCutoff.
constexpr uint32_t DepthTest = 1u << 22;
constexpr uint32_t DepthWrite = 1u << 23;
constexpr uint32_t FillDataDrivenMask = FillColorDataDriven | FillOpacityDataDriven;
constexpr uint32_t FillOutlineDataDrivenMask = FillOutlineColorDataDriven | FillOutlineOpacityDataDriven;
constexpr uint32_t CircleDataDrivenMask = FillExtrusionColorDataDriven | CircleColorDataDriven | CircleRadiusDataDriven |
                                          CircleBlurDataDriven | CircleOpacityDataDriven | CircleStrokeColorDataDriven |
                                          CircleStrokeWidthDataDriven | CircleStrokeOpacityDataDriven;
constexpr uint32_t LineDataDrivenMask = LineColorDataDriven | LineBlurDataDriven | LineOpacityDataDriven |
                                        LineGapWidthDataDriven | LineOffsetDataDriven | LineWidthDataDriven |
                                        LineFloorWidthDataDriven | LinePatternDataDriven;
static_assert(CircleDataDrivenMask == 0xFF0u);
static_assert(LineDataDrivenMask == 0xFF000u);
static_assert(FillOutlineDataDrivenMask == 0x300000u);
static_assert((DepthTest & DepthWrite) == 0u);
static_assert((DepthTest | DepthWrite) == 0xC00000u);
} // namespace DrawCommandFlags

/// A single draw command, serialized by Drawable::draw() for an external renderer.
///
/// Vertex/index data is referenced by pointer (zero-copy).
/// The underlying VertexVector memory is stable for the tile's lifetime.
/// UBO data is embedded (small, changes per frame due to camera matrix).
struct DrawCommand {
    ShaderType shaderType; //  0: shader/pipeline selection
    DrawModeType drawMode; //  4: primitive topology

    // Vertex data — direct pointer, no copy
    const void* vertexData; //  8: pointer to raw vertex data (stable memory)
    uint32_t vertexCount;   // 16: number of vertices
    uint32_t vertexStride;  // 20: bytes per vertex

    // Index data — direct pointer, no copy
    const uint16_t* indexData; // 24: pointer to raw index data (stable memory)
    uint32_t indexCount;       // 32: number of indices
    uint32_t flags;            // 36: see DrawCommandFlags

    // Embedded UBO data (small, changes per frame)
    uint8_t drawableUBO[128]; // 40: per-drawable UBO (matrix, etc.)
    uint32_t drawableUBOSize; // 168

    // 96 bytes: FillExtrusionPropsUBO (80) is the largest exported props UBO
    uint8_t propsUBO[96];  // 172: evaluated properties UBO (color, opacity, etc.)
    uint32_t propsUBOSize; // 268

    uint32_t layerIndex; // 272: layer index for cross-tile batching

    // Buffer identity for consumer-side GPU buffer caching. Raw pointers are
    // unsafe as cache keys: freed tile memory can be reallocated at the same
    // address for different data. bufferId is unique per drawable (never
    // reused); bufferVersion tracks vertex/attribute generations.
    uint32_t bufferId;      // 276
    uint32_t bufferVersion; // 280

    // Texture export (dash atlas, gradient ramp, pattern atlas).
    // texData points at the command_export::Texture2D CPU pixels — stable while
    // the texture object is alive. Consumers can cache GPU textures by
    // (texId, texVersion).
    uint32_t texChannels; // 284: 0 = no texture, 1 = alpha8, 4 = rgba8
    const void* texData;  // 288
    uint32_t texWidth;    // 296
    uint32_t texHeight;   // 300
    uint32_t texId;       // 304: unique per Texture2D instance
    uint32_t texVersion;  // 308: bumped on every (re)upload

    // Per-tile fragment UBO (LineSDFTilePropsUBO / LinePatternTilePropsUBO)
    uint8_t tilePropsUBO[64];  // 312
    uint32_t tilePropsUBOSize; // 376

    // Camera-to-center distance in pixels (TransformState). Needed by the
    // circle shader for scale-with-map / pitch-with-map sizing.
    float cameraDistance; // 380

    // Sampler filter configured on the exported Texture2D. Kept at the end
    // so adding it does not shift any existing field offsets.
    TextureFilterType texFilter; // 384

    // Native drawable ordering within a style layer. This occupies the
    // struct's former tail padding, preserving the 392-byte FFI ABI size.
    int32_t subLayerIndex; // 388

    // Resolved stencil state. Tail fields intentionally preserve every
    // existing offset while extending the FFI ABI from 392 to 400 bytes.
    uint32_t stencilReference;   // 392
    StencilModeType stencilMode; // 396

    // Index generation is independent from vertex/paint attribute updates.
    // Consumers use this for index-buffer caching so paint-only vertex changes
    // do not invalidate immutable index data.
    uint32_t indexVersion; // 400
};
static_assert(sizeof(DrawCommand) == 408, "DrawCommand size must be stable for FFI");
static_assert(static_cast<uint32_t>(ShaderType::ClippingMask) == 11);
static_assert(static_cast<uint32_t>(ShaderType::BackgroundPattern) == 12);
static_assert(static_cast<uint32_t>(TextureFilterType::Nearest) == 0);
static_assert(static_cast<uint32_t>(TextureFilterType::Linear) == 1);
static_assert(static_cast<uint32_t>(StencilModeType::Disabled) == 0);
static_assert(static_cast<uint32_t>(StencilModeType::ClippingMask) == 1);
static_assert(static_cast<uint32_t>(StencilModeType::ClippingTest) == 2);
static_assert(static_cast<uint32_t>(StencilModeType::FillExtrusion) == 3);
static_assert(static_cast<uint32_t>(StencilModeType::Clear) == 4);

// ── ABI offset locks (single source of truth) ───────────────────────
// Every field's byte offset is pinned here and verified by the compiler
// via offsetof. Reordering or resizing a field breaks the build instead
// of silently corrupting consumer-side reads. Binding generators can parse
// these locks to keep their offsets synchronized with this struct.
#define COMMAND_EXPORT_ABI_OFFSET(S, field, off) \
    static_assert(offsetof(S, field) == (off), #S "::" #field " ABI offset changed")
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, shaderType, 0);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, drawMode, 4);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexData, 8);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexCount, 16);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, vertexStride, 20);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, indexData, 24);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, indexCount, 32);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, flags, 36);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, drawableUBO, 40);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, drawableUBOSize, 168);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, propsUBO, 172);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, propsUBOSize, 268);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, layerIndex, 272);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, bufferId, 276);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, bufferVersion, 280);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texChannels, 284);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texData, 288);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texWidth, 296);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texHeight, 300);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texId, 304);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texVersion, 308);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, tilePropsUBO, 312);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, tilePropsUBOSize, 376);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, cameraDistance, 380);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, texFilter, 384);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, subLayerIndex, 388);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, stencilReference, 392);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, stencilMode, 396);
COMMAND_EXPORT_ABI_OFFSET(DrawCommand, indexVersion, 400);

/// Per-frame data accumulated during render and read by an external consumer.
struct FrameData {
    std::vector<DrawCommand> commands;
    std::optional<std::array<float, 4>> clearColor;

    void clear() {
        commands.clear();
        clearColor.reset();
    }

    /// Add a draw command with direct pointers to vertex/index data (zero-copy)
    DrawCommand& addCommand(ShaderType shader,
                            DrawModeType mode,
                            const void* vertices,
                            uint32_t vertexStride,
                            uint32_t vertexCount,
                            const uint16_t* indices,
                            uint32_t indexCount) {
        DrawCommand cmd{};
        cmd.shaderType = shader;
        cmd.drawMode = mode;
        cmd.vertexData = vertices;
        cmd.vertexStride = vertexStride;
        cmd.vertexCount = vertexCount;
        cmd.indexData = indices;
        cmd.indexCount = indexCount;
        commands.push_back(cmd);
        return commands.back();
    }
};

/// Global frame data — singleton, accessed from Drawable::draw() and FFI
FrameData& getFrameData();

/// Current layer index — set by LayerGroup::render(), read while exporting masks and drawables.
void setCurrentLayerIndex(uint32_t idx);
uint32_t getCurrentLayerIndex();

} // namespace command_export
} // namespace mbgl
